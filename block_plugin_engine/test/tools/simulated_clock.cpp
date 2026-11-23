// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#include "simulated_clock.hpp"

namespace bpe::test {

SchedulerClockMock& integration_clock() {
  static SchedulerClockMock clock;
  return clock;
}

} // namespace bpe::test
