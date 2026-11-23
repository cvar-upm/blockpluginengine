#include <chrono>
#include <cstdint>
#include <future>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "block_plugin_engine/typed_blackboard.hpp"

namespace bpe_test::test {
namespace {
using namespace std::chrono_literals;

// A native, non-ROS object. Its invariant would be observable as a torn
// update if a reader could see the producer's uncommitted slot.
struct Sample {
  std::uint64_t sequence{0};
  std::vector<std::uint64_t> values{0, 0, 0};
};

void fill(Sample& sample, std::uint64_t sequence) {
  sample.sequence = sequence;
  sample.values[0] = sequence;
  sample.values[1] = sequence * 2;
  sample.values[2] = sequence * 3;
}

bool consistent(const Sample& sample) {
  return sample.values.size() == 3 &&
         sample.values[0] == sample.sequence &&
         sample.values[1] == sample.sequence * 2 &&
         sample.values[2] == sample.sequence * 3;
}

// Each endpoint belongs to one thread. A producer grant remains alive while
// the reader obtains a separate grant on another thread.
TEST(BlackboardConcurrency, UncommittedInPlaceUpdateIsInvisibleToConcurrentReader) {
  bpe::TypedProducer<Sample> producer;
  bpe::TypedConsumer<Sample> consumer;
  producer.bind("test_04_uncommitted_native");
  consumer.bind("test_04_uncommitted_native");
  {
    auto access = producer.write_access();
    fill(access.value(), 1);
  }

  std::promise<void> writer_is_editing;
  auto editing = writer_is_editing.get_future();
  std::promise<void> release_writer;
  auto release = release_writer.get_future();

  std::thread writer([&] {
    auto access = producer.write_access();
    // Deliberately leave an inconsistent, partially modified object in the
    // *uncommitted* slot. Do not copy via a temporary object.
    access.value().sequence = 2;
    access.value().values[0] = 2;
    writer_is_editing.set_value();
    (void)release.wait_for(3s);
    // Complete the sample before releasing the grant even after a timeout.
    access.value().values[1] = 4;
    access.value().values[2] = 6;
  });

  const bool signalled = editing.wait_for(3s) == std::future_status::ready;
  if (signalled) {
    auto access = consumer.read_access();
    EXPECT_TRUE(access.has_new_value());
    EXPECT_EQ(access.value().sequence, 1u);
    EXPECT_TRUE(consistent(access.value()));
  }
  release_writer.set_value();
  writer.join();
  ASSERT_TRUE(signalled) << "writer did not reach the open-grant checkpoint";

  auto committed = consumer.read_access();
  EXPECT_TRUE(committed.has_new_value());
  EXPECT_EQ(committed.value().sequence, 2u);
  EXPECT_TRUE(consistent(committed.value()));
}

// A long-lived reader grant must not force the writer to wait, nor may the
// committed data reserved by that reader be modified in place by the writer.
TEST(BlackboardConcurrency, ReaderKeepsStableSnapshotWhileWriterKeepsPublishing) {
  bpe::TypedProducer<Sample> producer;
  bpe::TypedConsumer<Sample> held_consumer;
  bpe::TypedConsumer<Sample> latest_consumer;
  producer.bind("test_04_held_reader_native");
  held_consumer.bind("test_04_held_reader_native");
  latest_consumer.bind("test_04_held_reader_native");
  {
    auto access = producer.write_access();
    fill(access.value(), 1);
  }

  std::promise<void> finished;
  auto completed = finished.get_future();
  constexpr std::uint64_t kWrites = 1000;
  std::thread writer;
  bool progressed = false;
  {
    auto held = held_consumer.read_access();
    ASSERT_TRUE(held.has_new_value());
    ASSERT_EQ(held.value().sequence, 1u);
    writer = std::thread([&] {
      for (std::uint64_t seq = 2; seq <= kWrites + 1; ++seq) {
        auto access = producer.write_access();
        fill(access.value(), seq);
      }
      finished.set_value();
    });
    progressed = completed.wait_for(3s) == std::future_status::ready;
    EXPECT_EQ(held.value().sequence, 1u);
    EXPECT_TRUE(consistent(held.value()));
    EXPECT_EQ(held.sequence(), 1u);
  }  // Release the held reader even on an unsuccessful progress check.
  if (writer.joinable()) writer.join();
  ASSERT_TRUE(progressed) << "writer could not finish with a reader grant held";

  auto latest = latest_consumer.read_access();
  EXPECT_EQ(latest.value().sequence, kWrites + 1);
  EXPECT_TRUE(consistent(latest.value()));
}

} // namespace
} // namespace bpe_test::test
