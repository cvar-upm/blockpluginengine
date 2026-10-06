#pragma once

#include "bpe/lfbb/details/concurrent_ring.hpp"

namespace bpe::lfbb {

template <typename ProtectedData, std::size_t InitializationMargin>
ProducerGrant<ProtectedData, InitializationMargin>::ProducerGrant(Ring* ring,
                                                                   bool& active) noexcept
    : ring_(ring), slot_(ring_->claim_next_write_slot()), active_(active) {}

template <typename ProtectedData, std::size_t InitializationMargin>
ProducerGrant<ProtectedData, InitializationMargin>::~ProducerGrant() {
  ring_->commit_written_slot();
  active_ = false;
}

template <typename ProtectedData, std::size_t InitializationMargin>
ProtectedData& ProducerGrant<ProtectedData, InitializationMargin>::data() noexcept {
  return slot_->data();
}

} // namespace bpe::lfbb
