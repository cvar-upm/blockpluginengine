#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "bpe/lfbb/details/concurrent_slot.hpp"

namespace bpe::lfbb::details {

template <typename ProtectedData> class ConcurrentRing {
public:
  using Slot = ConcurrentSlot<ProtectedData>;

  ConcurrentRing();
  ~ConcurrentRing() = default;

  ConcurrentRing(const ConcurrentRing&) = delete;
  ConcurrentRing& operator=(const ConcurrentRing&) = delete;
  ConcurrentRing(ConcurrentRing&&) = delete;
  ConcurrentRing& operator=(ConcurrentRing&&) = delete;

  void register_consumer();

  Slot* claim_next_write_slot() noexcept;
  void commit_written_slot() noexcept;
  Slot* get_last_committed_slot() noexcept;

  std::size_t capacity() const noexcept;

private:
  void resize_and_fill(std::size_t new_capacity);

  static std::uint64_t now_ns() noexcept;
  static std::size_t next_power_of_two(std::size_t value) noexcept;
  static std::size_t compute_capacity(std::size_t consumer_count) noexcept;

  std::vector<std::unique_ptr<Slot>> buffer_;
  std::size_t capacity_{0};
  std::size_t index_mask_{0};
  std::size_t consumer_count_{0};
  std::size_t write_claim_index_{0};
  std::atomic<std::size_t> last_committed_index_{0};
  std::uint64_t commit_sequence_{0};
};

} // namespace bpe::lfbb::details

#include "bpe/lfbb/details/concurrent_ring_impl.hpp"
