#include "yolo11_postprocess.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <vector>

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

std::size_t offset(int width, int channels, int y, int x, int c) {
  return (static_cast<std::size_t>(y) * width + x) * channels + c;
}

void setDfl(std::vector<float>& values, int width, int y, int x, int bin) {
  for (int side = 0; side < 4; ++side) {
    values[offset(width, 64, y, x, side * 16 + bin)] = 20.0F;
  }
}
}  // namespace

int main() {
  std::vector<float> cls80(80 * 80 * 80, -20.0F);
  std::vector<float> reg80(80 * 80 * 64, -20.0F);
  std::vector<float> cls40(40 * 40 * 80, -20.0F);
  std::vector<float> reg40(40 * 40 * 64, -20.0F);
  std::vector<float> cls20(20 * 20 * 80, -20.0F);
  std::vector<float> reg20(20 * 20 * 64, -20.0F);

  cls80[offset(80, 80, 10, 20, 3)] = 8.0F;
  setDfl(reg80, 80, 10, 20, 2);
  cls80[offset(80, 80, 10, 21, 3)] = 7.0F;
  setDfl(reg80, 80, 10, 21, 2);

  const std::array<zerocopy::FloatTensorView, 6> outputs{{
      {cls80.data(), 80, 80, 80}, {reg80.data(), 80, 80, 64},
      {cls40.data(), 40, 40, 80}, {reg40.data(), 40, 40, 64},
      {cls20.data(), 20, 20, 80}, {reg20.data(), 20, 20, 64},
  }};

  zerocopy::Yolo11Postprocessor postprocessor(0.25F, 0.45F);
  zerocopy::PostprocessMetrics metrics;
  const auto detections = postprocessor.run(outputs, &metrics);
  CHECK(detections.size() == 1);
  CHECK(metrics.proposals == 2);
  CHECK(metrics.detections == 1);
  CHECK(metrics.class_scan_ms >= 0.0);
  CHECK(metrics.dfl_decode_ms >= 0.0);
  CHECK(metrics.map_ms >= 0.0);
  CHECK(metrics.nms_ms >= 0.0);
  CHECK(metrics.total_ms >= metrics.nms_ms);
  if (!detections.empty()) {
    const auto& d = detections.front();
    CHECK(d.class_id == 3);
    CHECK(d.score > 0.999F);
    CHECK(std::fabs(d.box.x1 - 251.6F) < 0.2F);
    CHECK(std::fabs(d.box.y1 - 211.6F) < 0.2F);
    CHECK(std::fabs(d.box.x2 - 306.0F) < 0.2F);
    CHECK(std::fabs(d.box.y2 - 266.0F) < 0.2F);
  }

  auto invalid = outputs;
  invalid[0].channels = 79;
  bool rejected = false;
  try {
    postprocessor.run(invalid);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  CHECK(rejected);

  if (failures != 0) return 1;
  std::cout << "test_yolo11_postprocess: PASS\n";
  return 0;
}
