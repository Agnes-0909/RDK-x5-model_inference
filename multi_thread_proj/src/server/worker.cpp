#include "worker.hpp"

#include <sstream>

#include "logger.hpp"
#include "inference_trace.hpp"

namespace mt {

Worker::Worker(SharedMemoryQueue& queue, ResultAggregator& aggregator)
    : queue_(queue), aggregator_(aggregator) {}

Worker::~Worker() { stop(); }

bool Worker::initialize(const WorkerConfig& config) {
  config_ = config;
  rdk::ModelInferenceBase::Config infer_config;
  infer_config.model_file = config.model_path;
  infer_config.bpu_core_id = config.bpu_core_id;
  model_ = std::make_unique<rdk::YoloInfer>(infer_config);
  if (!model_->initialize()) {
    last_error_ = model_->lastError();
    return false;
  }
  {
    std::ostringstream oss;
    oss << "started channel=" << config_.channel_id
        << " model=" << config_.model_path
        << " bpu_core_id=" << config_.bpu_core_id;
    MT_LOG_INFO(oss.str());
  }
  running_ = true;
  thread_ = std::thread(&Worker::runLoop, this);
  return true;
}

void Worker::enqueue(const WorkItem& item) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_items_.push_back(item);
  }
  cv_.notify_one();
}

void Worker::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
  }
  cv_.notify_one();
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool Worker::processOne(const WorkItem& item) {
  const auto start = std::chrono::steady_clock::now();
  {
    std::ostringstream oss;
    oss << "processing batch=" << item.batch_id
        << " channel=" << item.channel_id
        << " frame_id=" << item.frame_id;
    MT_LOG_INFO(oss.str());
  }

  FrameView view{};
  const auto acquire_start = std::chrono::steady_clock::now();
  if (!queue_.acquireReady(item.channel_id, item.frame_id, view)) {
    last_error_ = "failed to acquire frame";
    std::ostringstream oss;
    oss << "acquire failed batch=" << item.batch_id
        << " channel=" << item.channel_id
        << " frame_id=" << item.frame_id;
    MT_LOG_ERROR(oss.str());
    return false;
  }
  const auto acquire_end = std::chrono::steady_clock::now();
  {
    std::ostringstream oss;
    oss << "acquired batch=" << item.batch_id
        << " channel=" << item.channel_id
        << " frame_id=" << item.frame_id
        << " acquire_ms=" << elapsedMs(acquire_start, acquire_end);
    MT_LOG_INFO(oss.str());
  }

  rdk::YoloInfer::Nv12Frame frame;
  frame.data = view.payload;
  frame.width = static_cast<int>(view.metadata.width);
  frame.height = static_cast<int>(view.metadata.height);
  frame.stride_y = static_cast<int>(view.metadata.width);
  frame.stride_uv = static_cast<int>(view.metadata.width);
  frame.crop_x = static_cast<int>(view.metadata.crop_x);
  frame.crop_y = static_cast<int>(view.metadata.crop_y);
  frame.crop_width = static_cast<int>(view.metadata.crop_width);
  frame.crop_height = static_cast<int>(view.metadata.crop_height);
  frame.source_width = static_cast<int>(view.metadata.source_width);
  frame.source_height = static_cast<int>(view.metadata.source_height);
  frame.letterbox_pad_x = static_cast<int>(view.metadata.letterbox_pad_x);
  frame.letterbox_pad_y = static_cast<int>(view.metadata.letterbox_pad_y);
  frame.letterbox_scale = view.metadata.letterbox_scale;
  frame.preprocessed = view.metadata.preprocessed != 0;

  ChannelResult result{};
  result.channel_id = item.channel_id;
  result.frame_id = item.frame_id;
  rdk::SetInferenceTraceContext(rdk::InferenceTraceContext{
      item.batch_id, item.channel_id, item.frame_id, true});
  if (!model_->infer(frame)) {
    rdk::ClearInferenceTraceContext();
    result.status_code = kStatusInferFailed;
    std::ostringstream oss;
    oss << "infer failed batch=" << item.batch_id
        << " channel=" << item.channel_id
        << " frame_id=" << item.frame_id
        << " error=" << model_->lastError();
    MT_LOG_ERROR(oss.str());
  } else {
    const auto& detections = model_->detections();
    result.detect_count = static_cast<uint32_t>(
        std::min<size_t>(detections.size(), kMaxDetectionsPerChannel));
    if (detections.size() > kMaxDetectionsPerChannel) {
      result.status_code = kStatusTruncated;
    }
    for (size_t i = 0; i < result.detect_count; ++i) {
      result.detections[i].class_id = detections[i].class_id;
      result.detections[i].score = detections[i].score;
      result.detections[i].x1 = detections[i].box.x;
      result.detections[i].y1 = detections[i].box.y;
      result.detections[i].x2 = detections[i].box.x + detections[i].box.width;
      result.detections[i].y2 = detections[i].box.y + detections[i].box.height;
    }
    {
      const auto end = std::chrono::steady_clock::now();
      std::ostringstream oss;
      oss << "infer done batch=" << item.batch_id
          << " channel=" << item.channel_id
          << " frame_id=" << item.frame_id
          << " detect_count=" << result.detect_count
          << " status=" << result.status_code
          << " latency_ms=" << elapsedMs(start, end)
          << " notify_to_enqueue_ms=" << elapsedMs(item.notify_time, item.enqueue_time)
          << " enqueue_to_start_ms=" << elapsedMs(item.enqueue_time, start)
          << " notify_to_start_ms=" << elapsedMs(item.notify_time, start)
          << " notify_to_done_ms=" << elapsedMs(item.notify_time, end);
      MT_LOG_INFO(oss.str());
    }
  }
  rdk::ClearInferenceTraceContext();

  aggregator_.submit(item.batch_id, result);
  queue_.release(view);
  {
    std::ostringstream oss;
    oss << "released batch=" << item.batch_id
        << " channel=" << item.channel_id
        << " frame_id=" << item.frame_id;
    MT_LOG_INFO(oss.str());
  }
  return true;
}

const std::string& Worker::lastError() const { return last_error_; }

void Worker::runLoop() {
  while (true) {
    WorkItem item{};
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [&] { return !queue_items_.empty() || !running_; });
      if (!running_ && queue_items_.empty()) {
        return;
      }
      item = queue_items_.front();
      queue_items_.pop_front();
    }
    processOne(item);
  }
}

}  // namespace mt
