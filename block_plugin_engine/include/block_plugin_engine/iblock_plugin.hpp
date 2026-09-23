#pragma once

#include "block_plugin_engine/block_config.hpp"

namespace bpe {

enum class BlockResult { SUCCESS, FAILURE };

class IBlockPlugin {
public:
  virtual ~IBlockPlugin() = default;

  virtual BlockResult configure(const BlockConfig& config) = 0;
  virtual BlockResult start() = 0;
  virtual BlockResult stop() = 0;
  virtual BlockResult cleanup() = 0;
};

} // namespace bpe
