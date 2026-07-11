#include "channel_worker.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace zerocopy {
namespace {

using Clock = std::chrono::steady_clock;

double elapsedMs(const Clock::time_point& start,
                 const Clock::time_point& end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

}  // namespace

ChannelWorker::ChannelWorker(std::string channel,
                             std::unique_ptr<ChannelProcessor> processor,
                             AsyncLogger* logger,
                             FatalCallback fatal_callback)
    : channel_(std::move(channel)),
      processor_(std::move(processor)),
      logger_(logger),
      fatal_callback_(std::move(fatal_callback)) {
  if (channel_.empty() || processor_ == nullptr) {
    throw std::invalid_argument("channel and processor are required");
  }
}

ChannelWorker::~ChannelWorker() {
  close();
  join();
  shutdownProcessor();
}

bool ChannelWorker::initialize() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (initialized_) return true;
  std::string error;
  if (!processor_->initialize(error)) {
    last_error_ = error.empty() ? "processor initialization failed" : error;
    return false;
  }
  initialized_ = true;
  return true;
}

void ChannelWorker::start() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (!initialized_) throw std::logic_error("channel worker is not initialized");
  if (started_) throw std::logic_error("channel worker already started");
  started_ = true;
  thread_ = std::thread(&ChannelWorker::run, this);
}

QueuePushResult ChannelWorker::enqueue(VseFrame&& frame) {
  const uint64_t frame_id = frame.frameId();
  const auto result = queue_.push(std::move(frame));
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    stats_.queue_push_wait_ms += result.blocked.count() / 1000.0;
  }
  emit(LogLevel::kDebug, result.accepted ? "frame_enqueued" : "enqueue_closed",
       frame_id, result.accepted ? "frame moved to worker queue"
                                 : "worker queue is closed",
       {{"blocked_us", static_cast<int64_t>(result.blocked.count())},
        {"queue_depth", static_cast<uint64_t>(queue_.size())}});
  return result;
}

void ChannelWorker::close() noexcept {
  queue_.close();
  emit(LogLevel::kDebug, "queue_closed", 0, "worker queue closed");
}

void ChannelWorker::join() noexcept {
  if (thread_.joinable()) thread_.join();
}

int ChannelWorker::shutdownProcessor() noexcept {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (processor_shutdown_) return 0;
  processor_shutdown_ = true;
  return processor_->shutdown();
}

ChannelStats ChannelWorker::stats() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return stats_;
}

std::string ChannelWorker::lastError() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return last_error_;
}

const std::string& ChannelWorker::channel() const noexcept { return channel_; }

void ChannelWorker::run() noexcept {
  emit(LogLevel::kInfo, "worker_started", 0, "channel worker started");
  while (true) {
    VseFrame frame;
    const auto pop_result = queue_.pop(frame);
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      stats_.queue_pop_wait_ms += pop_result.blocked.count() / 1000.0;
    }
    if (!pop_result.has_frame) break;

    const uint64_t frame_id = frame.frameId();
    emit(LogLevel::kDebug, "frame_dequeued", frame_id,
         "worker owns VSE frame",
         {{"blocked_us", static_cast<int64_t>(pop_result.blocked.count())}});
    const auto start = Clock::now();
    ChannelProcessResult result;
    std::string error;
    bool success = false;
    try {
      success = processor_->process(frame, result, error);
    } catch (const std::exception& exception) {
      error = exception.what();
    } catch (...) {
      error = "unknown processor exception";
    }
    result.end_to_end_ms = elapsedMs(start, Clock::now());
    if (!success && error.empty()) error = "channel processing failed";

    const int release_result = frame.release();
    if (release_result != 0) {
      if (error.empty()) {
        error = "VSE frame release failed with code " +
                std::to_string(release_result);
      }
      success = false;
    }

    if (success) {
      {
        std::lock_guard<std::mutex> lock(state_mutex_);
        ++stats_.successes;
        stats_.detections += result.detections;
        if (result.cache_hit) {
          ++stats_.cache_hits;
        } else {
          ++stats_.cache_misses;
        }
        if (!result.address_match) ++stats_.address_mismatches;
        stats_.bpu_ms += result.bpu_ms;
        stats_.postprocess_ms += result.postprocess_ms;
        stats_.end_to_end_ms += result.end_to_end_ms;
      }
      emit(LogLevel::kInfo, "frame_processed", frame_id,
           "inference and postprocess completed",
           {{"address_match", result.address_match},
            {"bpu_ms", result.bpu_ms},
            {"cache_hit", result.cache_hit},
            {"detections", static_cast<uint64_t>(result.detections)},
            {"end_to_end_ms", result.end_to_end_ms},
            {"postprocess_ms", result.postprocess_ms}});
      continue;
    }

    if (release_result != 0) {
      std::lock_guard<std::mutex> lock(state_mutex_);
      ++stats_.release_errors;
    }
    recordFailure(frame_id, error);
    break;
  }
  emit(LogLevel::kInfo, "worker_stopped", 0, "channel worker stopped");
}

void ChannelWorker::recordFailure(uint64_t frame_id,
                                  const std::string& error) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    ++stats_.failures;
    if (last_error_.empty()) last_error_ = error;
  }
  emit(LogLevel::kError, "frame_failed", frame_id, error);
  queue_.close();
  if (fatal_callback_) fatal_callback_(channel_, frame_id, error);
}

void ChannelWorker::emit(LogLevel level, const std::string& event,
                         uint64_t frame_id, const std::string& message,
                         LogFields fields) noexcept {
  if (logger_ == nullptr) return;
  try {
    logger_->log({level, "worker", event, channel_ + "-worker", channel_,
                  frame_id == 0 ? -1 : static_cast<int64_t>(frame_id), message,
                  std::move(fields)});
  } catch (...) {
  }
}

}  // namespace zerocopy
