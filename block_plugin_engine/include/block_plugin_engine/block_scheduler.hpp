#pragma once

namespace bpe
{

class BlockScheduler
{
public:
    virtual ~BlockScheduler() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void clear() = 0;
};

}  // namespace bpe
