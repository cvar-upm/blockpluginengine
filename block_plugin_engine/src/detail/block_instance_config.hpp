// SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <string>

#include "block_plugin_engine/block_config.hpp"

namespace bpe {

struct BlockInstanceConfig {
  std::string name;
  std::string type;
  BlockConfig config;
  YAML::Node execution;
};

} // namespace bpe
