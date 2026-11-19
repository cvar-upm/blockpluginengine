#include "event_log.hpp"

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <vector>

namespace bpe::test {
namespace {

std::mutex event_mutex;
std::condition_variable event_cv;
std::vector<std::string> event_log;

} // namespace

void clear_events() {
  std::lock_guard<std::mutex> lock(event_mutex);
  event_log.clear();
}

void record_event(const std::string& event) {
  {
    std::lock_guard<std::mutex> lock(event_mutex);
    event_log.push_back(event);
  }

  event_cv.notify_all();
  std::cout << "[TEST BLOCK] " << event << std::endl;
}

std::vector<std::string> events() {
  std::lock_guard<std::mutex> lock(event_mutex);
  return event_log;
}

bool wait_for_events(std::size_t count, std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(event_mutex);
  return event_cv.wait_for(lock, timeout, [count] { return event_log.size() >= count; });
}

} // namespace bpe::test