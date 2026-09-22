#pragma once

#include <string>

namespace bpe {

class ITimerBlock;
struct ExecutionConfig;

class IBlockScheduler {
public:
  virtual ~IBlockScheduler() = default;

  virtual void
  add(const std::string& instance_name, ITimerBlock& block, const ExecutionConfig& config) = 0;

  virtual void remove(const std::string& instance_name) = 0;

  virtual void start() = 0;
  virtual void stop() = 0;
  virtual void clear() = 0;
};

} // namespace bpe
