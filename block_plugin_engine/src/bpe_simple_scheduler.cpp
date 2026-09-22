#include "block_plugin_engine/bpe_simple_scheduler.hpp"

#include "block_plugin_engine/execution_config.hpp"
#include "block_plugin_engine/itimer_block.hpp"

namespace bpe
{

void BPE_SimpleScheduler::add(
    const std::string&,
    ITimerBlock&,
    const ExecutionConfig&)
{
    // Registration will be implemented after the PluginManager contract
    // has been defined.
}

void BPE_SimpleScheduler::remove(const std::string&)
{
    // Nothing registered yet.
}

void BPE_SimpleScheduler::start()
{
    running_ = true;
}

void BPE_SimpleScheduler::stop()
{
    running_ = false;
}

void BPE_SimpleScheduler::clear()
{
    // Nothing to clear yet.
}

}  // namespace bpe
