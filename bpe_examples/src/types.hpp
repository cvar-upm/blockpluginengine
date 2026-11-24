// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <cstdint>
#include <vector>

// Private data contracts. These types are deliberately not part of the package API.
namespace bpe_examples {
struct Sample {
  std::uint64_t sequence{0};
  std::vector<double> values;
};
struct Report {
  std::uint64_t sequence{0};
  double average{0.0};
};
struct Summary {
  std::uint64_t sequence{0};
  std::vector<double> statistics;
};
} // namespace bpe_examples
