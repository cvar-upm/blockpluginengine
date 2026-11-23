#pragma once

namespace bpe {

enum class ExecutionResult { SUCCESS, FAILURE };

class IScheduledBlock {
public:
  virtual ~IScheduledBlock() = default;

  virtual ExecutionResult execute() = 0;
};

} // namespace bpe
