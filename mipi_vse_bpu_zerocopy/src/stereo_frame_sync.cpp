#include "stereo_frame_sync.hpp"

namespace zerocopy {

StereoFrameSyncDecision decideStereoFrameSync(
    std::uint64_t left_timestamp, std::uint64_t right_timestamp,
    std::uint64_t max_pair_skew_ticks) noexcept {
  if (left_timestamp == 0 || right_timestamp == 0) {
    return {StereoFrameSyncAction::kTimestampMissing, 0};
  }
  const std::uint64_t skew = left_timestamp > right_timestamp
                                 ? left_timestamp - right_timestamp
                                 : right_timestamp - left_timestamp;
  if (skew <= max_pair_skew_ticks) {
    return {StereoFrameSyncAction::kPair, skew};
  }
  return {left_timestamp < right_timestamp
              ? StereoFrameSyncAction::kRefreshLeft
              : StereoFrameSyncAction::kRefreshRight,
          skew};
}

}  // namespace zerocopy
