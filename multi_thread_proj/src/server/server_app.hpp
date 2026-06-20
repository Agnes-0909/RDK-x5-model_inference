#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

#include "result_aggregator.hpp"
#include "shared_memory_queue.hpp"
#include "uds_socket.hpp"
#include "worker.hpp"

namespace mt {

struct ServerConfig {
  std::string model_path;
  std::string uds_path = "/tmp/mt_infer.sock";
  std::string shm_path = "/mt_infer_shm";
  uint32_t channel_mask = 0x7;
  int32_t bpu_core_id = -1;
};

bool ParseServerArgs(int argc, char** argv, ServerConfig& config);

class ServerApp {
 public:
 bool initialize(const ServerConfig& config);
  int run();

 private:
  bool waitForBatchResult(uint64_t batch_id, BatchResultMessage& result);

  ServerConfig config_{};
  SharedMemoryQueue queue_{SharedMemoryConfig{}};
  ResultAggregator aggregator_{};
  UdsSocket socket_{};
  std::array<Worker, kChannelCount> workers_{
      Worker(queue_, aggregator_), Worker(queue_, aggregator_),
      Worker(queue_, aggregator_)};
};

}  // namespace mt
