#include "server_app.hpp"

#include <chrono>
#include <cstdint>
#include <sstream>
#include <thread>

#include "logger.hpp"

namespace mt {

bool ParseServerArgs(int argc, char** argv, ServerConfig& config) {
  for (int i = 1; i < argc; ++i) {
    const std::string key = argv[i];
    auto requireValue = [&](const std::string& name) -> const char* {
      if (i + 1 >= argc) {
        MT_LOG_ERROR(std::string("Missing value for ") + name);
        return nullptr;
      }
      return argv[++i];
    };

    if (key == "--model") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      config.model_path = value;
    } else if (key == "--uds-path") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      config.uds_path = value;
    } else if (key == "--shm-path") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      config.shm_path = value;
    } else if (key == "--channel-mask") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      config.channel_mask =
          static_cast<uint32_t>(std::strtoul(value, nullptr, 0));
    } else if (key == "--bpu-core-id") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      config.bpu_core_id = static_cast<int32_t>(std::strtol(value, nullptr, 0));
    } else {
      MT_LOG_ERROR(std::string("Unknown argument: ") + key);
      return false;
    }
  }
  return !config.model_path.empty() && config.channel_mask != 0;
}

bool ServerApp::initialize(const ServerConfig& config) {
  config_ = config;
  queue_ = SharedMemoryQueue(SharedMemoryConfig{true, config_.shm_path});
  if (!queue_.initialize()) {
    MT_LOG_ERROR(queue_.lastError());
    return false;
  }
  MT_LOG_INFO(std::string("shared memory initialized shm_path=") +
              config_.shm_path);
  if (!socket_.bindAndListen(config_.uds_path)) {
    MT_LOG_ERROR(socket_.lastError());
    return false;
  }
  MT_LOG_INFO(std::string("listening uds_path=") + config_.uds_path);
  if (!socket_.acceptOne()) {
    MT_LOG_ERROR(socket_.lastError());
    return false;
  }
  MT_LOG_INFO("manager connected");
  for (uint32_t channel = 0; channel < kChannelCount; ++channel) {
    if ((config_.channel_mask & (1U << channel)) == 0) {
      continue;
    }
    if (!workers_[channel].initialize(
            WorkerConfig{channel, config_.model_path, config_.bpu_core_id})) {
      MT_LOG_ERROR(workers_[channel].lastError());
      return false;
    }
    std::ostringstream oss;
    oss << "worker initialized channel=" << channel
        << " model=" << config_.model_path
        << " bpu_core_id=" << config_.bpu_core_id;
    MT_LOG_INFO(oss.str());
  }
  return true;
}

int ServerApp::run() {
  while (true) {
    const auto batch_start = std::chrono::steady_clock::now();
    NotifyMessage notify{};
    if (!socket_.recvAll(&notify, sizeof(notify))) {
      MT_LOG_WARN("manager disconnected, stopping workers");
      for (auto& worker : workers_) {
        worker.stop();
      }
      return 0;
    }

    {
      std::ostringstream oss;
      oss << "received batch=" << notify.batch_id
          << " channel_mask=0x" << std::hex << notify.channel_mask
          << std::dec;
      MT_LOG_INFO(oss.str());
    }

    const uint32_t effective_mask = notify.channel_mask & config_.channel_mask;
    const auto notify_time = std::chrono::steady_clock::time_point(
        std::chrono::nanoseconds(notify.notify_timestamp_ns));
    aggregator_.beginBatch(notify.batch_id, effective_mask);
    for (uint32_t channel = 0; channel < kChannelCount; ++channel) {
      if ((effective_mask & (1U << channel)) == 0) {
        continue;
      }
      WorkItem item{};
      item.batch_id = notify.batch_id;
      item.channel_id = channel;
      item.frame_id = notify.frame_ids[channel];
      item.notify_time = notify_time;
      item.enqueue_time = std::chrono::steady_clock::now();
      {
        std::ostringstream oss;
        oss << "dispatch batch=" << item.batch_id
            << " channel=" << item.channel_id
            << " frame_id=" << item.frame_id;
        MT_LOG_INFO(oss.str());
      }
      workers_[channel].enqueue(item);
    }

    BatchResultMessage result{};
    if (!waitForBatchResult(notify.batch_id, result)) {
      for (auto& worker : workers_) {
        worker.stop();
      }
      return 1;
    }
    {
      const auto batch_end = std::chrono::steady_clock::now();
      std::ostringstream oss;
      oss << "aggregated batch=" << result.batch_id
          << " overall_status=" << result.overall_status
          << " latency_ms=" << elapsedMs(batch_start, batch_end);
      MT_LOG_INFO(oss.str());
    }
    if (!socket_.sendAll(&result, sizeof(result))) {
      for (auto& worker : workers_) {
        worker.stop();
      }
      return 1;
    }
    {
      std::ostringstream oss;
      oss << "sent result batch=" << result.batch_id;
      MT_LOG_INFO(oss.str());
    }
  }
}

bool ServerApp::waitForBatchResult(uint64_t batch_id, BatchResultMessage& result) {
  for (;;) {
    if (aggregator_.tryBuildCompletedBatch(batch_id, result)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

}  // namespace mt
