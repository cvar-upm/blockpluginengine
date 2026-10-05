#pragma once

#include "bpe/lfbb/producer_grant.hpp"

namespace bpe::lfbb {

template <typename ProtectedData> class Blackboard;

namespace details {
template <typename ProtectedData> class ConcurrentRing;
} // namespace details

template <typename ProtectedData> class ProducerGrantFactory {
public:
  ~ProducerGrantFactory() = default;

  ProducerGrantFactory(const ProducerGrantFactory&) = delete;
  ProducerGrantFactory& operator=(const ProducerGrantFactory&) = delete;
  ProducerGrantFactory(ProducerGrantFactory&& other);
  ProducerGrantFactory& operator=(ProducerGrantFactory&&) = delete;

  ProducerGrant<ProtectedData> write_access();

private:
  using Ring = details::ConcurrentRing<ProtectedData>;

  explicit ProducerGrantFactory(Ring* ring) noexcept;

  Ring* ring_{nullptr};
  bool active_{false};

  friend class Blackboard<ProtectedData>;
};

} // namespace bpe::lfbb

#include "bpe/lfbb/details/producer_grant_factory_impl.hpp"
