#include "buffer_layout.hpp"
#include "detection_mapper.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

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

template <typename Fn>
void checkInvalid(Fn&& fn, const std::string& text) {
  try {
    fn();
    ++failures;
  } catch (const std::invalid_argument& error) {
    CHECK(std::string(error.what()).find(text) != std::string::npos);
  }
}
}  // namespace

int main() {
  const zerocopy::Box mapped =
      zerocopy::mapModelBoxToSensor({0.0F, 0.0F, 640.0F, 640.0F});
  CHECK(std::fabs(mapped.x1 - 0.0F) < 1e-4F);
  CHECK(std::fabs(mapped.y1 - 96.0F) < 1e-4F);
  CHECK(std::fabs(mapped.x2 - 1088.0F) < 1e-4F);
  CHECK(std::fabs(mapped.y2 - 1184.0F) < 1e-4F);

  const zerocopy::Box clipped =
      zerocopy::mapModelBoxToSensor({-10.0F, -100.0F, 800.0F, 800.0F});
  CHECK(clipped.x1 == 0.0F && clipped.y1 == 0.0F);
  CHECK(clipped.x2 == 1088.0F && clipped.y2 == 1280.0F);

  zerocopy::BufferLayout layout;
  layout.width = 640;
  layout.height = 640;
  layout.stride_y = 640;
  layout.stride_uv = 640;
  layout.y_bytes = 409600;
  layout.uv_bytes = 204800;
  layout.y_phy = 0x100000;
  layout.uv_phy = 0x164000;
  layout.y_virt = reinterpret_cast<void*>(0x200000);
  layout.uv_virt = reinterpret_cast<void*>(0x264000);
  layout.plane_count = 2;
  layout.format = zerocopy::PixelFormat::Nv12;

  const auto validated = zerocopy::validateZeroCopyLayout(layout);
  CHECK(validated.total_bytes == 614400);
  CHECK(validated.phy_addr == layout.y_phy);
  CHECK(validated.virt_addr == layout.y_virt);

  auto broken = layout;
  broken.uv_phy = 0x170000;
  checkInvalid([&] { zerocopy::validateZeroCopyLayout(broken); },
               "UV physical address is not contiguous with Y");
  broken = layout;
  broken.stride_y = 672;
  checkInvalid([&] { zerocopy::validateZeroCopyLayout(broken); }, "stride");
  broken = layout;
  broken.format = zerocopy::PixelFormat::Unknown;
  checkInvalid([&] { zerocopy::validateZeroCopyLayout(broken); }, "NV12");

  if (failures != 0) return 1;
  std::cout << "test_geometry_and_layout: PASS\n";
  return 0;
}
