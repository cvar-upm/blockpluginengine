#include <pluginlib/class_list_macros.hpp>
#include "detail/base_fair_scheduler.hpp"
#include "detail/bpe_hybrid_fair_scheduler.hpp"
#include "tools/simulated_clock.hpp"

namespace bpe::test {
using SimulatedFairPlugin = BaseFairScheduler<SharedSchedulerClock>;
using SimulatedHybridPlugin = BaseHybridFairScheduler<SharedSchedulerClock>;
} // namespace bpe::test

PLUGINLIB_EXPORT_CLASS(bpe::test::SimulatedFairPlugin, bpe::IBlockScheduler)
PLUGINLIB_EXPORT_CLASS(bpe::test::SimulatedHybridPlugin, bpe::IBlockScheduler)
