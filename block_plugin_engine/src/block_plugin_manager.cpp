#include "detail/block_plugin_manager.hpp"
#include "block_plugin_engine/iblock_scheduler.hpp"

namespace bpe {

BlockPluginManager::BlockPluginManager(std::shared_ptr<IBlockScheduler> scheduler)
  : scheduler_(scheduler) {}

bool BlockPluginManager::configure() {
  // Future:
  //   - load global configuration
  //   - instantiate BlockPlugins
  //   - configure instances
  //   - inject capabilities
  //   - register TimerBlocks in scheduler

  return true;
}

bool BlockPluginManager::start() {
  // Future:
  //   propagate BlockPlugin::start()

  return true;
}

bool BlockPluginManager::stop() {
  // Future:
  //   propagate BlockPlugin::stop()

  return true;
}

bool BlockPluginManager::cleanup() {
  // Future:
  //   propagate BlockPlugin::cleanup()
  //   destroy plugin instances

  scheduler_->clear();

  return true;
}

} // namespace bpe
