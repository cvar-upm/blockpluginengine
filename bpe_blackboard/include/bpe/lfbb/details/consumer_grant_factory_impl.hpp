#pragma once

#include <stdexcept>

namespace bpe::lfbb {

template <typename ProtectedData>
ConsumerGrantFactory<ProtectedData>::ConsumerGrantFactory(Ring* ring) noexcept : ring_(ring) {}

template <typename ProtectedData>
ConsumerGrantFactory<ProtectedData>::ConsumerGrantFactory(ConsumerGrantFactory&& other) {
  if (other.active_) {
    throw std::runtime_error("Cannot move consumer factory while a grant is active");
  } else {
    ring_ = other.ring_;
    last_seen_sequence_ = other.last_seen_sequence_;
    other.ring_ = nullptr;
    other.last_seen_sequence_ = 0u;
  }
}

template <typename ProtectedData>
ConsumerGrant<ProtectedData> ConsumerGrantFactory<ProtectedData>::read_access() {
  if (ring_ == nullptr) {
    throw std::runtime_error("Consumer factory has been moved from");
  } else if (active_) {
    throw std::runtime_error("Consumer already active");
  } else {
    active_ = true;
  }

  return ConsumerGrant<ProtectedData>(ring_, active_, last_seen_sequence_);
}

} // namespace bpe::lfbb
