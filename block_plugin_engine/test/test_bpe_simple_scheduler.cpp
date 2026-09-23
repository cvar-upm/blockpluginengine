#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <time.h>

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include "block_plugin_engine/execution_config.hpp"
#include "block_plugin_engine/itimer_block.hpp"
#include "detail/bpe_simple_scheduler.hpp"

namespace bpe::test {
namespace {

using namespace std::chrono_literals;

constexpr std::size_t MAX_SAMPLES = 128;

std::int64_t to_nanoseconds(const timespec& time) {
  return static_cast<std::int64_t>(time.tv_sec) * 1'000'000'000LL +
         static_cast<std::int64_t>(time.tv_nsec);
}

class RecordingTimerBlock : public ITimerBlock {
public:
  explicit RecordingTimerBlock(std::chrono::nanoseconds first_execution_delay = 0ns)
    : first_execution_delay_(first_execution_delay) {}

  ExecutionResult execute() override {
    const std::size_t index = count_.fetch_add(1, std::memory_order_relaxed);

    if (index < timestamps_.size()) {
      clock_gettime(CLOCK_MONOTONIC, &timestamps_[index]);
    }

    if (index == 0 && first_execution_delay_.count() > 0) {
      std::this_thread::sleep_for(first_execution_delay_);
    }

    return ExecutionResult::SUCCESS;
  }

  std::size_t count() const {
    return count_.load(std::memory_order_acquire);
  }

  std::int64_t timestamp_ns(std::size_t index) const {
    return to_nanoseconds(timestamps_[index]);
  }

private:
  std::array<timespec, MAX_SAMPLES> timestamps_{};
  std::atomic<std::size_t> count_{0};
  std::chrono::nanoseconds first_execution_delay_;
};

bool wait_for_samples(const RecordingTimerBlock& block,
                      std::size_t expected,
                      std::chrono::milliseconds timeout) {
  const auto limit = std::chrono::steady_clock::now() + timeout;

  while (block.count() < expected && std::chrono::steady_clock::now() < limit) {
    std::this_thread::sleep_for(1ms);
  }

  return block.count() >= expected;
}

SchedulerConfig normal_test_config() {
  return YAML::Load(R"(
realtime:
  enabled: false

timing:
  spin_before_deadline: 500us
)");
}

ExecutionConfig timer_config(std::chrono::nanoseconds period) {
  ExecutionConfig config;
  config.policy = ExecutionPolicy::TIMER;
  config.period = period;

  return config;
}

TEST(BPESimpleSchedulerTest, PeriodicExecutionMaintainsPhase) {
  constexpr auto period = 20ms;
  constexpr std::size_t samples = 8;

  BPE_SimpleScheduler scheduler;
  RecordingTimerBlock block;

  ASSERT_EQ(scheduler.configure(normal_test_config()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.add("timer", block, timer_config(period)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);

  ASSERT_TRUE(wait_for_samples(block, samples, 500ms));

  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);

  constexpr auto interval_tolerance = 8ms;
  constexpr auto accumulated_tolerance = 8ms;

  for (std::size_t i = 1; i < samples; ++i) {
    const auto interval =
        std::chrono::nanoseconds{block.timestamp_ns(i) - block.timestamp_ns(i - 1)};

    EXPECT_GE(interval, period - interval_tolerance);
    EXPECT_LE(interval, period + interval_tolerance);
  }

  const auto measured_span =
      std::chrono::nanoseconds{block.timestamp_ns(samples - 1) - block.timestamp_ns(0)};

  const auto expected_span = period * static_cast<std::int64_t>(samples - 1);

  EXPECT_GE(measured_span, expected_span - accumulated_tolerance);
  EXPECT_LE(measured_span, expected_span + accumulated_tolerance);
}

TEST(BPESimpleSchedulerTest, RecoversNominalPhaseAfterOverrun) {
  constexpr auto period = 20ms;

  BPE_SimpleScheduler scheduler;

  // The first execution deliberately overruns its 20 ms period.
  RecordingTimerBlock block{35ms};

  ASSERT_EQ(scheduler.configure(normal_test_config()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.add("timer", block, timer_config(period)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);

  ASSERT_TRUE(wait_for_samples(block, 3, 500ms));

  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);

  const auto first_to_second =
      std::chrono::nanoseconds{block.timestamp_ns(1) - block.timestamp_ns(0)};

  const auto second_to_third =
      std::chrono::nanoseconds{block.timestamp_ns(2) - block.timestamp_ns(1)};

  // First execute() sleeps for 35 ms.
  EXPECT_GE(first_to_second, 30ms);
  EXPECT_LE(first_to_second, 50ms);

  // Nominal deadlines are 20, 40, 60 ms. After the first call ends around
  // 55 ms, the second execution is already overdue. The third should then
  // occur near the original 60 ms deadline, not 20 ms after the second call.
  EXPECT_LT(second_to_third, 15ms);
}

TEST(BPESimpleSchedulerTest, ScheduleIsImmutableWhileRunning) {
  BPE_SimpleScheduler scheduler;
  RecordingTimerBlock first;
  RecordingTimerBlock second;

  ASSERT_EQ(scheduler.configure(normal_test_config()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.add("first", first, timer_config(20ms)), SchedulerResult::SUCCESS);

  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);

  EXPECT_EQ(scheduler.add("second", second, timer_config(20ms)), SchedulerResult::INVALID_STATE);

  EXPECT_EQ(scheduler.remove("first"), SchedulerResult::INVALID_STATE);
  EXPECT_EQ(scheduler.clear(), SchedulerResult::INVALID_STATE);

  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);

  EXPECT_EQ(scheduler.remove("first"), SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler.clear(), SchedulerResult::SUCCESS);
}

TEST(BPESimpleSchedulerTest, RejectsInvalidConfiguration) {
  BPE_SimpleScheduler scheduler;

  const SchedulerConfig config = YAML::Load(R"(
realtime:
  enabled: false

timing:
  spin_before_deadline: nonsense
)");

  EXPECT_EQ(scheduler.configure(config), SchedulerResult::INVALID_CONFIG);
}

} // namespace
} // namespace bpe::test