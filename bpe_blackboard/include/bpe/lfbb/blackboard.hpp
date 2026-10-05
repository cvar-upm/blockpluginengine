#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "bpe/lfbb/consumer_grant_factory.hpp"
#include "bpe/lfbb/details/concurrent_ring.hpp"
#include "bpe/lfbb/producer_grant_factory.hpp"

namespace bpe::lfbb {

template <typename ProtectedData> class Blackboard {
public:
  Blackboard() = default;
  ~Blackboard() = default;

  Blackboard(const Blackboard&) = delete;
  Blackboard& operator=(const Blackboard&) = delete;
  Blackboard(Blackboard&&) = delete;
  Blackboard& operator=(Blackboard&&) = delete;

  ProducerGrantFactory<ProtectedData> register_producer(const std::string& label);
  ConsumerGrantFactory<ProtectedData> register_consumer(const std::string& label);

  bool all_have_producer() const;
  std::vector<std::string> labels_without_producer() const;

private:
  struct Entry {
    details::ConcurrentRing<ProtectedData> ring;
    bool has_producer{false};
  };

  mutable std::mutex mutex_;
  std::map<std::string, Entry> entries_;
};

} // namespace bpe::lfbb

#include "bpe/lfbb/details/blackboard_impl.hpp"
