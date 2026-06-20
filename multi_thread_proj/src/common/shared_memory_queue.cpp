#include "shared_memory_queue.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>

#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mt {

struct SharedMemoryQueue::SharedHeader {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t channel_count = 0;
  uint16_t queue_depth = 0;
  uint32_t frame_bytes = 0;
  pthread_mutex_t channel_mutexes[kChannelCount];
  uint32_t write_indices[kChannelCount];
};

struct SharedMemoryQueue::SharedSlotHeader {
  FrameMetadata metadata{};
};

SharedMemoryQueue::SharedMemoryQueue(SharedMemoryConfig config)
    : config_(std::move(config)) {}

SharedMemoryQueue::~SharedMemoryQueue() {
  if (mapped_ != nullptr && mapped_ != MAP_FAILED && mapped_bytes_ > 0) {
    munmap(mapped_, mapped_bytes_);
  }
  if (fd_ >= 0) {
    close(fd_);
  }
}

bool SharedMemoryQueue::initialize() {
  if (config_.path.empty()) {
    last_error_ = "shared memory path is empty";
    return false;
  }

  const int flags = config_.create ? (O_CREAT | O_RDWR) : O_RDWR;
  fd_ = shm_open(config_.path.c_str(), flags, 0666);
  if (fd_ < 0) {
    std::ostringstream oss;
    oss << "shm_open failed for '" << config_.path << "', errno=" << errno;
    last_error_ = oss.str();
    return false;
  }

  mapped_bytes_ = totalBytes();
  if (config_.create && ftruncate(fd_, static_cast<off_t>(mapped_bytes_)) != 0) {
    std::ostringstream oss;
    oss << "ftruncate failed for '" << config_.path << "', errno=" << errno;
    last_error_ = oss.str();
    return false;
  }

  mapped_ = mmap(nullptr, mapped_bytes_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_,
                 0);
  if (mapped_ == MAP_FAILED) {
    std::ostringstream oss;
    oss << "mmap failed for '" << config_.path << "', errno=" << errno;
    last_error_ = oss.str();
    mapped_ = nullptr;
    return false;
  }

  header_ = static_cast<SharedHeader*>(mapped_);
  if (config_.create) {
    if (!initializeLayout()) {
      return false;
    }
  } else if (header_->magic != kProtocolMagic ||
             header_->version != kProtocolVersion ||
             header_->channel_count != kChannelCount ||
             header_->queue_depth != kQueueDepth ||
             header_->frame_bytes != kNv12FrameBytes) {
    last_error_ = "shared memory layout does not match expected protocol";
    return false;
  }

  initialized_ = true;
  return true;
}

bool SharedMemoryQueue::push(uint32_t channel_id, const FrameMetadata& metadata,
                             const uint8_t* payload, size_t payload_size) {
  if (!initialized_ || channel_id >= kChannelCount || payload == nullptr ||
      payload_size > kNv12FrameBytes) {
    return false;
  }

  pthread_mutex_lock(&header_->channel_mutexes[channel_id]);
  const uint32_t start = header_->write_indices[channel_id] % kQueueDepth;
  size_t free_slot = kQueueDepth;
  for (size_t offset = 0; offset < kQueueDepth; ++offset) {
    const size_t slot_index = (start + offset) % kQueueDepth;
    SharedSlotHeader* slot = slotHeader(channel_id, slot_index);
    if (slot->metadata.status == SlotStatus::kEmpty) {
      free_slot = slot_index;
      break;
    }
  }
  if (free_slot == kQueueDepth) {
    pthread_mutex_unlock(&header_->channel_mutexes[channel_id]);
    return false;
  }

  SharedSlotHeader* slot = slotHeader(channel_id, free_slot);
  slot->metadata = metadata;
  slot->metadata.channel_id = channel_id;
  slot->metadata.payload_size = static_cast<uint32_t>(payload_size);
  slot->metadata.status = SlotStatus::kWriting;
  std::memcpy(slotPayload(channel_id, free_slot), payload, payload_size);
  slot->metadata.status = SlotStatus::kReady;
  header_->write_indices[channel_id] =
      static_cast<uint32_t>((free_slot + 1) % kQueueDepth);
  pthread_mutex_unlock(&header_->channel_mutexes[channel_id]);
  return true;
}

bool SharedMemoryQueue::acquireReady(uint32_t channel_id, uint64_t frame_id,
                                     FrameView& view) {
  if (!initialized_ || channel_id >= kChannelCount) {
    return false;
  }

  pthread_mutex_lock(&header_->channel_mutexes[channel_id]);
  for (size_t i = 0; i < kQueueDepth; ++i) {
    SharedSlotHeader* slot = slotHeader(channel_id, i);
    if (slot->metadata.status == SlotStatus::kReady &&
        slot->metadata.frame_id == frame_id) {
      slot->metadata.status = SlotStatus::kReading;
      view.metadata = slot->metadata;
      view.payload = slotPayload(channel_id, i);
      view.channel_index = channel_id;
      view.slot_index = i;
      pthread_mutex_unlock(&header_->channel_mutexes[channel_id]);
      return true;
    }
  }
  pthread_mutex_unlock(&header_->channel_mutexes[channel_id]);

  return false;
}

bool SharedMemoryQueue::release(const FrameView& view) {
  if (!initialized_ || view.channel_index >= kChannelCount ||
      view.slot_index >= kQueueDepth) {
    return false;
  }

  pthread_mutex_lock(&header_->channel_mutexes[view.channel_index]);
  SharedSlotHeader* slot = slotHeader(view.channel_index, view.slot_index);
  slot->metadata = FrameMetadata{};
  slot->metadata.status = SlotStatus::kEmpty;
  pthread_mutex_unlock(&header_->channel_mutexes[view.channel_index]);
  return true;
}

const std::string& SharedMemoryQueue::lastError() const { return last_error_; }

size_t SharedMemoryQueue::totalBytes() const {
  const size_t slot_stride =
      sizeof(SharedSlotHeader) + static_cast<size_t>(kNv12FrameBytes);
  return sizeof(SharedHeader) +
         static_cast<size_t>(kChannelCount) * kQueueDepth * slot_stride;
}

SharedMemoryQueue::SharedSlotHeader* SharedMemoryQueue::slotHeader(
    uint32_t channel_id, size_t slot_index) const {
  uint8_t* base = static_cast<uint8_t*>(mapped_) + sizeof(SharedHeader);
  const size_t slot_stride =
      sizeof(SharedSlotHeader) + static_cast<size_t>(kNv12FrameBytes);
  return reinterpret_cast<SharedSlotHeader*>(
      base + (static_cast<size_t>(channel_id) * kQueueDepth + slot_index) *
                 slot_stride);
}

uint8_t* SharedMemoryQueue::slotPayload(uint32_t channel_id,
                                        size_t slot_index) const {
  uint8_t* header_ptr =
      reinterpret_cast<uint8_t*>(slotHeader(channel_id, slot_index));
  return header_ptr + sizeof(SharedSlotHeader);
}

bool SharedMemoryQueue::initializeLayout() {
  std::memset(mapped_, 0, mapped_bytes_);
  header_->magic = kProtocolMagic;
  header_->version = kProtocolVersion;
  header_->channel_count = kChannelCount;
  header_->queue_depth = kQueueDepth;
  header_->frame_bytes = kNv12FrameBytes;

  pthread_mutexattr_t attr;
  if (pthread_mutexattr_init(&attr) != 0) {
    last_error_ = "pthread_mutexattr_init failed";
    return false;
  }
  if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
    pthread_mutexattr_destroy(&attr);
    last_error_ = "pthread_mutexattr_setpshared failed";
    return false;
  }

  for (size_t channel = 0; channel < kChannelCount; ++channel) {
    if (pthread_mutex_init(&header_->channel_mutexes[channel], &attr) != 0) {
      pthread_mutexattr_destroy(&attr);
      last_error_ = "pthread_mutex_init failed";
      return false;
    }
    header_->write_indices[channel] = 0;
    for (size_t slot = 0; slot < kQueueDepth; ++slot) {
      SharedSlotHeader* slot_header = slotHeader(channel, slot);
      slot_header->metadata = FrameMetadata{};
      slot_header->metadata.status = SlotStatus::kEmpty;
    }
  }

  pthread_mutexattr_destroy(&attr);
  return true;
}

}  // namespace mt
