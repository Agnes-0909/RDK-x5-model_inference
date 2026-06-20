#include <cstdint>

#include "protocol.hpp"
#include "result_aggregator.hpp"
#include "server_types.hpp"

int main() {
  mt::ResultAggregator aggregator;
  aggregator.beginBatch(42, 0x5);

  for (uint32_t channel : {0U, 2U}) {
    mt::ChannelResult result{};
    result.channel_id = channel;
    result.frame_id = 100 + channel;
    result.status_code = mt::kStatusOk;
    aggregator.submit(42, result);
  }

  mt::BatchResultMessage message{};
  if (!aggregator.tryBuildCompletedBatch(42, message)) {
    return 1;
  }

  if (message.channel_results[0].frame_id != 100 ||
      message.channel_results[2].frame_id != 102) {
    return 2;
  }

  if (message.channel_results[1].frame_id != 0) {
    return 3;
  }

  return 0;
}
