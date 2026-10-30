#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include "block_plugin_engine/itimer_block_with_info.hpp"
#include "detail/bpe_multi_scheduler.hpp"

namespace bpe::test {
namespace {
using namespace std::chrono_literals;

class Probe final : public ITimerBlockWithInfo {
public:
  ExecutionResult execute(const TimerExecutionInfo& info) override {
    if (count_.load(std::memory_order_relaxed) == 0) first_ = info;
    count_.fetch_add(1, std::memory_order_release);
    return ExecutionResult::SUCCESS;
  }
  bool has_sample() const { return count_.load(std::memory_order_acquire) > 0; }
  TimerExecutionInfo first() const { return first_; }
private:
  TimerExecutionInfo first_{};
  std::atomic<unsigned> count_{0};
};

YAML::Node config(std::string delay = "200ms") {
  return YAML::Load("start_delay: " + delay + "\nthreads:\n"
                    "  - type: block_plugin_engine/FairScheduler\n"
                    "    realtime: {enabled: false}\n"
                    "  - type: block_plugin_engine/FairScheduler\n"
                    "    realtime: {enabled: false}\n");
}

TEST(BPEMultiSchedulerTest, SharedOriginAndIndependentBlocks) {
  BPE_MultiScheduler scheduler;
  ASSERT_EQ(scheduler.configure(config()), SchedulerResult::SUCCESS);
  Probe first, second;
  ExecutionConfig c{};
  c.period = 20ms;
  ASSERT_EQ(scheduler.add("first", first, c), SchedulerResult::SUCCESS);
  c.thread = 1;
  c.offset = 5ms;
  ASSERT_EQ(scheduler.add("second", second, c), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);
  auto deadline = std::chrono::steady_clock::now() + 2s;
  while ((!first.has_sample() || !second.has_sample()) && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  ASSERT_TRUE(first.has_sample());
  ASSERT_TRUE(second.has_sample());
  EXPECT_EQ(first.first().activation_index, 0u);
  EXPECT_EQ(second.first().activation_index, 0u);
  EXPECT_EQ(second.first().scheduled_time - first.first().scheduled_time, 5ms);
  EXPECT_EQ(scheduler.clear(), SchedulerResult::SUCCESS);
}

TEST(BPEMultiSchedulerTest, RejectsInvalidConfigurationsAndIndices) {
  BPE_MultiScheduler scheduler;
  EXPECT_EQ(scheduler.configure(YAML::Load("threads: []")), SchedulerResult::INVALID_CONFIG);
  EXPECT_EQ(scheduler.configure(config("-2ms")), SchedulerResult::INVALID_CONFIG);
  ASSERT_EQ(scheduler.configure(config()), SchedulerResult::SUCCESS);
  Probe block;
  ExecutionConfig c{};
  c.period = 20ms;
  c.thread = 2;
  EXPECT_EQ(scheduler.add("invalid", block, c), SchedulerResult::INVALID_CONFIG);
  c.thread = 1;
  EXPECT_EQ(scheduler.add("valid", block, c), SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler.add("valid", block, c), SchedulerResult::INVALID_CONFIG);
  EXPECT_EQ(scheduler.remove("valid"), SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler.remove("valid"), SchedulerResult::NOT_FOUND);
}

TEST(BPEMultiSchedulerTest, RejectsElapsedOriginAndRollsBack) {
  BPE_MultiScheduler scheduler;
  ASSERT_EQ(scheduler.configure(config("0ms")), SchedulerResult::SUCCESS);
  Probe block;
  ExecutionConfig c{};
  c.period = 20ms;
  ASSERT_EQ(scheduler.add("block", block, c), SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler.start(std::chrono::nanoseconds{1}), SchedulerResult::INVALID_CONFIG);
  EXPECT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler.clear(), SchedulerResult::SUCCESS);
}
} // namespace
} // namespace bpe::test
