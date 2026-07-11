#ifndef MIPI_VSE_BPU_ZEROCOPY_BOUNDED_FRAME_QUEUE_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_BOUNDED_FRAME_QUEUE_HPP_

#include "vse_frame.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>

namespace zerocopy {

struct QueuePushResult {
  bool accepted = false;
  std::chrono::microseconds blocked{0};
};

struct QueuePopResult {
  bool has_frame = false;
  std::chrono::microseconds blocked{0};
};

class BoundedFrameQueue {
 public:
  BoundedFrameQueue() = default;
  ~BoundedFrameQueue();

  BoundedFrameQueue(const BoundedFrameQueue&) = delete;
  BoundedFrameQueue& operator=(const BoundedFrameQueue&) = delete;

  QueuePushResult push(VseFrame&& frame);
  QueuePopResult pop(VseFrame& frame);
  void close() noexcept;
  std::size_t size() const noexcept;
  bool closed() const noexcept;

 private:
  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  std::optional<VseFrame> frame_;
  bool closed_ = false;
};

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_BOUNDED_FRAME_QUEUE_HPP_
