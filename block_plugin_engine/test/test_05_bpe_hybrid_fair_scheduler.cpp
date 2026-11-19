#include <chrono>
#include <memory>
#include <vector>
#include <string>

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include "detail/bpe_hybrid_fair_scheduler.hpp"
#include "mocks/recording_timer_block.hpp"
#include "mocks/scheduler_clock_mock.hpp"
#include "tools/event_log.hpp"

namespace {
using namespace std::chrono_literals;
using bpe::BPE_HybridFairScheduler;
using bpe::SchedulerResult;

using bpe::test::RecordingTimerBlock;
using bpe::test::clear_events;
using bpe::test::events;
using bpe::test::wait_for_events;

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
  clear_events();
  RecordingTimerBlock a("A");
  ASSERT_EQ(scheduler.configure(YAML::Load(config)), SchedulerResult::SUCCESS);
  auto producer1 = scheduler.get_signal("data_ready");
  auto producer2 = scheduler.get_signal("data_ready");
  ASSERT_TRUE(producer1);
  ASSERT_TRUE(producer2);
  EXPECT_EQ(producer1.get(), producer2.get());
  EXPECT_FALSE(scheduler.get_signal("unknown"));
  ASSERT_EQ(scheduler.register_block("event_a", a, YAML::Node{}), SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler.start(), SchedulerResult::INVALID_CONFIG); // other blocks unresolved
  ASSERT_EQ(scheduler.clear(), SchedulerResult::SUCCESS);
  EXPECT_FALSE(producer1->signal()); // stale emitters are disabled
}

TEST(HybridFairScheduler, TimerPipelineExecutesItsBlocksInOrder) {
  BPE_HybridFairScheduler scheduler;
  clear_events();
  RecordingTimerBlock a("A"), b("B");
  ASSERT_EQ(scheduler.configure(YAML::Load(R"(
realtime: {enabled: false}
pipelines:
  - {name: periodic, policy: timer, period: 30ms, blocks: [a, b]}
)")), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("a", a, YAML::Load("{policy: wrong, period: bogus}")), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("b", b, YAML::Node{}), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);
  ASSERT_TRUE(wait_for_events(4, 2s));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  const auto result = events();
  ASSERT_GE(result.size(), 4u);
  for (std::size_t i = 0; i + 1 < result.size(); i += 2) {
    EXPECT_EQ(result[i], "A");
    EXPECT_EQ(result[i + 1], "B");
  }
}

TEST(HybridFairScheduler, EventPipelineCoalescesMultipleProducers) {
  BPE_HybridFairScheduler scheduler;
  clear_events();
  RecordingTimerBlock a("A"), b("B");
  ASSERT_EQ(scheduler.configure(YAML::Load(R"(
realtime: {enabled: false}
pipelines:
  - {name: on_data, policy: event, period: 150ms, signal: data_ready, blocks: [a, b]}
)")), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("a", a, YAML::Node{}), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("b", b, YAML::Node{}), SchedulerResult::SUCCESS);
  auto first_producer = scheduler.get_signal("data_ready");
  auto second_producer = scheduler.get_signal("data_ready");
  ASSERT_TRUE(first_producer);
  ASSERT_TRUE(second_producer);
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);
  ASSERT_TRUE(first_producer->signal());
  ASSERT_TRUE(second_producer->signal());
  ASSERT_TRUE(wait_for_events(2, 2s));
  const auto result = events();
  ASSERT_GE(result.size(), 2u);
  EXPECT_EQ(result[0], "A");
  EXPECT_EQ(result[1], "B");
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  // At most one execution per signal burst, even with several producers.
  EXPECT_EQ(events().size(), 2u);
}

TEST(HybridFairScheduler, EventSignalsDoNotProduceCatchUpExecutions) {
  BPE_HybridFairScheduler scheduler;
  clear_events();
  RecordingTimerBlock a("A");
  ASSERT_EQ(scheduler.configure(YAML::Load(R"(
realtime: {enabled: false}
pipelines:
  - {name: on_data, policy: event, period: 100ms, signal: ready, blocks: [a]}
)")), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("a", a, YAML::Node{}), SchedulerResult::SUCCESS);
  auto signal = scheduler.get_signal("ready");
  ASSERT_TRUE(signal);
  ASSERT_EQ(scheduler.start(), SchedulerResult::SUCCESS);
  for (int i = 0; i < 20; ++i) ASSERT_TRUE(signal->signal());
  ASSERT_TRUE(wait_for_events(1, 2s));
  // No buffered sequence of 20 executions should be replayed.
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  EXPECT_EQ(events().size(), 1u);
}
} // namespace

namespace {
using bpe::BaseHybridFairScheduler;
using bpe::test::SchedulerClockMock;
using SimulatedHybridFairScheduler = BaseHybridFairScheduler<SchedulerClockMock>;

// All pipelines become ready at the same virtual instant. Their execution
// order must follow the nominal k*T values, not the order in which the OS
// delivers epoll readiness. No wall-clock deadlines are involved.
TEST(SimulatedHybridFairScheduler, TwoTimerPipelinesFollowNominalPriorities) {
  clear_events();
  auto clock = std::make_shared<SchedulerClockMock>();
  clock->freeze();
  SimulatedHybridFairScheduler scheduler(clock);
  RecordingTimerBlock a("A"), b("B");
  ASSERT_EQ(scheduler.configure(YAML::Load(R"(
realtime: {enabled: false}
pipelines:
  - {name: fast, policy: timer, period: 10ms, blocks: [a]}
  - {name: slow, policy: timer, period: 20ms, blocks: [b]}
)")), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("a", a, YAML::Node{}), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("b", b, YAML::Node{}), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.start(0ns), SchedulerResult::SUCCESS);
  ASSERT_TRUE(clock->wait_until_frozen(2s));
  clock->advance_to(40ms);
  clock->release();
  ASSERT_TRUE(wait_for_events(6, 2s));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  EXPECT_EQ(events(), (std::vector<std::string>{"A", "A", "B", "A", "A", "B"}));
}

TEST(SimulatedHybridFairScheduler, TwoEventPipelinesUseNominalOrderAndCoalesce) {
  clear_events();
  auto clock = std::make_shared<SchedulerClockMock>();
  clock->freeze();
  SimulatedHybridFairScheduler scheduler(clock);
  RecordingTimerBlock a("A"), b("B");
  ASSERT_EQ(scheduler.configure(YAML::Load(R"(
realtime: {enabled: false}
pipelines:
  - {name: fast, policy: event, period: 20ms, signal: ready_a, blocks: [a]}
  - {name: slow, policy: event, period: 40ms, signal: ready_b, blocks: [b]}
)")), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("a", a, YAML::Node{}), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("b", b, YAML::Node{}), SchedulerResult::SUCCESS);
  auto a_signal = scheduler.get_signal("ready_a");
  auto b_signal = scheduler.get_signal("ready_b");
  ASSERT_TRUE(a_signal);
  ASSERT_TRUE(b_signal);
  ASSERT_EQ(scheduler.start(0ns), SchedulerResult::SUCCESS);
  ASSERT_TRUE(clock->wait_until_frozen(2s));
  clock->advance_to(40ms);
  for (int i = 0; i < 5; ++i) ASSERT_TRUE(a_signal->signal());
  for (int i = 0; i < 3; ++i) ASSERT_TRUE(b_signal->signal());
  clock->release();
  ASSERT_TRUE(wait_for_events(2, 2s));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  EXPECT_EQ(events(), (std::vector<std::string>{"A", "B"}));
}

TEST(SimulatedHybridFairScheduler, TwoTimersAndTwoEventsShareOneFairOrder) {
  clear_events();
  auto clock = std::make_shared<SchedulerClockMock>();
  clock->freeze();
  SimulatedHybridFairScheduler scheduler(clock);
  RecordingTimerBlock a("A"), b("B"), c("C"), d("D");
  ASSERT_EQ(scheduler.configure(YAML::Load(R"(
realtime: {enabled: false}
pipelines:
  - {name: timer_a, policy: timer, period: 10ms, blocks: [a]}
  - {name: timer_b, policy: timer, period: 20ms, blocks: [b]}
  - {name: event_c, policy: event, period: 20ms, signal: c_ready, blocks: [c]}
  - {name: event_d, policy: event, period: 40ms, signal: d_ready, blocks: [d]}
)")), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("a", a, YAML::Node{}), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("b", b, YAML::Node{}), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("c", c, YAML::Node{}), SchedulerResult::SUCCESS);
  ASSERT_EQ(scheduler.register_block("d", d, YAML::Node{}), SchedulerResult::SUCCESS);
  auto c_signal = scheduler.get_signal("c_ready");
  auto d_signal = scheduler.get_signal("d_ready");
  ASSERT_TRUE(c_signal);
  ASSERT_TRUE(d_signal);
  ASSERT_EQ(scheduler.start(0ns), SchedulerResult::SUCCESS);
  ASSERT_TRUE(clock->wait_until_frozen(2s));
  clock->advance_to(40ms);
  ASSERT_TRUE(c_signal->signal());
  ASSERT_TRUE(d_signal->signal());
  clock->release();
  ASSERT_TRUE(wait_for_events(8, 2s));
  ASSERT_EQ(scheduler.stop(), SchedulerResult::SUCCESS);
  EXPECT_EQ(events(), (std::vector<std::string>{
      "A", "A", "B", "C", "A", "A", "B", "D"}));
}
} // namespace
