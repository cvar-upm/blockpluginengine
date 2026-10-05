#pragma once

#include <stdexcept>

namespace bpe::lfbb {

template <typename ProtectedData>
ConsumerGrantFactory<ProtectedData>::ConsumerGrantFactory(Ring* ring) noexcept : ring_(ring) {}

template <typename ProtectedData>
ConsumerGrant<ProtectedData> ConsumerGrantFactory<ProtectedData>::read_access() {
  if (active_) {
    throw std::runtime_error("Consumer already active");
  } else {
    active_ = true;
  }

  return ConsumerGrant<ProtectedData>(ring_, active_, last_seen_sequence_);
}

} // namespace bpe::lfbb
