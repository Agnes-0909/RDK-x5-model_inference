#pragma once

#include <cstddef>
#include <cstdint>

namespace mt {

constexpr uint32_t kProtocolMagic = 0x4D544946U;
constexpr uint16_t kProtocolVersion = 1;
constexpr uint16_t kChannelCount = 3;
constexpr uint16_t kQueueDepth = 4;
constexpr uint32_t kMaxDetectionsPerChannel = 256;
constexpr uint32_t kFixedWidth = 2592;
constexpr uint32_t kFixedHeight = 1944;
constexpr uint32_t kModelInputWidth = 640;
constexpr uint32_t kModelInputHeight = 640;
constexpr size_t kNv12FrameBytes =
    static_cast<size_t>(kFixedWidth) * kFixedHeight * 3 / 2;

enum class MessageType : uint16_t {
  kBatchReady = 1,
  kBatchResult = 2,
};

enum class SlotStatus : uint32_t {
  kEmpty = 0,
  kWriting = 1,
  kReady = 2,
  kReading = 3,
};

struct MessageHeader {
  uint32_t magic = kProtocolMagic;
  uint16_t version = kProtocolVersion;
  uint16_t message_type = 0;
};

struct DetectionRecord {
  int32_t class_id = -1;
  float score = 0.0F;
  float x1 = 0.0F;
  float y1 = 0.0F;
  float x2 = 0.0F;
  float y2 = 0.0F;
};

struct ChannelResultRecord {
  uint32_t channel_id = 0;
  uint64_t frame_id = 0;
  uint32_t status_code = 0;
  uint32_t detect_count = 0;
  DetectionRecord detections[kMaxDetectionsPerChannel]{};
};

struct NotifyMessage {
  MessageHeader header{ kProtocolMagic, kProtocolVersion,
                        static_cast<uint16_t>(MessageType::kBatchReady) };
  uint64_t batch_id = 0;
  uint64_t notify_timestamp_ns = 0;
  uint32_t channel_mask = 0;
  uint64_t frame_ids[kChannelCount]{};
};

struct BatchResultMessage {
  MessageHeader header{ kProtocolMagic, kProtocolVersion,
                        static_cast<uint16_t>(MessageType::kBatchResult) };
  uint64_t batch_id = 0;
  uint32_t channel_count = kChannelCount;
  uint32_t overall_status = 0;
  ChannelResultRecord channel_results[kChannelCount]{};
};

}  // namespace mt
