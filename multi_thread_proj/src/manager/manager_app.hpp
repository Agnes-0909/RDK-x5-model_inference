#pragma once

#include <array>
#include <string>
#include <vector>

#include "image_codec.hpp"
#include "protocol.hpp"
#include "shared_memory_queue.hpp"
#include "uds_socket.hpp"

namespace mt {

struct ManagerConfig {
  std::string input_dir_0;
  std::string input_dir_1;
  std::string input_dir_2;
  std::string uds_path = "/tmp/mt_infer.sock";
  std::string uds_path_secondary;
  std::string shm_path = "/mt_infer_shm";
};

bool ParseManagerArgs(int argc, char** argv, ManagerConfig& config);

class ManagerApp {
 public:
  bool initialize(const ManagerConfig& config);
  int run();

 private:
  struct ServerRoute {
    uint32_t channel_mask = 0;
    UdsSocket* socket = nullptr;
  };

  struct PreparedInput {
    std::string source_path;
    std::vector<uint8_t> nv12_buffer;
    ModelInputInfo model_input_info{};
  };

  static bool mergeBatchResult(const std::vector<BatchResultMessage>& partials,
                               uint64_t batch_id,
                               BatchResultMessage& merged_result);

  bool collectInputs(std::array<std::vector<std::string>, kChannelCount>& files);
  bool prepareInputs(
      const std::array<std::vector<std::string>, kChannelCount>& files,
      std::array<std::vector<PreparedInput>, kChannelCount>& prepared_inputs);

  ManagerConfig config_;
  SharedMemoryQueue queue_{SharedMemoryConfig{}};
  UdsSocket socket_;
  UdsSocket secondary_socket_;
};

}  // namespace mt
