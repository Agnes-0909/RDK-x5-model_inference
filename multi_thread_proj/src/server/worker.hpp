#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "result_aggregator.hpp"
#include "shared_memory_queue.hpp"
#include "model_inference_base.hpp"
#include "yolo_infer.hpp"

namespace mt {

struct WorkerConfig {
  uint32_t channel_id = 0;
  std::string model_path;
  int32_t bpu_core_id = -1;
};

struct WorkItem {
  uint64_t batch_id = 0;
  uint32_t channel_id = 0;
  uint64_t frame_id = 0;
  std::chrono::steady_clock::time_point notify_time{};
  std::chrono::steady_clock::time_point enqueue_time{};
};

class Worker {
 public:
  Worker(SharedMemoryQueue& queue, ResultAggregator& aggregator);
  ~Worker();

  bool initialize(const WorkerConfig& config);
  void enqueue(const WorkItem& item);
  void stop();
  const std::string& lastError() const;

 private:
  void runLoop();
  bool processOne(const WorkItem& item);

  SharedMemoryQueue& queue_;
  ResultAggregator& aggregator_;
  WorkerConfig config_{};
  std::unique_ptr<rdk::YoloInfer> model_;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<WorkItem> queue_items_;
  bool running_ = false;
  std::string last_error_;
};

}  // namespace mt
