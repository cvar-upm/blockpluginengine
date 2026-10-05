#pragma once

#include <algorithm>
#include <chrono>

namespace bpe::lfbb::details {

template <typename ProtectedData> ConcurrentRing<ProtectedData>::ConcurrentRing() {
  resize_and_fill(4u);
}

template <typename ProtectedData> void ConcurrentRing<ProtectedData>::register_consumer() {
  ++consumer_count_;
  const std::size_t required_capacity = compute_capacity(consumer_count_);
  resize_and_fill(required_capacity);
}

template <typename ProtectedData>
typename ConcurrentRing<ProtectedData>::Slot*
ConcurrentRing<ProtectedData>::claim_next_write_slot() noexcept {
  std::size_t sequence = last_committed_index_.load(std::memory_order_acquire) + 1u;
  Slot* claimed_slot = nullptr;

  while (claimed_slot == nullptr) {
    Slot* slot = buffer_[sequence & index_mask_].get();
    if (slot->try_acquire_writer()) {
      write_claim_index_ = sequence;
      claimed_slot = slot;
    } else {
      ++sequence;
    }
  }

  return claimed_slot;
}

template <typename ProtectedData> void ConcurrentRing<ProtectedData>::commit_written_slot() noexcept {
  Slot* slot = buffer_[write_claim_index_ & index_mask_].get();

  ++commit_sequence_;
  slot->set_sequence(commit_sequence_);
  slot->set_timestamp_ns(now_ns());

  last_committed_index_.store(write_claim_index_, std::memory_order_release);
  slot->release_writer();
}

template <typename ProtectedData>
typename ConcurrentRing<ProtectedData>::Slot*
ConcurrentRing<ProtectedData>::get_last_committed_slot() noexcept {
  const std::size_t sequence = last_committed_index_.load(std::memory_order_acquire);
  return buffer_[sequence & index_mask_].get();
}

template <typename ProtectedData>
std::size_t ConcurrentRing<ProtectedData>::capacity() const noexcept {
  return capacity_;
}

template <typename ProtectedData>
void ConcurrentRing<ProtectedData>::resize_and_fill(std::size_t new_capacity) {
  if (new_capacity > capacity_) {
    buffer_.reserve(new_capacity);
    for (std::size_t index = capacity_; index < new_capacity; ++index) {
      buffer_.emplace_back(std::make_unique<Slot>());
    }
    capacity_ = new_capacity;
    index_mask_ = new_capacity - 1u;
  }
}

template <typename ProtectedData> std::uint64_t ConcurrentRing<ProtectedData>::now_ns() noexcept {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count());
}

template <typename ProtectedData>
std::size_t ConcurrentRing<ProtectedData>::next_power_of_two(std::size_t value) noexcept {
  std::size_t power = 1u;
  if (value > 1u) {
    while (power < value) {
      power <<= 1u;
    }
  }
  return power;
}

template <typename ProtectedData>
std::size_t ConcurrentRing<ProtectedData>::compute_capacity(std::size_t consumer_count) noexcept {
  const std::size_t minimum_required = std::max<std::size_t>(4u, consumer_count + 2u);
  return next_power_of_two(minimum_required);
}

} // namespace bpe::lfbb::details
