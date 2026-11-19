#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>
#include <utility>

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include "detail/bpe_hybrid_fair_scheduler.hpp"

namespace {
using namespace std::chrono_literals;
using bpe::BPE_HybridFairScheduler;
using bpe::ExecutionResult;
using bpe::ITimerBlock;
using bpe::SchedulerResult;

struct Trace {
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::string> calls;

  void record(const std::string& name) {
    { std::lock_guard lock(mutex); calls.push_back(name); }
    cv.notify_all();
  }
  bool wait_for(std::size_t count) {
    std::unique_lock lock(mutex);
    return cv.wait_for(lock, 2s, [&] { return calls.size() >= count; });
  }
  std::vector<std::string> snapshot() {
    std::lock_guard lock(mutex);
    return calls;
  }
};

struct Recorder final : ITimerBlock {
  Recorder(Trace& trace, std::string name) : trace(trace), name(std::move(name)) {}
  ExecutionResult execute() override { trace.record(name); return ExecutionResult::SUCCESS; }
  Trace& trace;
  std::string name;
};

const char* config = R"(
realtime: {enabled: false}
pipelines:
  - name: periodic
    policy: timer
    period: 50ms
    blocks: [timer_a, timer_b]
  - name: on_data
    policy: event
    period: 150ms
    signal: data_ready
    blocks: [event_a, event_b]
)";

TEST(HybridFairScheduler, RejectsMalformedPipelines) {
  BPE_HybridFairScheduler scheduler;
  EXPECT_EQ(scheduler.configure(YAML::Load(R"(pipelines: [{name: p, policy: event, period: 10ms, blocks: [a]}])")),
            SchedulerResult::INVALID_CONFIG);
  EXPECT_EQ(scheduler.configure(YAML::Load(R"(pipelines: [{name: p, policy: timer, period: 0ms, blocks: [a]}])")),
            SchedulerResult::INVALID_CONFIG);
  EXPECT_EQ(scheduler.configure(YAML::Load(R"(pipelines: [{name: p, policy: timer, period: 10ms, blocks: []}])")),
            SchedulerResult::INVALID_CONFIG);
}

TEST(HybridFairScheduler, SharedSignalAndMissingBlockValidation) {
  BPE_HybridFairScheduler scheduler;
  Trace trace;
  Recorder a(trace, "A");
  ASSERT_EQ(scheduler.configure(YAML::Load(config)), SchedulerResult::SUCCESS);
  auto producer1 = scheduler.get_signal("data_ready");
  auto producer2 = scheduler.get_signal("data_ready");
  ASSERT_TRUE(producer1);
  ASSERT_TRUE(producer2);
  EXPECT_EQ(producer1.get(), producer2.get());
  EXPECT_FALSE(scheduler.get_signal("unknown"));
  ASSERT_EQ(scheduler.bind_block("event_a", a), SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler.start(), SchedulerResult::INVALID_CONFIG); // other blocks unresolved
  ASSERT_EQ(scheduler.clear(), SchedulerResult::SUCCESS);
  EXPECT_FALSE(producer1->signal()); // stale emitters are disabled
}

TEST(HybridFairScheduler, TimerPipelineExecutesItsBlocksInOrder) {
  BPE_HybridFairScheduler scheduler;
  Trace trace;
  Recorder a(trace, "A"), b(trace, "B");
  ASSERT_EQ(scheduler.configure(YAML::Load(R"(
realtime: {enabled: false}
pipelines:
  - {name: periodic, policy: timer, period: 30ms, blocks: [a, b]}
)")), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.bind_block("a", a), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.bind_block("b", b), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);
  ASSERT_TRUE(trace.wait_for(4));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  const auto result = trace.snapshot();
  ASSERT_GE(result.size(), 4u);
  for (std::size_t i = 0; i + 1 < result.size(); i += 2) {
    EXPECT_EQ(result[i], "A");
    EXPECT_EQ(result[i + 1], "B");
  }
}

TEST(HybridFairScheduler, EventPipelineCoalescesMultipleProducers) {
  BPE_HybridFairScheduler scheduler;
  Trace trace;
  Recorder a(trace, "A"), b(trace, "B");
  ASSERT_EQ(scheduler.configure(YAML::Load(R"(
realtime: {enabled: false}
pipelines:
  - {name: on_data, policy: event, period: 150ms, signal: data_ready, blocks: [a, b]}
)")), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.bind_block("a", a), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.bind_block("b", b), SchedulerResult::SUCCESS);
  auto first_producer = scheduler.get_signal("data_ready");
  auto second_producer = scheduler.get_signal("data_ready");
  ASSERT_TRUE(first_producer);
  ASSERT_TRUE(second_producer);
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);
  ASSERT_TRUE(first_producer->signal());
  ASSERT_TRUE(second_producer->signal());
  ASSERT_TRUE(trace.wait_for(2));
  const auto result = trace.snapshot();
  ASSERT_GE(result.size(), 2u);
  EXPECT_EQ(result[0], "A");
  EXPECT_EQ(result[1], "B");
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  // At most one execution per signal burst, even with several producers.
  EXPECT_EQ(trace.snapshot().size(), 2u);
}

TEST(HybridFairScheduler, EventSignalsDoNotProduceCatchUpExecutions) {
  BPE_HybridFairScheduler scheduler;
  Trace trace;
  Recorder a(trace, "A");
  ASSERT_EQ(scheduler.configure(YAML::Load(R"(
realtime: {enabled: false}
pipelines:
  - {name: on_data, policy: event, period: 100ms, signal: ready, blocks: [a]}
)")), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.bind_block("a", a), SchedulerResult::SUCCESS);
  auto signal = scheduler.get_signal("ready");
  ASSERT_TRUE(signal);
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);
  for (int i = 0; i < 20; ++i) ASSERT_TRUE(signal->signal());
  ASSERT_TRUE(trace.wait_for(1));
  // No buffered sequence of 20 executions should be replayed.
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  EXPECT_EQ(trace.snapshot().size(), 1u);
}
} // namespace
