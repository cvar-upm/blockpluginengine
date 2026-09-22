#include "block_plugin_engine/bpe_simple_scheduler.hpp"

namespace bpe
{

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
