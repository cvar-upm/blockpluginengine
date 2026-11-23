#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <pluginlib/class_loader.hpp>
#include <yaml-cpp/yaml.h>

#include "block_plugin_engine/event_signal.hpp"
#include "block_plugin_engine/iblock_scheduler.hpp"
#include "block_plugin_engine/ischeduled_block.hpp"
#include "block_plugin_engine/typed_blackboard.hpp"

namespace bpe_test::test {
namespace {
using namespace std::chrono_literals;

// No yielding, sleeping or intentionally introduced synchronization within
// either access. A mismatched sequence, field or checksum is a torn snapshot.
struct DeterministicPayload {
  static constexpr std::size_t kDataSize = 32;
  std::uint64_t logical_sequence{0};
  std::array<std::int32_t, kDataSize> data{};

  static std::int32_t expected(std::uint64_t seq, std::size_t i) noexcept {
    // Test restricts seq to <= 1'000'000, so all calculations and checksum
    // fit into int32_t; no implementation-defined narrowing or overflow.
    return static_cast<std::int32_t>(seq * 17u + i * 3u + 1u);
  }

  void assign(std::uint64_t seq) noexcept {
    logical_sequence = seq;
    std::int64_t sum = 0;
    for (std::size_t i = 0; i + 1 < data.size(); ++i) {
      data[i] = expected(seq, i);
      sum += data[i];
    }
    data.back() = static_cast<std::int32_t>(-sum);
  }

  bool check() const noexcept {
    std::int64_t sum = 0;
    for (std::size_t i = 0; i + 1 < data.size(); ++i) {
      if (data[i] != expected(logical_sequence, i)) return false;
      sum += data[i];
    }
    return data.back() == -sum;
  }
};

class Writer final : public bpe::IScheduledBlock {
public:
  Writer(const std::string& label, std::uint64_t limit,
         std::shared_ptr<bpe::IEventSignal> signal)
      : limit_(limit), signal_(std::move(signal)) {
    producer_.bind(label);
  }

  bpe::ExecutionResult execute() override {
    const auto next = written_.load(std::memory_order_relaxed) + 1;
    if (next > limit_) return bpe::ExecutionResult::SUCCESS;
    {
      auto grant = producer_.write_access();
      grant.value().assign(next); // Direct, in-place modification.
    } // Commit before signalling the event reader.
    written_.store(next, std::memory_order_release);
    if (!signal_->signal()) {
      signal_failures_.fetch_add(1, std::memory_order_relaxed);
    }
    return bpe::ExecutionResult::SUCCESS;
  }

  std::uint64_t written() const { return written_.load(std::memory_order_acquire); }
  unsigned signal_failures() const { return signal_failures_.load(); }
private:
  bpe::TypedProducer<DeterministicPayload> producer_;
  std::uint64_t limit_;
  std::shared_ptr<bpe::IEventSignal> signal_;
  std::atomic<std::uint64_t> written_{0};
  std::atomic<unsigned> signal_failures_{0};
};

class Reader final : public bpe::IScheduledBlock {
public:
  explicit Reader(const std::string& label) { consumer_.bind(label); }

  bpe::ExecutionResult execute() override {
    auto grant = consumer_.read_access();
    if (!grant.has_new_value()) return bpe::ExecutionResult::SUCCESS;
    const auto seq = grant.value().logical_sequence;
    if (seq == 0) return bpe::ExecutionResult::SUCCESS;
    if (!grant.value().check() || seq <= last_) {
      errors_.fetch_add(1, std::memory_order_relaxed);
    } else {
      ++count_;
      last_ = seq;
      received_.store(count_, std::memory_order_release);
      last_seen_.store(seq, std::memory_order_release);
    }
    return bpe::ExecutionResult::SUCCESS;
  }

  std::uint64_t count() const { return received_.load(std::memory_order_acquire); }
  std::uint64_t last_seen() const { return last_seen_.load(std::memory_order_acquire); }
  unsigned errors() const { return errors_.load(std::memory_order_acquire); }
private:
  bpe::TypedConsumer<DeterministicPayload> consumer_;
  // Only this reader's worker thread accesses these two non-atomic variables.
  std::uint64_t last_{0};
  std::uint64_t count_{0};
  std::atomic<std::uint64_t> received_{0}, last_seen_{0};
  std::atomic<unsigned> errors_{0};
};

std::uint64_t number_of_writes() {
  constexpr std::uint64_t kDefault = 10'000;
  const char* env = std::getenv("BPE_CONCURRENCY_WRITES");
  if (!env || !*env) return kDefault;
  try {
    const auto number = std::stoull(env);
    if (number >= kDefault && number <= 1'000'000) return number;
  } catch (const std::exception&) {}
  return kDefault;
}

TEST(BlackboardConcurrency, OneTimedWriterThreeIndependentReaders) {
  const std::uint64_t limit = number_of_writes();
  constexpr const char* kLabel = "bpe_test_05_multischeduler_payload";

  // Four independent workers: TIMER writer; EVENT reader; two TIMER readers.
  // The EVENT pipelines accept at most one activation per nominal 1ms slot.
  const auto config = YAML::Load(R"(
start_delay: 200ms
threads:
  - {type: block_plugin_engine/FairScheduler, realtime: {enabled: false}}
  - type: block_plugin_engine/HybridFairScheduler
    realtime: {enabled: false}
    pipelines:
      - {name: reader_a_pipeline, policy: event, period: 1ms, signal: reader_a_ready, blocks: [reader_a]}
  - {type: block_plugin_engine/FairScheduler, realtime: {enabled: false}}
  - {type: block_plugin_engine/FairScheduler, realtime: {enabled: false}}
)");

  pluginlib::ClassLoader<bpe::IBlockScheduler> loader(
      "block_plugin_engine", "bpe::IBlockScheduler");
  auto scheduler = loader.createSharedInstance("block_plugin_engine/MultiScheduler");
  ASSERT_EQ(scheduler->configure(config), bpe::SchedulerResult::SUCCESS);
  auto* events = dynamic_cast<bpe::IEventSignalProvider*>(scheduler.get());
  ASSERT_NE(events, nullptr);
  auto signal_a = events->get_signal("reader_a_ready");
  ASSERT_TRUE(signal_a);

  Writer writer(kLabel, limit, signal_a);
  Reader reader_a(kLabel), reader_c(kLabel), reader_d(kLabel);
  bpe::TypedConsumer<DeterministicPayload> supervisor;
  supervisor.bind(kLabel);

  EXPECT_EQ(scheduler->register_block("writer", writer,
      YAML::Load("{thread: 0, policy: timer, period: 2ms}")), bpe::SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler->register_block("reader_a", reader_a,
      YAML::Load("{thread: 1}")), bpe::SchedulerResult::SUCCESS);
  EXPECT_EQ(scheduler->register_block("reader_c", reader_c,
      YAML::Load("{thread: 2, policy: timer, period: 1ms}")), bpe::SchedulerResult::SUCCESS);

  EXPECT_EQ(scheduler->register_block("reader_d", reader_d,
      YAML::Load("{thread: 3, policy: timer, period: 1ms}")), bpe::SchedulerResult::SUCCESS);

  ASSERT_EQ(scheduler->start(), bpe::SchedulerResult::SUCCESS);
  // Bounded test even if a scheduler stops progressing. Timeout increases for
  // configured larger workloads but does not depend on nominal RT guarantees.
  const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::milliseconds(limit * 3 + 10'000);
  std::uint64_t supervisor_seen = 0;
  bool finished = false;
  while (std::chrono::steady_clock::now() < deadline) {
    {
      auto grant = supervisor.read_access();
      if (grant.has_new_value() && grant.value().logical_sequence > 0) {
        if (!grant.value().check()) ADD_FAILURE() << "Supervisor saw invalid payload";
        supervisor_seen = grant.value().logical_sequence;
      }
    }
    if (supervisor_seen >= limit && reader_a.last_seen() >= limit &&
        reader_c.last_seen() >= limit && reader_d.last_seen() >= limit) {
      finished = true;
      break;
    }
    std::this_thread::sleep_for(1ms); // Test supervisor only, not a block/grant.
  }
  EXPECT_EQ(scheduler->stop(), bpe::SchedulerResult::SUCCESS);

  EXPECT_TRUE(finished) << "Timed out: supervisor=" << supervisor_seen
      << " writer=" << writer.written() << " readers="
      << reader_a.last_seen() << "," << reader_c.last_seen() << ","
      << reader_d.last_seen();
  EXPECT_EQ(writer.written(), limit);
  EXPECT_EQ(writer.signal_failures(), 0u);
  for (const auto* reader : {&reader_a, &reader_c, &reader_d}) {
    EXPECT_EQ(reader->errors(), 0u);
    EXPECT_EQ(reader->last_seen(), limit);
  }
  EXPECT_GE(reader_a.count(), limit * 90 / 100);
  EXPECT_GE(reader_c.count(), limit * 80 / 100);
  EXPECT_GE(reader_d.count(), limit * 80 / 100);
  const auto percentage = [limit](std::uint64_t count) {
    return 100.0 * static_cast<double>(count) / static_cast<double>(limit);
  };
  std::cout << "Blackboard 1W/3R: " << writer.written() << " writes; "
            << "A EVENT: " << reader_a.count() << "/" << limit
            << " (" << percentage(reader_a.count()) << "%), "
            << "C TIMER: " << reader_c.count() << "/" << limit
            << " (" << percentage(reader_c.count()) << "%), "
            << "D TIMER: " << reader_d.count() << "/" << limit
            << " (" << percentage(reader_d.count()) << "%)\n";
  EXPECT_EQ(scheduler->clear(), bpe::SchedulerResult::SUCCESS);
}

} // namespace
} // namespace bpe_test::test
