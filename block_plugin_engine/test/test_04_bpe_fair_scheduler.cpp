#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <sched.h>
#include <string>
#include <vector>

#include "detail/base_fair_scheduler.hpp"
#include "mocks/scheduler_clock_mock.hpp"
#include "mocks/timer_mock_block.hpp"

namespace bpe::test {
namespace {
using namespace std::chrono_literals;

SchedulerConfig no_rt() {
  return YAML::Load(R"(realtime: {enabled: false}
timing: {spin_before_deadline: 0ns})");
}

YAML::Node periodic(std::chrono::nanoseconds period, std::chrono::nanoseconds offset = 0ns) {
  YAML::Node config;
  config["policy"] = "timer";
  config["period"] = std::to_string(period.count()) + "ns";
  config["offset"] = std::to_string(offset.count()) + "ns";
  return config;
}

using SimulatedFairScheduler = BaseFairScheduler<SchedulerClockMock>;

TEST(SimulatedFairSchedulerTest, RejectsInvalidConfigurationAndSchedule) {
  auto clock = std::make_shared<SchedulerClockMock>();
  SimulatedFairScheduler scheduler(clock);
  TimerMockBlock block;
  EXPECT_EQ(scheduler.configure(YAML::Load("timing: {spin_before_deadline: nonsense}")),
            SchedulerResult::INVALID_CONFIG);
  ASSERT_EQ(scheduler.configure(no_rt()), SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler.register_block("invalid", block,
      YAML::Load("{policy: nonsense, period: 10ms}")), SchedulerResult::INVALID_CONFIG);
  EXPECT_EQ(scheduler.register_block("not_a_map", block, YAML::Load("raw")),
            SchedulerResult::INVALID_CONFIG);
  EXPECT_EQ(scheduler.register_block("missing", block, YAML::Node{}),
            SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler.register_block("zero", block, periodic(0ns)), SchedulerResult::INVALID_CONFIG);
  EXPECT_EQ(scheduler.register_block("negative", block, periodic(10ms, -1ms)), SchedulerResult::INVALID_CONFIG);
  auto wrong_thread = periodic(10ms);
  wrong_thread["thread"] = 1;
  EXPECT_EQ(scheduler.register_block("thread", block, wrong_thread), SchedulerResult::INVALID_CONFIG);
  ASSERT_EQ(scheduler.register_block("first", block, periodic(10ms)), SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler.register_block("first", block, periodic(10ms)), SchedulerResult::INVALID_CONFIG);
  EXPECT_EQ(scheduler.remove("missing"), SchedulerResult::NOT_FOUND);
  EXPECT_EQ(scheduler.remove("first"), SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler.clear(), SchedulerResult::SUCCESS);
}

TEST(SimulatedFairSchedulerTest, ExactNominalPhaseAndIndices) {
  auto clock = std::make_shared<SchedulerClockMock>();
  SimulatedFairScheduler scheduler(clock);
  TimerMockBlock block;
  ASSERT_EQ(scheduler.configure(no_rt()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("periodic", block, periodic(10ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(0ns), SchedulerResult::SUCCESS);
  for (int i = 1; i <= 5; ++i) {
    clock->advance_to(i * 10ms);
    ASSERT_TRUE(block.wait_for_count(i));
  }
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  const auto samples = block.snapshots();
  ASSERT_EQ(samples.size(), 5u);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    EXPECT_EQ(samples[i].activation_index, i);
    EXPECT_EQ(samples[i].scheduled_time, (i + 1) * 10ms);
    EXPECT_EQ(samples[i].lateness, 0ns);
  }
}

TEST(SimulatedFairSchedulerTest, OrderedDeadlinesAndStableTies) {
  auto clock = std::make_shared<SchedulerClockMock>();
  SimulatedFairScheduler scheduler(clock);
  TimerMockBlock a, b, c;
  std::mutex mutex;
  std::vector<int> order;
  auto record = [&](int id) {
    std::lock_guard<std::mutex> lock(mutex);
    order.push_back(id);
  };
  a.set_on_execute([&] { record(1); });
  b.set_on_execute([&] { record(2); });
  c.set_on_execute([&] { record(3); });
  ASSERT_EQ(scheduler.configure(no_rt()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("a", a, periodic(10ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("b", b, periodic(20ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("c", c, periodic(20ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(0ns), SchedulerResult::SUCCESS);
  clock->advance_to(20ms);
  ASSERT_TRUE(a.wait_for_count(2));
  ASSERT_TRUE(b.wait_for_count(1));
  ASSERT_TRUE(c.wait_for_count(1));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  EXPECT_EQ(order, (std::vector<int>{1, 1, 2, 3}));
}

TEST(SimulatedFairSchedulerTest, OffsetsChangeNominalPhase) {
  auto clock = std::make_shared<SchedulerClockMock>();
  SimulatedFairScheduler scheduler(clock);
  TimerMockBlock a, b;
  ASSERT_EQ(scheduler.configure(no_rt()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("a", a, periodic(10ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("b", b, periodic(10ms, 5ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(0ns), SchedulerResult::SUCCESS);
  clock->advance_to(25ms);
  ASSERT_TRUE(a.wait_for_count(2));
  ASSERT_TRUE(b.wait_for_count(2));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  EXPECT_EQ(a.snapshots()[0].scheduled_time, 10ms);
  EXPECT_EQ(b.snapshots()[0].scheduled_time, 15ms);
  EXPECT_EQ(b.snapshots()[1].scheduled_time, 25ms);
}

TEST(SimulatedFairSchedulerTest, LateActivationDoesNotSkipNominalDeadlines) {
  auto clock = std::make_shared<SchedulerClockMock>();
  SimulatedFairScheduler scheduler(clock);
  TimerMockBlock block;
  ASSERT_EQ(scheduler.configure(no_rt()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("late", block, periodic(10ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(0ns), SchedulerResult::SUCCESS);
  clock->advance_to(35ms);
  ASSERT_TRUE(block.wait_for_count(3));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  const auto samples = block.snapshots();
  ASSERT_EQ(samples.size(), 3u);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    EXPECT_EQ(samples[i].scheduled_time, (i + 1) * 10ms);
    EXPECT_EQ(samples[i].lateness, 35ms - (i + 1) * 10ms);
  }
}

TEST(SimulatedFairSchedulerTest, ExecutionAdvancesSimulatedClockAndCausesOverrun) {
  auto clock = std::make_shared<SchedulerClockMock>();
  SimulatedFairScheduler scheduler(clock);
  TimerMockBlock block;
  std::atomic<int> executions{0};
  block.set_on_execute([&] {
    if (executions.fetch_add(1) == 0) clock->advance(25ms);
  });
  ASSERT_EQ(scheduler.configure(no_rt()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("overrun", block, periodic(10ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(0ns), SchedulerResult::SUCCESS);
  clock->advance_to(10ms);
  ASSERT_TRUE(block.wait_for_count(3));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  const auto samples = block.snapshots();
  EXPECT_EQ(samples[0].scheduled_time, 10ms);
  EXPECT_EQ(samples[1].scheduled_time, 20ms);
  EXPECT_EQ(samples[2].scheduled_time, 30ms);
  EXPECT_EQ(samples[1].lateness, 15ms);
  EXPECT_EQ(samples[2].lateness, 5ms);
}

TEST(SimulatedFairSchedulerTest, ScheduleImmutableWhileRunningAndStopUnblocksWait) {
  auto clock = std::make_shared<SchedulerClockMock>();
  SimulatedFairScheduler scheduler(clock);
  TimerMockBlock a, b;
  ASSERT_EQ(scheduler.configure(no_rt()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("a", a, periodic(10ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(0ns), SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler.register_block("b", b, periodic(10ms)), SchedulerResult::INVALID_STATE);
  EXPECT_EQ(scheduler.remove("a"), SchedulerResult::INVALID_STATE);
  EXPECT_EQ(scheduler.clear(), SchedulerResult::INVALID_STATE);
  // No virtual time advances. stop() must wake and join the waiting worker.
  EXPECT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler.clear(), SchedulerResult::SUCCESS);
}

struct RealtimeRequestState {
  std::atomic<int> calls{0};
  std::atomic<int> priority{-1};
  bool success{false};
};
struct RecordingRealtimePolicy {
  std::shared_ptr<RealtimeRequestState> state;
  bool request_fifo(int priority) const {
    state->priority.store(priority);
    state->calls.fetch_add(1);
    return state->success;
  }
};

TEST(SimulatedFairSchedulerTest, OverdueBlocksAreServicedFairly) {
  auto clock = std::make_shared<SchedulerClockMock>();
  SimulatedFairScheduler scheduler(clock);
  TimerMockBlock first, second;
  std::mutex mutex;
  std::vector<int> order;
  first.set_on_execute([&] { std::lock_guard<std::mutex> lock(mutex); order.push_back(1); });
  second.set_on_execute([&] { std::lock_guard<std::mutex> lock(mutex); order.push_back(2); });
  ASSERT_EQ(scheduler.configure(no_rt()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("first", first, periodic(10ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("second", second, periodic(10ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(0ns), SchedulerResult::SUCCESS);
  clock->advance_to(40ms);
  ASSERT_TRUE(first.wait_for_count(4));
  ASSERT_TRUE(second.wait_for_count(4));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  EXPECT_EQ(order, (std::vector<int>{1, 2, 1, 2, 1, 2, 1, 2}));
}

TEST(SimulatedFairSchedulerTest, SupportsBlocksWithoutExecutionInfo) {
  auto clock = std::make_shared<SchedulerClockMock>();
  SimulatedFairScheduler scheduler(clock);
  PlainTimerMockBlock block;
  ASSERT_EQ(scheduler.configure(no_rt()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("plain", block, periodic(10ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(0ns), SchedulerResult::SUCCESS);
  clock->advance_to(20ms);
  ASSERT_TRUE(block.wait_for_count(2));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  EXPECT_EQ(block.count(), 2u);
}

TEST(SimulatedFairSchedulerTest, RestartResetsNominalActivationIndex) {
  auto clock = std::make_shared<SchedulerClockMock>();
  SimulatedFairScheduler scheduler(clock);
  TimerMockBlock block;
  ASSERT_EQ(scheduler.configure(no_rt()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("block", block, periodic(10ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(0ns), SchedulerResult::SUCCESS);
  clock->advance_to(10ms);
  ASSERT_TRUE(block.wait_for_count(1));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(100ms), SchedulerResult::SUCCESS);
  clock->advance_to(110ms);
  ASSERT_TRUE(block.wait_for_count(2));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  const auto samples = block.snapshots();
  ASSERT_EQ(samples.size(), 2u);
  EXPECT_EQ(samples[0].activation_index, 0u);
  EXPECT_EQ(samples[1].activation_index, 0u);
  EXPECT_EQ(samples[1].scheduled_time, 110ms);
}

TEST(SimulatedFairSchedulerTest, RequestsRealtimePriorityAndHandlesFailure) {
  for (bool required : {false, true}) {
    auto clock = std::make_shared<SchedulerClockMock>();
    auto state = std::make_shared<RealtimeRequestState>();
    state->success = false;
    BaseFairScheduler<SchedulerClockMock, RecordingRealtimePolicy> scheduler(
        clock, RecordingRealtimePolicy{state});
    ASSERT_EQ(scheduler.configure(YAML::Load(
        std::string("realtime: {enabled: true, required: ") +
        (required ? "true" : "false") + ", priority: 42}\n"
        "timing: {spin_before_deadline: 0ns}")), SchedulerResult::SUCCESS);
    TimerMockBlock block;
    ASSERT_EQ(scheduler.register_block("block", block, periodic(10ms)), SchedulerResult::SUCCESS);
    EXPECT_EQ(scheduler.start(0ns), required ? SchedulerResult::SYSTEM_ERROR
                                               : SchedulerResult::SUCCESS);
    EXPECT_EQ(state->calls.load(), 1);
    EXPECT_EQ(state->priority.load(), 42);
    EXPECT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  }
}

TEST(ProductionFairSchedulerTest, StartsExecutesAndRestarts) {
  BPE_FairScheduler scheduler;
  TimerMockBlock block;
  ASSERT_EQ(scheduler.configure(no_rt()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("real", block, periodic(2ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);
  const bool reached = block.wait_for_count(2);
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  ASSERT_TRUE(reached);
  const auto before = block.snapshots().size();
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);
  const bool reached_again = block.wait_for_count(before + 1);
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  EXPECT_TRUE(reached_again);
}

TEST(ProductionFairSchedulerTest, ExecutesSeveralBlocks) {
  BPE_FairScheduler scheduler;
  TimerMockBlock a, b;
  ASSERT_EQ(scheduler.configure(no_rt()), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("a", a, periodic(2ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("b", b, periodic(3ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);
  const bool first = a.wait_for_count(1);
  const bool second = b.wait_for_count(1);
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  EXPECT_TRUE(first);
  EXPECT_TRUE(second);
}

TEST(ProductionFairSchedulerTest, RealtimePriorityRequiresExplicitRtEnvironment) {
  // Dedicated integration check: set BPE_TEST_REQUIRE_RT=1 in an RT-capable environment.
  if (std::getenv("BPE_TEST_REQUIRE_RT") == nullptr) {
    GTEST_SKIP() << "Set BPE_TEST_REQUIRE_RT=1 on a host permitted to use SCHED_FIFO";
  }
  BPE_FairScheduler scheduler;
  TimerMockBlock block;
  std::atomic<int> observed_policy{-1};
  std::atomic<int> observed_priority{-1};
  block.set_on_execute([&] {
    int policy = -1;
    sched_param param{};
    if (pthread_getschedparam(pthread_self(), &policy, &param) == 0) {
      observed_policy.store(policy);
      observed_priority.store(param.sched_priority);
    }
  });
  ASSERT_EQ(scheduler.configure(YAML::Load(
      "realtime: {enabled: true, required: true, priority: 42}\n"
      "timing: {spin_before_deadline: 0ns}")), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("rt", block, periodic(2ms)), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);
  const bool reached = block.wait_for_count(1);
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  ASSERT_TRUE(reached);
  EXPECT_EQ(observed_policy.load(), SCHED_FIFO);
  EXPECT_EQ(observed_priority.load(), 42);
}

} // namespace
} // namespace bpe::test
