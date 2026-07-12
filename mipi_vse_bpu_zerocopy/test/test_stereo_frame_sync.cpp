#include "stereo_frame_sync.hpp"

#include <iostream>

namespace {
int failures = 0;
#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                          \
      std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: "           \
                << #condition << '\n';                                           \
      ++failures;                                                                \
    }                                                                            \
  } while (false)
}  // namespace

int main() {
  using zerocopy::StereoFrameSyncAction;

  const auto aligned = zerocopy::decideStereoFrameSync(
      1'354'527'098'604ULL, 1'354'527'141'020ULL, 1'000'000ULL);
  CHECK(aligned.action == StereoFrameSyncAction::kPair);
  CHECK(aligned.skew_ticks == 42'416ULL);

  const auto right_is_old = zerocopy::decideStereoFrameSync(
      1'354'594'121'604ULL, 1'354'560'430'645ULL, 1'000'000ULL);
  CHECK(right_is_old.action == StereoFrameSyncAction::kRefreshRight);
  CHECK(right_is_old.skew_ticks == 33'690'959ULL);

  const auto left_is_old = zerocopy::decideStereoFrameSync(
      1'354'560'430'645ULL, 1'354'594'121'604ULL, 1'000'000ULL);
  CHECK(left_is_old.action == StereoFrameSyncAction::kRefreshLeft);

  const auto missing_timestamp =
      zerocopy::decideStereoFrameSync(0, 1234, 1'000'000ULL);
  CHECK(missing_timestamp.action == StereoFrameSyncAction::kTimestampMissing);

  if (failures != 0) return 1;
  std::cout << "test_stereo_frame_sync: PASS\n";
  return 0;
}
