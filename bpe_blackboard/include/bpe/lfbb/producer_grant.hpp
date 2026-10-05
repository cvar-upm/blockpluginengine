#pragma once

namespace bpe::lfbb {

template <typename ProtectedData> class ProducerGrantFactory;

namespace details {
template <typename ProtectedData> class ConcurrentRing;
template <typename ProtectedData> class ConcurrentSlot;
} // namespace details

template <typename ProtectedData> class ProducerGrant {
public:
  ~ProducerGrant();

  ProtectedData& data() noexcept;

  ProducerGrant(const ProducerGrant&) = delete;
  ProducerGrant& operator=(const ProducerGrant&) = delete;
  ProducerGrant(ProducerGrant&&) = delete;
  ProducerGrant& operator=(ProducerGrant&&) = delete;

private:
  using Ring = details::ConcurrentRing<ProtectedData>;
  using Slot = details::ConcurrentSlot<ProtectedData>;

  ProducerGrant(Ring* ring, bool& active) noexcept;

  Ring* ring_{nullptr};
  Slot* slot_{nullptr};
  bool& active_;

  friend class ProducerGrantFactory<ProtectedData>;
};

} // namespace bpe::lfbb

#include "bpe/lfbb/details/producer_grant_impl.hpp"
