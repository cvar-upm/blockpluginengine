#pragma once

#include <memory>

namespace bpe {

class BlockScheduler;

class BlockPluginManager {
public:
  explicit BlockPluginManager(std::shared_ptr<BlockScheduler>);

  bool configure();
  bool start();
  bool stop();
  bool cleanup();

private:
  std::shared_ptr<BlockScheduler> scheduler_;
};

} // namespace bpe
