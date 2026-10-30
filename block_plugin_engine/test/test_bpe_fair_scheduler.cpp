#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <random>
#include <thread>
#include <time.h>

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include "block_plugin_engine/execution_config.hpp"
#include "block_plugin_engine/itimer_block.hpp"
#include "block_plugin_engine/itimer_block_with_info.hpp"
#include "detail/bpe_fair_scheduler.hpp"

namespace bpe::test {
namespace {

using namespace std::chrono_literals;

constexpr std::size_t SAMPLE_COUNT = 128;
constexpr std::size_t MAX_SAMPLES = 256;
constexpr std::size_t MAX_EVENTS = 512;

constexpr auto PERIOD = 20ms;

constexpr std::int64_t PERIOD_NS =
    std::chrono::duration_cast<std::chrono::nanoseconds>(PERIOD).count();

constexpr auto NORMAL_WORK_TIME = 2ms;
constexpr auto VARIABLE_WORK_MAX_TIME = 8ms;
constexpr auto OVERRUN_WORK_TIME = 35ms;
constexpr auto OVERLOAD_WORK_TIME = 15ms;

constexpr auto INTERVAL_TOLERANCE = 3ms;
constexpr auto PHASE_TOLERANCE = 3ms;

constexpr auto TEST_TIMEOUT = 10s;

constexpr std::uint32_t RANDOM_SEED = 12345;

// Rank-two idempotent matrix. The entries are obtained at runtime to avoid
// compile-time propagation of the complete benchmark result.
constexpr double MATRIX_SCALAR = 1.0 + 1.0e-12;
volatile double runtime_one = 1.0;
volatile double work_sink = 0.0;

using Matrix = std::array<double, 9>;

std::int64_t to_nanoseconds(const timespec& time) {
  return static_cast<std::int64_t>(time.tv_sec) * 1'000'000'000LL +
         static_cast<std::int64_t>(time.tv_nsec);
}

std::int64_t now_nanoseconds() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return to_nanoseconds(now);
}

Matrix base_matrix() {
  const double one = runtime_one;
  // A = [1 0 0; 0 1 0; 1 -1 0], with A*A = A.
  return {one, 0.0, 0.0, 0.0, one, 0.0, one, -one, 0.0};
}

// A separate, ordinary matrix multiplication: input and output never alias.
// No allocations or sleeps occur inside the measured workload.
void multiply(const Matrix& left, const Matrix& right, Matrix& result) {
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t col = 0; col < 3; ++col) {
      double sum = 0.0;
      for (std::size_t k = 0; k < 3; ++k) {
        sum += left[row * 3 + k] * right[k * 3 + col];
      }
      result[row * 3 + col] = sum;
    }
  }
}

Matrix matrix_work(std::size_t iterations) {
  const Matrix a = base_matrix();
  Matrix factor = a;
  for (double& entry : factor) {
    entry *= MATRIX_SCALAR;
  }
  Matrix current = factor;
  Matrix next{};
  for (std::size_t i = 1; i < iterations; ++i) {
    multiply(current, factor, next);
    current.swap(next);
  }
  return current;
}

bool valid_matrix_result(const Matrix& result, std::size_t iterations) {
  const Matrix a = base_matrix();
  const double scale = std::pow(MATRIX_SCALAR, static_cast<double>(iterations));
  for (std::size_t i = 0; i < result.size(); ++i) {
    const double expected = scale * a[i];
    if (!std::isfinite(result[i]) ||
        std::abs(result[i] - expected) > 2e-5 * std::max(1.0, std::abs(expected))) {
      return false;
    }
  }
  return true;
}

struct Calibration {
  double iterations_per_second{};
  std::size_t sample_iterations{};
  std::int64_t elapsed_ns{};

  std::size_t for_duration(std::chrono::nanoseconds duration) const {
    const double estimated = iterations_per_second *
                             (static_cast<double>(duration.count()) / 1e9);
    return std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(estimated)));
  }
};

Calibration measure_calibration() {
  constexpr std::size_t pilot_iterations = 1'000'000;
  constexpr double target_seconds = 1.5;
  // Pilot establishes scale; one long run measures the actual throughput.
  const auto warmup = matrix_work(10'000);
  if (!valid_matrix_result(warmup, 10'000)) {
    throw std::runtime_error("Matrix warmup result is invalid");
  }
  const auto pilot_start = now_nanoseconds();
  const Matrix pilot = matrix_work(pilot_iterations);
  const auto pilot_ns = now_nanoseconds() - pilot_start;
  if (pilot_ns <= 0 || !valid_matrix_result(pilot, pilot_iterations)) {
    throw std::runtime_error("Matrix pilot measurement failed");
  }
  const double iterations = static_cast<double>(pilot_iterations) *
                            target_seconds * 1e9 / static_cast<double>(pilot_ns);
  const auto sample_iterations = static_cast<std::size_t>(
      std::clamp(iterations, 1'000'000.0,
                 static_cast<double>(std::numeric_limits<std::size_t>::max() / 2)));
  const auto start = now_nanoseconds();
  const Matrix result = matrix_work(sample_iterations);
  const auto elapsed_ns = now_nanoseconds() - start;
  if (elapsed_ns <= 0 || !valid_matrix_result(result, sample_iterations)) {
    throw std::runtime_error("Matrix calibration result is invalid");
  }
  // A classical 3x3 dense matrix product requires 27 multiplications and
  // 18 additions. This is a nominal FLOP count, not measured CPU FLOPs.
  const double rate = static_cast<double>(sample_iterations) * 1e9 /
                      static_cast<double>(elapsed_ns);
  std::cout << "[scheduler calibration] matrix=3x3, iterations=" << sample_iterations
            << ", elapsed=" << elapsed_ns / 1e9 << " s, iterations/s=" << rate
            << ", nominal GFLOP/s=" << rate * 45.0 / 1e9 << std::endl;
  work_sink = result[0];
  return {rate, sample_iterations, elapsed_ns};
}

const Calibration& calibration() {
  static const Calibration result = measure_calibration();
  return result;
}

std::size_t iterations_for_duration(std::chrono::nanoseconds target) {
  return calibration().for_duration(target);
}

class ExecutionLog {
public:
  void record(int block_id) {
    const std::size_t index = count_.load(std::memory_order_relaxed);
    if (index < events_.size()) {
      events_[index] = block_id;
    }
    count_.store(index + 1, std::memory_order_release);
  }

  std::size_t count() const {
    return count_.load(std::memory_order_acquire);
  }

  int event(std::size_t index) const {
    return events_[index];
  }

private:
  std::array<int, MAX_EVENTS> events_{};
  std::atomic<std::size_t> count_{0};
};

class RecordingTimerBlock : public ITimerBlock {
public:
  RecordingTimerBlock(std::size_t min_iterations,
                      std::size_t max_iterations,
                      std::size_t first_iterations = 0,
                      std::uint32_t seed = RANDOM_SEED,
                      ExecutionLog* log = nullptr,
                      int block_id = 0)
    : min_iterations_(min_iterations),
      max_iterations_(max_iterations),
      first_iterations_(first_iterations),
      generator_(seed),
      distribution_(min_iterations, max_iterations),
      log_(log),
      block_id_(block_id) {}

  ExecutionResult execute() override {
    const std::size_t index = started_.fetch_add(1, std::memory_order_relaxed);

    const std::int64_t start = now_nanoseconds();

    if (index < timestamps_.size()) {
      timestamps_[index] = start;
    }

    if (log_ != nullptr) {
      log_->record(block_id_);
    }

    std::size_t iterations = distribution_(generator_);

    if (index == 0 && first_iterations_ > 0) {
      iterations = first_iterations_;
    }

    const Matrix result = matrix_work(iterations);
    const std::int64_t end = now_nanoseconds();
    const bool valid = valid_matrix_result(result, iterations);
    if (!valid) {
      invalid_results_.fetch_add(1, std::memory_order_relaxed);
    }
    work_sink = result[0];

    if (index < execution_times_.size()) {
      execution_times_[index] = end - start;
      iterations_[index] = iterations;
    }
    count_.store(index + 1, std::memory_order_release);

    return ExecutionResult::SUCCESS;
  }

  std::size_t count() const {
    return count_.load(std::memory_order_acquire);
  }

  std::size_t started() const {
    return started_.load(std::memory_order_acquire);
  }

  std::size_t invalid_results() const {
    return invalid_results_.load(std::memory_order_acquire);
  }

  std::int64_t timestamp_ns(std::size_t index) const {
    return timestamps_[index];
  }

  std::chrono::nanoseconds execution_time(std::size_t index) const {
    return std::chrono::nanoseconds{execution_times_[index]};
  }

  std::size_t iterations(std::size_t index) const {
    return iterations_[index];
  }


private:
  std::array<std::int64_t, MAX_SAMPLES> timestamps_{};
  std::array<std::int64_t, MAX_SAMPLES> execution_times_{};
  std::array<std::size_t, MAX_SAMPLES> iterations_{};

  std::atomic<std::size_t> count_{0};
  std::atomic<std::size_t> started_{0};
  std::atomic<std::size_t> invalid_results_{0};

  std::size_t min_iterations_;
  std::size_t max_iterations_;
  std::size_t first_iterations_;

  std::mt19937 generator_;
  std::uniform_int_distribution<std::size_t> distribution_;

  ExecutionLog* log_;
  int block_id_;

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

bool wait_for_events(const ExecutionLog& log,
                     std::size_t expected,
                     std::chrono::milliseconds timeout) {
  const auto limit = std::chrono::steady_clock::now() + timeout;

  while (log.count() < expected && std::chrono::steady_clock::now() < limit) {
    std::this_thread::sleep_for(1ms);
  }

  return log.count() >= expected;
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

void check_block_result(const RecordingTimerBlock& block) {
  EXPECT_EQ(block.invalid_results(), 0U) << "Matrix result verification failed";
}

void report_block(const char* name, const RecordingTimerBlock& block) {
  const auto n = std::min(block.count(), MAX_SAMPLES);
  std::cerr << "[scheduler] " << name << " started=" << block.started()
            << " completed=" << block.count()
            << " invalid=" << block.invalid_results();
  if (n > 0) {
    std::int64_t min_time = std::numeric_limits<std::int64_t>::max();
    std::int64_t max_time = 0;
    long double total = 0;
    for (std::size_t i = 0; i < n; ++i) {
      const auto ns = block.execution_time(i).count();
      min_time = std::min(min_time, ns);
      max_time = std::max(max_time, ns);
      total += ns;
    }
    std::cerr << " work ms (min/mean/max)=" << min_time / 1e6 << "/"
              << static_cast<double>(total / n) / 1e6 << "/" << max_time / 1e6;
    if (n > 1) {
      long double sum_interval = 0;
      std::int64_t min_interval = std::numeric_limits<std::int64_t>::max();
      std::int64_t max_interval = 0;
      std::int64_t max_phase = 0;
      for (std::size_t i = 1; i < n; ++i) {
        const auto interval = block.timestamp_ns(i) - block.timestamp_ns(i - 1);
        min_interval = std::min(min_interval, interval);
        max_interval = std::max(max_interval, interval);
        sum_interval += interval;
        const auto phase = block.timestamp_ns(i) - block.timestamp_ns(0) -
                           static_cast<std::int64_t>(i) * PERIOD_NS;
        max_phase = std::max(max_phase, static_cast<std::int64_t>(std::llabs(phase)));
      }
      std::cerr << " intervals ms (min/mean/max)=" << min_interval / 1e6 << "/"
                << static_cast<double>(sum_interval / (n-1)) / 1e6 << "/"
                << max_interval / 1e6 << " max phase error ms=" << max_phase / 1e6;
    }
  }
  std::cerr << std::endl;
}

TEST(BPEFairSchedulerTest, PeriodicExecutionMaintainsPhase) {
  const std::size_t work_iterations = iterations_for_duration(NORMAL_WORK_TIME);


  BPE_FairScheduler scheduler;

  RecordingTimerBlock block{work_iterations, work_iterations};

  ASSERT_EQ(scheduler.configure(normal_test_config()), SchedulerResult::SUCCESS);

  ASSERT_EQ(scheduler.add("timer", block, timer_config(PERIOD)), SchedulerResult::SUCCESS);

  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);

  const bool reached = wait_for_samples(
      block, SAMPLE_COUNT, std::chrono::duration_cast<std::chrono::milliseconds>(TEST_TIMEOUT));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  report_block("timer", block);
  ASSERT_TRUE(reached) << "Timed out before completing requested samples";

  // Wall-clock jitter is diagnostic only; nominal sequencing is tested below.
  check_block_result(block);
  // Workload duration depends on host CPU scheduling and is diagnostic only.
}

TEST(BPEFairSchedulerTest, VariableExecutionLoadMaintainsPhase) {
  const std::size_t maximum_iterations = iterations_for_duration(VARIABLE_WORK_MAX_TIME);


  const std::size_t minimum_iterations = maximum_iterations / 8;

  BPE_FairScheduler scheduler;

  RecordingTimerBlock block{minimum_iterations, maximum_iterations};

  ASSERT_EQ(scheduler.configure(normal_test_config()), SchedulerResult::SUCCESS);

  ASSERT_EQ(scheduler.add("timer", block, timer_config(PERIOD)), SchedulerResult::SUCCESS);

  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);

  const bool reached = wait_for_samples(
      block, SAMPLE_COUNT, std::chrono::duration_cast<std::chrono::milliseconds>(TEST_TIMEOUT));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  report_block("timer", block);
  ASSERT_TRUE(reached) << "Timed out before completing requested samples";

  // Wall-clock jitter is diagnostic only; nominal sequencing is tested below.
  check_block_result(block);
  // Workload duration depends on host CPU scheduling and is diagnostic only.

  bool execution_time_changes = false;

  for (std::size_t i = 1; i < SAMPLE_COUNT; ++i) {
    if (block.iterations(i) != block.iterations(0)) {
      execution_time_changes = true;
    }
  }

  EXPECT_TRUE(execution_time_changes);
}

TEST(BPEFairSchedulerTest, RecoversNominalPhaseAfterOverrun) {
  const std::size_t normal_iterations = iterations_for_duration(NORMAL_WORK_TIME);

  const std::size_t overrun_iterations = iterations_for_duration(OVERRUN_WORK_TIME);

  BPE_FairScheduler scheduler;

  RecordingTimerBlock block{normal_iterations, normal_iterations, overrun_iterations};

  ASSERT_EQ(scheduler.configure(normal_test_config()), SchedulerResult::SUCCESS);

  ASSERT_EQ(scheduler.add("timer", block, timer_config(PERIOD)), SchedulerResult::SUCCESS);

  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);

  const bool reached = wait_for_samples(block, 4, 1s);
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  report_block("overrun", block);
  ASSERT_TRUE(reached) << "Timed out before completing 4 samples";

  EXPECT_GT(block.execution_time(0), PERIOD);

  // An overrun does not skip any activation; nominal phase may be
  // temporarily unattainable while the worker is catching up.
  for (std::size_t i = 1; i < 4; ++i) {
    EXPECT_GE(block.timestamp_ns(i), block.timestamp_ns(i - 1));
  }

  check_block_result(block);
  // Workload duration depends on host CPU scheduling and is diagnostic only.
}

TEST(BPEFairSchedulerTest, OverloadedBlocksRemainFair) {
  constexpr std::size_t EVENT_COUNT = 32;

  const std::size_t overload_iterations = iterations_for_duration(OVERLOAD_WORK_TIME);

  ExecutionLog log;

  RecordingTimerBlock first{overload_iterations, overload_iterations, 0, RANDOM_SEED, &log, 1};

  RecordingTimerBlock second{overload_iterations, overload_iterations, 0, RANDOM_SEED + 1, &log, 2};

  BPE_FairScheduler scheduler;

  ASSERT_EQ(scheduler.configure(normal_test_config()), SchedulerResult::SUCCESS);

  ASSERT_EQ(scheduler.add("first", first, timer_config(PERIOD)), SchedulerResult::SUCCESS);

  ASSERT_EQ(scheduler.add("second", second, timer_config(PERIOD)), SchedulerResult::SUCCESS);

  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);

  const bool reached = wait_for_events(log, EVENT_COUNT, 3s);
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  report_block("first", first);
  report_block("second", second);
  std::cerr << "[scheduler] recorded events=" << log.count() << std::endl;
  ASSERT_TRUE(reached) << "Timed out before recording 32 events";

  //
  // Verify that the test really overloaded the single scheduler thread.
  // The combined work required by both blocks must exceed one period.
  //
  ASSERT_GT(first.count(), 0U);
  ASSERT_GT(second.count(), 0U);

  const auto combined_execution_time = first.execution_time(0) + second.execution_time(0);

  std::cerr << "[scheduler] combined initial work ms="
            << combined_execution_time.count() / 1e6 << std::endl;

  //
  // A permanently overdue block must not monopolize the worker.
  // Each scheduler pass executes each overdue block at most once.
  //
  const std::size_t recorded_events = std::min(EVENT_COUNT, log.count());

  for (std::size_t i = 1; i < recorded_events; ++i) {
    EXPECT_NE(log.event(i), log.event(i - 1))
        << "Block " << log.event(i) << " executed twice consecutively at event " << i;
  }

  const auto first_count = static_cast<std::int64_t>(first.count());

  const auto second_count = static_cast<std::int64_t>(second.count());

  EXPECT_LE(std::llabs(first_count - second_count), 1);

  check_block_result(first);
  check_block_result(second);


}

TEST(BPEFairSchedulerTest, ScheduleIsImmutableWhileRunning) {
  const std::size_t work_iterations = iterations_for_duration(NORMAL_WORK_TIME);

  BPE_FairScheduler scheduler;

  RecordingTimerBlock first{work_iterations, work_iterations};

  RecordingTimerBlock second{work_iterations, work_iterations};

  ASSERT_EQ(scheduler.configure(normal_test_config()), SchedulerResult::SUCCESS);

  ASSERT_EQ(scheduler.add("first", first, timer_config(PERIOD)), SchedulerResult::SUCCESS);

  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);

  EXPECT_EQ(scheduler.add("second", second, timer_config(PERIOD)), SchedulerResult::INVALID_STATE);

  EXPECT_EQ(scheduler.remove("first"), SchedulerResult::INVALID_STATE);

  EXPECT_EQ(scheduler.clear(), SchedulerResult::INVALID_STATE);

  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);

  EXPECT_EQ(scheduler.remove("first"), SchedulerResult::SUCCESS);

  EXPECT_EQ(scheduler.clear(), SchedulerResult::SUCCESS);
}

TEST(BPEFairSchedulerTest, RejectsInvalidConfiguration) {
  BPE_FairScheduler scheduler;

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
namespace bpe::test {
namespace {

class InfoRecorder final : public ITimerBlockWithInfo {
public:
  InfoRecorder(std::vector<int>& order, int id) : order_(order), id_(id) {}

  ExecutionResult execute(const TimerExecutionInfo& info) override {
    const std::int64_t entered = now_nanoseconds();
    order_.push_back(id_);
    infos_.push_back(info);
    actual_times_.push_back(entered);
    return ExecutionResult::SUCCESS;
  }

  const std::vector<TimerExecutionInfo>& infos() const { return infos_; }
  const std::vector<std::int64_t>& actual_times() const { return actual_times_; }

private:
  std::vector<int>& order_;
  int id_;
  std::vector<TimerExecutionInfo> infos_;
  std::vector<std::int64_t> actual_times_;
};

TEST(BPEFairSchedulerTest, NominalOrderAndActivationInfo) {
  std::vector<int> order;
  InfoRecorder a(order, 1);
  InfoRecorder b(order, 2);
  InfoRecorder c(order, 3);
  BPE_FairScheduler scheduler;
  ASSERT_EQ(scheduler.configure(normal_test_config()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.add("a", a, timer_config(20ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.add("b", b, timer_config(30ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.add("c", c, timer_config(40ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);
  // The worker owns the vectors while running. Stop before reading them.
  std::this_thread::sleep_for(280ms);
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);

  const std::array<int, 13> expected{1, 2, 1, 3, 1, 2, 1, 3, 2, 1, 1, 2, 3};
  ASSERT_GE(order.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(order[i], expected[i]) << "Event " << i;
  }
  for (const auto* recorder : {&a, &b, &c}) {
    ASSERT_FALSE(recorder->infos().empty());
    const auto period = recorder == &a ? 20ms : recorder == &b ? 30ms : 40ms;
    const auto base = recorder->infos().front().scheduled_time;
    for (std::size_t i = 0; i < recorder->infos().size(); ++i) {
      const auto& info = recorder->infos()[i];
      EXPECT_EQ(info.activation_index, i);
      EXPECT_EQ(info.scheduled_time, base + period * static_cast<std::int64_t>(i));
      const auto computed_lateness =
          recorder->actual_times()[i] - info.scheduled_time.count();
      // The scheduler samples now immediately before invoking execute().
      EXPECT_GE(info.lateness.count(), 0);
      EXPECT_LE(info.lateness.count(), std::max<std::int64_t>(0, computed_lateness));
    }
  }
  // All workers in this scheduler share the same start instant.
  EXPECT_EQ(a.infos()[0].scheduled_time - 20ms,
            b.infos()[0].scheduled_time - 30ms);
  EXPECT_EQ(a.infos()[0].scheduled_time - 20ms,
            c.infos()[0].scheduled_time - 40ms);
}

} // namespace
} // namespace bpe::test

namespace bpe::test {
namespace {
TEST(BPEFairSchedulerTest, OffsetChangesNominalPhaseWithoutChangingPeriod) {
  std::vector<int> order;
  InfoRecorder first(order, 1);
  InfoRecorder second(order, 2);
  BPE_FairScheduler scheduler;
  ASSERT_EQ(scheduler.configure(normal_test_config()), SchedulerResult::SUCCESS);
  auto config_a = timer_config(20ms);
  auto config_b = timer_config(20ms);
  config_b.offset = 10ms;
  ASSERT_EQ(scheduler.add("a", first, config_a), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.add("b", second, config_b), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);
  std::this_thread::sleep_for(130ms);
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  ASSERT_GE(first.infos().size(), 4u);
  ASSERT_GE(second.infos().size(), 4u);
  EXPECT_EQ(second.infos()[0].scheduled_time - first.infos()[0].scheduled_time, 10ms);
  for (std::size_t i = 1; i < first.infos().size(); ++i) {
    EXPECT_EQ(first.infos()[i].scheduled_time - first.infos()[i-1].scheduled_time, 20ms);
  }
  for (std::size_t i = 1; i < second.infos().size(); ++i) {
    EXPECT_EQ(second.infos()[i].scheduled_time - second.infos()[i-1].scheduled_time, 20ms);
  }
}

TEST(BPEFairSchedulerTest, RejectsNegativeOffset) {
  BPE_FairScheduler scheduler;
  std::vector<int> order;
  InfoRecorder recorder(order, 1);
  ASSERT_EQ(scheduler.configure(normal_test_config()), SchedulerResult::SUCCESS);
  auto config = timer_config(20ms);
  config.offset = -1ms;
  EXPECT_EQ(scheduler.add("bad", recorder, config), SchedulerResult::INVALID_CONFIG);
}
} // namespace
} // namespace bpe::test
