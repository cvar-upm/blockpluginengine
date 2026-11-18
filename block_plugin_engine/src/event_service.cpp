#include "block_plugin_engine/event_service.hpp"

#include <cerrno>
#include <ctime>
#include <sys/eventfd.h>
#include <unistd.h>

namespace bpe {

EventService::Channel::~Channel() { if (fd >= 0) ::close(fd); }

EventService& EventService::instance() {
  static EventService service;
  return service;
}

std::shared_ptr<EventService::Channel> EventService::channel(const std::string& tag) {
  if (tag.empty()) return {};
  // TODO: Channel lookup/creation is confined to single-threaded configuration.
  // Review removal of this lock together with the mutex declaration.
  std::lock_guard lock(mutex_);
  auto& weak = channels_[tag];
  if (auto current = weak.lock()) return current;
  const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (fd < 0) return {};
  auto created = std::make_shared<Channel>(fd);
  weak = created;
  return created;
}

EventService::Publisher EventService::publisher(const std::string& tag) {
  auto value = channel(tag);
  if (!value || value->publisher_claimed.exchange(true, std::memory_order_acq_rel)) return {};
  return Publisher(std::move(value));
}

void EventService::Publisher::reset() noexcept {
  if (channel_) {
    channel_->publisher_claimed.store(false, std::memory_order_release);
    channel_.reset();
  }
}

bool EventService::Publisher::signal() const noexcept {
  if (!channel_) return false;
  timespec ts{};
  if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return false;
  const auto time = static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
  channel_->last_signal_ns.store(time, std::memory_order_release);
  const eventfd_t one = 1;
  return ::eventfd_write(channel_->fd, one) == 0;
}

} // namespace bpe
