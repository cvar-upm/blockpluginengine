#pragma once

#include <stdexcept>

namespace bpe::lfbb {

template <typename ProtectedData>
ProducerGrantFactory<ProtectedData>::ProducerGrantFactory(Ring* ring) noexcept : ring_(ring) {}

template <typename ProtectedData>
ProducerGrant<ProtectedData> ProducerGrantFactory<ProtectedData>::write_access() {
  if (active_) {
    throw std::runtime_error("Producer already active");
  } else {
    active_ = true;
  }

  return ProducerGrant<ProtectedData>(ring_, active_);
}

} // namespace bpe::lfbb
