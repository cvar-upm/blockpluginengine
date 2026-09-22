#pragma once

#include "block_plugin_engine/iblock_scheduler.hpp"

namespace bpe
{

class BPE_SimpleScheduler final : public IBlockScheduler
{
public:
    BPE_SimpleScheduler() = default;
    ~BPE_SimpleScheduler() override = default;

    void add(
        const std::string& instance_name,
        ITimerBlock& block,
        const ExecutionConfig& config) override;

    void remove(const std::string& instance_name) override;

    void start() override;
    void stop() override;
    void clear() override;

private:
    bool running_{false};
};

}  // namespace bpe
