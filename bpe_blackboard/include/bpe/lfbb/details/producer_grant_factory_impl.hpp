#pragma once

#include <stdexcept>

namespace bpe::lfbb {

template <typename ProtectedData>
ProducerGrantFactory<ProtectedData>::ProducerGrantFactory(Ring* ring) noexcept : ring_(ring) {}

template <typename ProtectedData>
ProducerGrantFactory<ProtectedData>::ProducerGrantFactory(ProducerGrantFactory&& other) {
  if (other.active_) {
    throw std::runtime_error("Cannot move producer factory while a grant is active");
  } else {
    ring_ = other.ring_;
    other.ring_ = nullptr;
  }
}

template <typename ProtectedData>
ProducerGrant<ProtectedData> ProducerGrantFactory<ProtectedData>::write_access() {
  if (ring_ == nullptr) {
    throw std::runtime_error("Producer factory has been moved from");
  } else if (active_) {
    throw std::runtime_error("Producer already active");
  } else {
    active_ = true;
  }

  return ProducerGrant<ProtectedData>(ring_, active_);
}

} // namespace bpe::lfbb
