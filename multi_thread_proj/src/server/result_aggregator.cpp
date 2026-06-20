#include "result_aggregator.hpp"

namespace mt {

void ResultAggregator::beginBatch(uint64_t batch_id, uint32_t channel_mask) {
  auto& batch = batches_[batch_id];
  batch = BatchContext{};
  batch.channel_mask = channel_mask;
}

void ResultAggregator::submit(uint64_t batch_id, const ChannelResult& result) {
  auto& batch = batches_[batch_id];
  if (result.channel_id >= kChannelCount) {
    return;
  }
  batch.results[result.channel_id] = result;
  batch.completed[result.channel_id] = true;
}

bool ResultAggregator::tryBuildCompletedBatch(uint64_t batch_id,
                                              BatchResultMessage& message) {
  auto it = batches_.find(batch_id);
  if (it == batches_.end()) {
    return false;
  }

  for (size_t channel = 0; channel < kChannelCount; ++channel) {
    const uint32_t bit = (1U << channel);
    if ((it->second.channel_mask & bit) == 0) {
      continue;
    }
    if (!it->second.completed[channel]) {
      return false;
    }
  }

  message = BatchResultMessage{};
  message.batch_id = batch_id;
  message.channel_count = kChannelCount;
  message.overall_status = kStatusOk;
  for (size_t channel = 0; channel < kChannelCount; ++channel) {
    const ChannelResult& result = it->second.results[channel];
    ChannelResultRecord& record = message.channel_results[channel];
    record.channel_id = result.channel_id;
    record.frame_id = result.frame_id;
    record.status_code = result.status_code;
    record.detect_count = result.detect_count;
    for (size_t i = 0; i < kMaxDetectionsPerChannel; ++i) {
      record.detections[i] = result.detections[i];
    }
    if (result.status_code != kStatusOk) {
      message.overall_status = result.status_code;
    }
  }

  batches_.erase(it);
  return true;
}

}  // namespace mt
