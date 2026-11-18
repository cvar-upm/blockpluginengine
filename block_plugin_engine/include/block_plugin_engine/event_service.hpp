#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace bpe {

// Event notifications carry no data. The blackboard remains the data transport.
// Exactly one publisher may own a label; the scheduler is its sole eventfd reader.
class EventService {
public:
  struct Channel {
    explicit Channel(int descriptor) : fd(descriptor) {}
    ~Channel();
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    int fd;
    std::atomic<bool> publisher_claimed{false};
    std::atomic<std::int64_t> last_signal_ns{0};
  };

  class Publisher {
  public:
    Publisher() = default;
    ~Publisher() { reset(); }
    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;
    Publisher(Publisher&& other) noexcept : channel_(std::move(other.channel_)) {}
    Publisher& operator=(Publisher&& other) noexcept {
      if (this != &other) { reset(); channel_ = std::move(other.channel_); }
      return *this;
    }
    bool signal() const noexcept;
    explicit operator bool() const noexcept { return bool(channel_); }
    void reset() noexcept;
  private:
    friend class EventService;
    explicit Publisher(std::shared_ptr<Channel> channel) : channel_(std::move(channel)) {}
    std::shared_ptr<Channel> channel_;
  };

  static EventService& instance();
  Publisher publisher(const std::string& tag);
  std::shared_ptr<Channel> channel(const std::string& tag);

private:
  std::mutex mutex_;
  std::unordered_map<std::string, std::weak_ptr<Channel>> channels_;
};

class IEventServiceUser {
public:
  virtual ~IEventServiceUser() = default;
  virtual void set_event_service(EventService& service) = 0;
};

} // namespace bpe
