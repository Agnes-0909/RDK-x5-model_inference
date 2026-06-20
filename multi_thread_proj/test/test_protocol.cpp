#include "protocol.hpp"

int main() {
  static_assert(mt::kChannelCount == 3, "channel count must stay fixed");
  static_assert(mt::kMaxDetectionsPerChannel == 256,
                "detection capacity changed unexpectedly");
  static_assert(sizeof(mt::NotifyMessage) > 0, "notify message missing");
  static_assert(sizeof(mt::NotifyMessage::notify_timestamp_ns) == sizeof(uint64_t),
                "notify timestamp missing");
  static_assert(sizeof(mt::BatchResultMessage) > 0,
                "batch result message missing");
  return 0;
}
