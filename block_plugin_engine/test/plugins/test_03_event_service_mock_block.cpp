#include <pluginlib/class_list_macros.hpp>
#include "block_plugin_engine/iblock_plugin.hpp"
#include "block_plugin_engine/event_service.hpp"
#include "tools/event_log.hpp"

namespace bpe::test {
class TestEventServiceMockBlock : public IBlockPlugin, public IEventServiceUser {
public:
  void set_event_service(EventService& service) override { service_ = &service; record_event("event.inject"); }
  BlockResult configure(const BlockConfig&) override {
    record_event(service_ == &EventService::instance() ? "event.configure:injected" : "event.configure:missing");
    return service_ == &EventService::instance() ? BlockResult::SUCCESS : BlockResult::FAILURE;
  }
private:
  EventService* service_{nullptr};
};
} // namespace bpe::test
PLUGINLIB_EXPORT_CLASS(bpe::test::TestEventServiceMockBlock, bpe::IBlockPlugin)
