// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <stdexcept>

namespace bpe::lfbb {

template <typename ProtectedData, std::size_t InitializationMargin>
ProducerGrantFactory<ProtectedData, InitializationMargin>::ProducerGrantFactory(Ring* ring) noexcept
    : ring_(ring) {}

template <typename ProtectedData, std::size_t InitializationMargin>
ProducerGrant<ProtectedData, InitializationMargin>
ProducerGrantFactory<ProtectedData, InitializationMargin>::write_access() {
  if (active_) {
    throw std::runtime_error("Producer already active");
  } else {
    active_ = true;
  }

  return ProducerGrant<ProtectedData, InitializationMargin>(ring_, active_);
}

} // namespace bpe::lfbb
