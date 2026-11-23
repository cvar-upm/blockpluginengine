// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstddef>

namespace bpe_test {

class ITwistCheckerInspection {
public:
  virtual ~ITwistCheckerInspection() = default;

  virtual std::size_t checked_samples() const noexcept = 0;
  virtual std::size_t errors() const noexcept = 0;
  virtual std::size_t missed_samples() const noexcept = 0;
};

} // namespace bpe_test
