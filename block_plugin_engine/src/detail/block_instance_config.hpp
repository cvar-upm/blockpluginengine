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
