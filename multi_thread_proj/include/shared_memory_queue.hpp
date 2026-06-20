#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "protocol.hpp"

namespace mt {

struct SharedMemoryConfig {
  bool create = false;
  std::string path;
};

struct FrameMetadata {
  uint64_t batch_id = 0;
  uint32_t channel_id = 0;
  uint64_t frame_id = 0;
  uint32_t width = kFixedWidth;
  uint32_t height = kFixedHeight;
  uint32_t payload_size = 0;
  uint32_t crop_x = 0;
  uint32_t crop_y = 0;
  uint32_t crop_width = kFixedWidth;
  uint32_t crop_height = kFixedHeight;
  uint32_t source_width = kFixedWidth;
  uint32_t source_height = kFixedHeight;
  uint32_t letterbox_pad_x = 0;
  uint32_t letterbox_pad_y = 0;
  float letterbox_scale = 1.0F;
  uint32_t preprocessed = 0;
  uint64_t timestamp_ns = 0;
  SlotStatus status = SlotStatus::kEmpty;
};

struct FrameView {
  FrameMetadata metadata{};
  const uint8_t* payload = nullptr;

 private:
  friend class SharedMemoryQueue;
  uint32_t channel_index = 0;
  size_t slot_index = 0;
};

class SharedMemoryQueue {
 public:
  explicit SharedMemoryQueue(SharedMemoryConfig config);
  ~SharedMemoryQueue();

  bool initialize();
  bool push(uint32_t channel_id, const FrameMetadata& metadata,
            const uint8_t* payload, size_t payload_size);
  bool acquireReady(uint32_t channel_id, uint64_t frame_id, FrameView& view);
  bool release(const FrameView& view);
  const std::string& lastError() const;

 private:
  struct SharedHeader;
  struct SharedSlotHeader;

  size_t totalBytes() const;
  SharedSlotHeader* slotHeader(uint32_t channel_id, size_t slot_index) const;
  uint8_t* slotPayload(uint32_t channel_id, size_t slot_index) const;
  bool initializeLayout();

  SharedMemoryConfig config_;
  bool initialized_ = false;
  int fd_ = -1;
  void* mapped_ = nullptr;
  size_t mapped_bytes_ = 0;
  SharedHeader* header_ = nullptr;
  mutable std::string last_error_;
};

}  // namespace mt
