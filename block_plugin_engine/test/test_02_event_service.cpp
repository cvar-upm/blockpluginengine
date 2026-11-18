#include <cerrno>
#include <cstdint>
#include <ctime>
#include <memory>
#include <utility>

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/eventfd.h>

#include "block_plugin_engine/event_service.hpp"

namespace {

using bpe::EventService;

std::int64_t monotonic_now_ns() {
  timespec ts{};
  if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
  return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

TEST(EventService, RejectsEmptyTag) {
  auto& service = EventService::instance();
  EXPECT_FALSE(service.channel(""));
  EXPECT_FALSE(service.publisher(""));
}

TEST(EventService, SameTagReturnsSameChannel) {
  auto& service = EventService::instance();
  auto first = service.channel("02/same");
  ASSERT_NE(first, nullptr);
  auto second = service.channel("02/same");
  EXPECT_EQ(first.get(), second.get());
  EXPECT_EQ(first->fd, second->fd);
}

TEST(EventService, DifferentTagsHaveIndependentChannels) {
  auto& service = EventService::instance();
  auto first = service.channel("02/independent/a");
  auto second = service.channel("02/independent/b");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first.get(), second.get());
  EXPECT_NE(first->fd, second->fd);
  EXPECT_TRUE(service.publisher("02/independent/a"));
  EXPECT_TRUE(service.publisher("02/independent/b"));
}

TEST(EventService, ChannelIsReleasedWhenUnused) {
  auto& service = EventService::instance();
  std::weak_ptr<EventService::Channel> weak;
  int descriptor = -1;
  {
    auto channel = service.channel("02/lifetime");
    ASSERT_NE(channel, nullptr);
    descriptor = channel->fd;
    weak = channel;
    EXPECT_FALSE(weak.expired());
    EXPECT_NE(::fcntl(descriptor, F_GETFD), -1);
  }
  EXPECT_TRUE(weak.expired());
  errno = 0;
  EXPECT_EQ(::fcntl(descriptor, F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
  auto replacement = service.channel("02/lifetime");
  EXPECT_NE(replacement, nullptr);
}

TEST(EventService, OnlyOnePublisherPerTag) {
  auto& service = EventService::instance();
  auto first = service.publisher("02/exclusive");
  ASSERT_TRUE(first);
  EXPECT_FALSE(service.publisher("02/exclusive"));
  EXPECT_TRUE(service.publisher("02/exclusive/other"));
}

TEST(EventService, PublisherCanBeReacquiredAfterReset) {
  auto& service = EventService::instance();
  auto first = service.publisher("02/reset");
  ASSERT_TRUE(first);
  auto channel = service.channel("02/reset");
  ASSERT_NE(channel, nullptr);
  first.reset();
  EXPECT_FALSE(first);
  auto second = service.publisher("02/reset");
  EXPECT_TRUE(second);
  EXPECT_EQ(channel.get(), service.channel("02/reset").get());
}

TEST(EventService, PublisherCanBeReacquiredAfterDestruction) {
  auto& service = EventService::instance();
  auto channel = service.channel("02/destruction");
  ASSERT_NE(channel, nullptr);
  {
    auto first = service.publisher("02/destruction");
    ASSERT_TRUE(first);
    EXPECT_FALSE(service.publisher("02/destruction"));
  }
  EXPECT_TRUE(service.publisher("02/destruction"));
}

TEST(EventService, PublisherMoveTransfersOwnership) {
  auto& service = EventService::instance();
  auto first = service.publisher("02/move/a");
  ASSERT_TRUE(first);
  auto second = std::move(first);
  EXPECT_FALSE(first);
  EXPECT_TRUE(second);
  EXPECT_FALSE(service.publisher("02/move/a"));

  auto other = service.publisher("02/move/b");
  ASSERT_TRUE(other);
  other = std::move(second);
  EXPECT_FALSE(second);
  EXPECT_TRUE(other);
  EXPECT_FALSE(service.publisher("02/move/a"));
  EXPECT_TRUE(service.publisher("02/move/b"));  // Assignment released the old channel.
  other.reset();
  EXPECT_TRUE(service.publisher("02/move/a"));
}

TEST(EventService, SignalIncrementsEventCounter) {
  auto& service = EventService::instance();
  auto channel = service.channel("02/signal");
  auto publisher = service.publisher("02/signal");
  ASSERT_NE(channel, nullptr);
  ASSERT_TRUE(publisher);
  EXPECT_TRUE(publisher.signal());
  eventfd_t count = 0;
  ASSERT_EQ(::eventfd_read(channel->fd, &count), 0);
  EXPECT_EQ(count, 1u);
  errno = 0;
  EXPECT_EQ(::eventfd_read(channel->fd, &count), -1);
  EXPECT_EQ(errno, EAGAIN);
}

TEST(EventService, MultipleSignalsAreAccumulated) {
  auto& service = EventService::instance();
  auto channel = service.channel("02/accumulate");
  auto publisher = service.publisher("02/accumulate");
  ASSERT_NE(channel, nullptr);
  ASSERT_TRUE(publisher);
  for (int i = 0; i < 5; ++i) ASSERT_TRUE(publisher.signal());
  eventfd_t count = 0;
  ASSERT_EQ(::eventfd_read(channel->fd, &count), 0);
  EXPECT_EQ(count, 5u);
  errno = 0;
  EXPECT_EQ(::eventfd_read(channel->fd, &count), -1);
  EXPECT_EQ(errno, EAGAIN);
}

TEST(EventService, SignalUpdatesMonotonicTimestamp) {
  auto& service = EventService::instance();
  auto channel = service.channel("02/timestamp");
  auto publisher = service.publisher("02/timestamp");
  ASSERT_NE(channel, nullptr);
  ASSERT_TRUE(publisher);
  EXPECT_EQ(channel->last_signal_ns.load(), 0);
  const auto before = monotonic_now_ns();
  ASSERT_GE(before, 0);
  ASSERT_TRUE(publisher.signal());
  const auto after = monotonic_now_ns();
  ASSERT_GE(after, 0);
  const auto stamp = channel->last_signal_ns.load();
  EXPECT_GE(stamp, before);
  EXPECT_LE(stamp, after);
}

TEST(EventService, SignalWithoutPublisherFails) {
  EventService::Publisher empty;
  EXPECT_FALSE(empty.signal());
  auto publisher = EventService::instance().publisher("02/inactive");
  ASSERT_TRUE(publisher);
  publisher.reset();
  EXPECT_FALSE(publisher.signal());
}

}  // namespace
