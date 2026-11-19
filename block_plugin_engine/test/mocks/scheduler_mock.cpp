#include "mocks/scheduler_mock.hpp"

#include <algorithm>
#include <utility>

namespace bpe::test {
namespace {
struct TestSignal : IEventSignal {
  bool signal() noexcept override { return true; }
};
}

void SchedulerMock::allow_signal(std::string name) {
  signal_name_ = std::move(name);
  test_signal_ = std::make_shared<TestSignal>();
}
std::shared_ptr<IEventSignal> SchedulerMock::get_signal(std::string_view name) {
  return name == signal_name_ ? test_signal_ : nullptr;
}


SchedulerResult SchedulerMock::configure(const SchedulerConfig&) {
  calls_.push_back("configure");
  return SchedulerResult::SUCCESS;
}
SchedulerResult SchedulerMock::add(const std::string& name, ITimerBlock& block,
                                   const ExecutionConfig& config) {
  calls_.push_back("add:" + name);
  if (add_result_ == SchedulerResult::SUCCESS) entries_.push_back({name, &block, config});
  return add_result_;
}
SchedulerResult SchedulerMock::remove(const std::string& name) {
  calls_.push_back("remove:" + name);
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [&](const Entry& e) { return e.name == name; }), entries_.end());
  return SchedulerResult::SUCCESS;
}
SchedulerResult SchedulerMock::start() {
  calls_.push_back("start"); running_ = true; return SchedulerResult::SUCCESS;
}
SchedulerResult SchedulerMock::start(std::chrono::nanoseconds) { return start(); }
SchedulerResult SchedulerMock::stop() {
  calls_.push_back("stop"); running_ = false; return SchedulerResult::SUCCESS;
}
SchedulerResult SchedulerMock::clear() {
  calls_.push_back("clear");
  if (clear_result_ == SchedulerResult::SUCCESS) entries_.clear();
  return clear_result_;
}
void SchedulerMock::reset() {
  entries_.clear(); calls_.clear(); add_result_ = SchedulerResult::SUCCESS;
  clear_result_ = SchedulerResult::SUCCESS; running_ = false;
}
ExecutionResult SchedulerMock::execute(const std::string& name) {
  for (const auto& e : entries_) if (e.name == name) return e.block->execute();
  return ExecutionResult::FAILURE;
}

} // namespace bpe::test
