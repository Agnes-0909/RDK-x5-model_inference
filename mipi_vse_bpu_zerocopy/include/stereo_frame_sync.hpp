#ifndef MIPI_VSE_BPU_ZEROCOPY_STEREO_FRAME_SYNC_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_STEREO_FRAME_SYNC_HPP_

#include <cstdint>

namespace zerocopy {

enum class StereoFrameSyncAction {
  kPair,
  kRefreshLeft,
  kRefreshRight,
  kTimestampMissing,
};

struct StereoFrameSyncDecision {
  StereoFrameSyncAction action = StereoFrameSyncAction::kTimestampMissing;
  std::uint64_t skew_ticks = 0;
};

StereoFrameSyncDecision decideStereoFrameSync(
    std::uint64_t left_timestamp, std::uint64_t right_timestamp,
    std::uint64_t max_pair_skew_ticks) noexcept;

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_STEREO_FRAME_SYNC_HPP_
