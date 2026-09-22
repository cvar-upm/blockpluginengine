#pragma once

namespace bpe
{

struct BlockConfig;

enum class BlockResult
{
    SUCCESS,
    FAILURE
};

class IBlockPlugin
{
public:
    virtual ~IBlockPlugin() = default;

    virtual BlockResult configure(const BlockConfig& config) = 0;
    virtual BlockResult start() = 0;
    virtual BlockResult stop() = 0;
    virtual BlockResult cleanup() = 0;
};

}  // namespace bpe
