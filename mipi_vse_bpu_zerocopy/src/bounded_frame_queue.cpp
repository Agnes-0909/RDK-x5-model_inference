#include "bounded_frame_queue.hpp"

#include <utility>

namespace zerocopy {
namespace {

std::chrono::microseconds elapsed(
    const std::chrono::steady_clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);
}

}  // namespace

BoundedFrameQueue::~BoundedFrameQueue() { close(); }

QueuePushResult BoundedFrameQueue::push(VseFrame&& frame) {
  const auto start = std::chrono::steady_clock::now();
  std::unique_lock<std::mutex> lock(mutex_);
  not_full_.wait(lock, [this] { return !frame_.has_value() || closed_; });
  QueuePushResult result;
  result.blocked = elapsed(start);
  if (closed_) return result;
  frame_.emplace(std::move(frame));
  result.accepted = true;
  not_empty_.notify_one();
  return result;
}

QueuePopResult BoundedFrameQueue::pop(VseFrame& frame) {
  const auto start = std::chrono::steady_clock::now();
  std::unique_lock<std::mutex> lock(mutex_);
  not_empty_.wait(lock, [this] { return frame_.has_value() || closed_; });
  QueuePopResult result;
  result.blocked = elapsed(start);
  if (!frame_.has_value()) return result;
  frame = std::move(*frame_);
  frame_.reset();
  result.has_frame = true;
  not_full_.notify_one();
  return result;
}

void BoundedFrameQueue::close() noexcept {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
  }
  not_empty_.notify_all();
  not_full_.notify_all();
}

std::size_t BoundedFrameQueue::size() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return frame_.has_value() ? 1U : 0U;
}

bool BoundedFrameQueue::closed() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return closed_;
}

}  // namespace zerocopy
