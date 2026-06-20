#include <cstdint>
#include <vector>

#include "shared_memory_queue.hpp"

int main() {
  mt::SharedMemoryConfig config;
  config.create = true;
  config.path = "/mt_infer_shm_test_queue";

  mt::SharedMemoryQueue queue(config);
  if (!queue.initialize()) {
    return 1;
  }

  mt::FrameMetadata meta{};
  meta.batch_id = 7;
  meta.channel_id = 1;
  meta.frame_id = 11;
  meta.width = 2592;
  meta.height = 1944;
  meta.payload_size = mt::kNv12FrameBytes;

  std::vector<uint8_t> payload(mt::kNv12FrameBytes, 123);
  if (!queue.push(1, meta, payload.data(), payload.size())) {
    return 2;
  }

  mt::FrameView view{};
  if (!queue.acquireReady(1, 11, view)) {
    return 3;
  }

  if (!queue.release(view)) {
    return 4;
  }

  return 0;
}
