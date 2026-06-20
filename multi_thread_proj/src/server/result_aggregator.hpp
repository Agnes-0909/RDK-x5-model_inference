#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>

#include "protocol.hpp"
#include "server_types.hpp"

namespace mt {

class ResultAggregator {
 public:
  void beginBatch(uint64_t batch_id, uint32_t channel_mask);
  void submit(uint64_t batch_id, const ChannelResult& result);
  bool tryBuildCompletedBatch(uint64_t batch_id, BatchResultMessage& message);

 private:
  struct BatchContext {
    uint32_t channel_mask = 0;
    std::array<bool, kChannelCount> completed{};
    std::array<ChannelResult, kChannelCount> results{};
  };

  std::unordered_map<uint64_t, BatchContext> batches_;
};

}  // namespace mt
