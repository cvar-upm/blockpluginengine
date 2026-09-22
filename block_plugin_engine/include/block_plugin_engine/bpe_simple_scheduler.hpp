#pragma once

#include "block_plugin_engine/block_scheduler.hpp"

namespace bpe {

class BPE_SimpleScheduler final : public BlockScheduler {
public:
  BPE_SimpleScheduler() = default;
  ~BPE_SimpleScheduler() override = default;

  void start() override;
  void stop() override;
  void clear() override;

private:
  bool running_{false};
};

} // namespace bpe
