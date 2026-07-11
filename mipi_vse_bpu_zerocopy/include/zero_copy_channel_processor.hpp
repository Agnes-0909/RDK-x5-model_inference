#ifndef MIPI_VSE_BPU_ZEROCOPY_ZERO_COPY_CHANNEL_PROCESSOR_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_ZERO_COPY_CHANNEL_PROCESSOR_HPP_

#include "channel_worker.hpp"
#include "zero_copy_yolo_runner.hpp"

#include <memory>
#include <string>

namespace zerocopy {

std::unique_ptr<ChannelProcessor> makeZeroCopyChannelProcessor(
    std::string channel, ZeroCopyYoloRunner::Config config,
    AsyncLogger* logger);

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_ZERO_COPY_CHANNEL_PROCESSOR_HPP_
