#pragma once

#include "bpe/lfbb/details/concurrent_ring.hpp"

namespace bpe::lfbb {

template <typename ProtectedData>
ProducerGrant<ProtectedData>::ProducerGrant(Ring* ring, bool& active) noexcept
    : ring_(ring), slot_(ring_->claim_next_write_slot()), active_(active) {}

template <typename ProtectedData> ProducerGrant<ProtectedData>::~ProducerGrant() {
  ring_->commit_written_slot();
  active_ = false;
}

template <typename ProtectedData> ProtectedData& ProducerGrant<ProtectedData>::data() noexcept {
  return slot_->data();
}

} // namespace bpe::lfbb
