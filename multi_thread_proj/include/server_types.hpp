#pragma once

#include <array>
#include <cstdint>

#include "protocol.hpp"

namespace mt {

constexpr uint32_t kStatusOk = 0;
constexpr uint32_t kStatusInferFailed = 1;
constexpr uint32_t kStatusTimeout = 2;
constexpr uint32_t kStatusTruncated = 3;

struct ChannelResult {
  uint32_t channel_id = 0;
  uint64_t frame_id = 0;
  uint32_t status_code = kStatusOk;
  uint32_t detect_count = 0;
  std::array<DetectionRecord, kMaxDetectionsPerChannel> detections{};
};

}  // namespace mt
