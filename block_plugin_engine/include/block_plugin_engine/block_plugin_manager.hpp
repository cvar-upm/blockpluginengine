#pragma once

#include <memory>

namespace bpe {

class IBlockScheduler;

class BlockPluginManager {
public:
  explicit BlockPluginManager(std::shared_ptr<IBlockScheduler>);

  bool configure();
  bool start();
  bool stop();
  bool cleanup();

private:
  std::shared_ptr<IBlockScheduler> scheduler_;
};

} // namespace bpe
