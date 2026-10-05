#pragma once

#include <cstdint>

namespace bpe::lfbb {

template <typename ProtectedData> class ConsumerGrantFactory;

namespace details {
template <typename ProtectedData> class ConcurrentRing;
template <typename ProtectedData> class ConcurrentSlot;
} // namespace details

enum class ReadStatus { no_data_yet, no_update, ok, missed };

template <typename ProtectedData> class ConsumerGrant {
public:
  ~ConsumerGrant();

  const ProtectedData& data() const noexcept;

  std::uint64_t sequence() const noexcept;
  std::uint64_t timestamp_ns() const noexcept;
  ReadStatus status() const noexcept;
  bool has_new_value() const noexcept;
  bool has_missed() const noexcept;
  std::uint64_t missed() const noexcept;

  ConsumerGrant(const ConsumerGrant&) = delete;
  ConsumerGrant& operator=(const ConsumerGrant&) = delete;
  ConsumerGrant(ConsumerGrant&&) = delete;
  ConsumerGrant& operator=(ConsumerGrant&&) = delete;

private:
  using Ring = details::ConcurrentRing<ProtectedData>;
  using Slot = details::ConcurrentSlot<ProtectedData>;

  ConsumerGrant(Ring* ring, bool& active, std::uint64_t& last_seen_sequence) noexcept;

  Slot* slot_{nullptr};
  bool& active_;
  ReadStatus status_{ReadStatus::no_data_yet};
  std::uint64_t missed_{0};

  friend class ConsumerGrantFactory<ProtectedData>;
};

} // namespace bpe::lfbb

#include "bpe/lfbb/details/consumer_grant_impl.hpp"
