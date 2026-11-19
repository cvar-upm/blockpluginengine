#include "simulated_clock.hpp"

namespace bpe::test {

SchedulerClockMock& integration_clock() {
  static SchedulerClockMock clock;
  return clock;
}

} // namespace bpe::test
