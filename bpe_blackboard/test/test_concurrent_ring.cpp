#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include "bpe/lfbb/details/concurrent_ring.hpp"

namespace bpe::lfbb::details {
namespace {

struct TestProtectedData {
  std::int32_t value{0};
  std::int32_t marker{0};
};

using TestRing = ConcurrentRing<TestProtectedData>;
using TestSlot = TestRing::Slot;

static_assert(!std::is_copy_constructible_v<TestRing>);
static_assert(!std::is_copy_assignable_v<TestRing>);
static_assert(!std::is_move_constructible_v<TestRing>);
static_assert(!std::is_move_assignable_v<TestRing>);

TEST(ConcurrentRingTests, StartsWithFourSlotsAndNoCommittedData) {
  TestRing ring;

  EXPECT_EQ(ring.capacity(), 4u);

  TestSlot* slot = ring.get_last_committed_slot();
  ASSERT_NE(slot, nullptr);
  EXPECT_EQ(slot->sequence(), 0u);
  EXPECT_EQ(slot->timestamp_ns(), 0u);
  EXPECT_EQ(slot->data().value, 0);
  EXPECT_EQ(slot->data().marker, 0);
}

TEST(ConcurrentRingTests, CapacityTracksRegisteredConsumersUsingPowerOfTwoSizing) {
  TestRing ring;

  EXPECT_EQ(ring.capacity(), 4u);

  ring.register_consumer();
  EXPECT_EQ(ring.capacity(), 4u);

  ring.register_consumer();
  EXPECT_EQ(ring.capacity(), 4u);

  ring.register_consumer();
  EXPECT_EQ(ring.capacity(), 8u);

  ring.register_consumer();
  ring.register_consumer();
  ring.register_consumer();
  EXPECT_EQ(ring.capacity(), 8u);

  ring.register_consumer();
  EXPECT_EQ(ring.capacity(), 16u);
}

TEST(ConcurrentRingTests, UncommittedWriteIsNotPublished) {
  TestRing ring;

  TestSlot* initial_slot = ring.get_last_committed_slot();
  TestSlot* write_slot = ring.claim_next_write_slot();
  ASSERT_NE(write_slot, nullptr);
  ASSERT_NE(write_slot, initial_slot);

  write_slot->data().value = 42;
  write_slot->data().marker = 7;

  EXPECT_EQ(ring.get_last_committed_slot(), initial_slot);
  EXPECT_EQ(initial_slot->sequence(), 0u);

  ring.commit_written_slot();

  TestSlot* committed_slot = ring.get_last_committed_slot();
  EXPECT_EQ(committed_slot, write_slot);
  EXPECT_EQ(committed_slot->sequence(), 1u);
  EXPECT_GT(committed_slot->timestamp_ns(), 0u);
  EXPECT_EQ(committed_slot->data().value, 42);
  EXPECT_EQ(committed_slot->data().marker, 7);
}

TEST(ConcurrentRingTests, CommitSequenceIsMonotonicAndLatestCommitIsReturned) {
  TestRing ring;

  TestSlot* first = ring.claim_next_write_slot();
  first->data().value = 10;
  ring.commit_written_slot();
  const std::uint64_t first_timestamp = first->timestamp_ns();

  TestSlot* second = ring.claim_next_write_slot();
  second->data().value = 20;
  ring.commit_written_slot();

  ASSERT_NE(first, second);
  EXPECT_EQ(first->sequence(), 1u);
  EXPECT_EQ(second->sequence(), 2u);
  EXPECT_GE(second->timestamp_ns(), first_timestamp);
  EXPECT_EQ(ring.get_last_committed_slot(), second);
  EXPECT_EQ(ring.get_last_committed_slot()->data().value, 20);
}

TEST(ConcurrentRingTests, SeveralReadersCanReserveLatestCommittedSlot) {
  TestRing ring;

  TestSlot* committed_slot = ring.claim_next_write_slot();
  committed_slot->data().value = 91;
  ring.commit_written_slot();

  TestSlot* reader_a = ring.get_last_committed_slot();
  TestSlot* reader_b = ring.get_last_committed_slot();
  TestSlot* reader_c = ring.get_last_committed_slot();

  ASSERT_EQ(reader_a, committed_slot);
  ASSERT_EQ(reader_b, committed_slot);
  ASSERT_EQ(reader_c, committed_slot);
  ASSERT_TRUE(reader_a->try_acquire_reader());
  ASSERT_TRUE(reader_b->try_acquire_reader());
  ASSERT_TRUE(reader_c->try_acquire_reader());
  EXPECT_FALSE(committed_slot->try_acquire_writer());

  reader_a->release_reader();
  reader_b->release_reader();
  reader_c->release_reader();
}

TEST(ConcurrentRingTests, WriterSkipsSlotsHeldByReaders) {
  TestRing ring;

  TestSlot* first = ring.claim_next_write_slot();
  first->data().value = 1;
  ring.commit_written_slot();
  ASSERT_TRUE(first->try_acquire_reader());

  TestSlot* second = ring.claim_next_write_slot();
  second->data().value = 2;
  ring.commit_written_slot();

  TestSlot* third = ring.claim_next_write_slot();
  third->data().value = 3;
  ring.commit_written_slot();

  TestSlot* fourth = ring.claim_next_write_slot();
  fourth->data().value = 4;
  ring.commit_written_slot();

  TestSlot* wrapped = ring.claim_next_write_slot();
  ASSERT_NE(wrapped, first);
  EXPECT_EQ(wrapped, second);
  wrapped->data().value = 5;
  ring.commit_written_slot();

  EXPECT_EQ(ring.get_last_committed_slot(), second);
  EXPECT_EQ(second->sequence(), 5u);
  EXPECT_EQ(second->data().value, 5);

  first->release_reader();
}

TEST(ConcurrentRingTests, RegisteredConsumerCapacityLeavesWritableSlotWhenReadersHoldDistinctSamples) {
  TestRing ring;
  ring.register_consumer();
  ring.register_consumer();
  ring.register_consumer();

  ASSERT_EQ(ring.capacity(), 8u);

  TestSlot* reader_slots[3]{};
  for (std::size_t index = 0; index < 3u; ++index) {
    TestSlot* slot = ring.claim_next_write_slot();
    slot->data().value = static_cast<std::int32_t>(index + 1u);
    ring.commit_written_slot();
    ASSERT_TRUE(slot->try_acquire_reader());
    reader_slots[index] = slot;
  }

  TestSlot* writable_slot = ring.claim_next_write_slot();
  ASSERT_NE(writable_slot, nullptr);
  writable_slot->data().value = 99;
  ring.commit_written_slot();

  EXPECT_EQ(ring.get_last_committed_slot(), writable_slot);
  EXPECT_EQ(ring.get_last_committed_slot()->data().value, 99);

  for (TestSlot* slot : reader_slots) {
    slot->release_reader();
  }
}

} // namespace
} // namespace bpe::lfbb::details
