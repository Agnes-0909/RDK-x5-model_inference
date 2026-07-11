#include "registered_buffer_cache.hpp"
#include "vse_frame.hpp"

#include <iostream>
#include <stdexcept>

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

zerocopy::BufferLayout validLayout() {
  zerocopy::BufferLayout layout;
  layout.width = layout.height = layout.stride_y = layout.stride_uv = 640;
  layout.y_bytes = 409600;
  layout.uv_bytes = 204800;
  layout.y_phy = 0x100000;
  layout.uv_phy = 0x164000;
  layout.y_virt = reinterpret_cast<void*>(0x200000);
  layout.uv_virt = reinterpret_cast<void*>(0x264000);
  layout.plane_count = 2;
  layout.format = zerocopy::PixelFormat::Nv12;
  return layout;
}
}  // namespace

int main() {
  int registrations = 0;
  int unregistrations = 0;
  zerocopy::RegisteredBufferCache cache(
      [&](zerocopy::ExternalMemory&) {
        ++registrations;
        return 0;
      },
      [&](zerocopy::ExternalMemory&) {
        ++unregistrations;
        return 0;
      });
  const auto first = cache.getOrRegister(validLayout());
  const auto second = cache.getOrRegister(validLayout());
  CHECK(!first.cache_hit);
  CHECK(second.cache_hit);
  CHECK(first.memory.phy_addr == second.memory.phy_addr);
  CHECK(registrations == 1);
  CHECK(cache.size() == 1);

  auto changed = validLayout();
  changed.y_virt = reinterpret_cast<void*>(0x300000);
  bool rejected = false;
  try {
    cache.getOrRegister(changed);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  CHECK(rejected);
  CHECK(cache.clear() == 0);
  CHECK(unregistrations == 1);
  CHECK(cache.size() == 0);

  int remap_registrations = 0;
  zerocopy::RegisteredBufferCache remapped_cache(
      [&](zerocopy::ExternalMemory& memory) {
        ++remap_registrations;
        memory.virt_addr = reinterpret_cast<void*>(0x900000);
        return 0;
      },
      [](zerocopy::ExternalMemory&) { return 0; });
  const auto remapped_first = remapped_cache.getOrRegister(validLayout());
  const auto remapped_hit = remapped_cache.getOrRegister(validLayout());
  CHECK(remapped_first.memory.virt_addr == reinterpret_cast<void*>(0x900000));
  CHECK(remapped_hit.cache_hit);
  CHECK(remapped_hit.memory.virt_addr == reinterpret_cast<void*>(0x900000));
  CHECK(remap_registrations == 1);

  int releases = 0;
  {
    zerocopy::VseFrame frame(7, validLayout(), [&] {
      ++releases;
      return 0;
    });
    zerocopy::VseFrame moved(std::move(frame));
    CHECK(!frame.valid());
    CHECK(moved.valid());
    CHECK(moved.frameId() == 7);
    CHECK(moved.release() == 0);
    CHECK(moved.release() == 0);
  }
  CHECK(releases == 1);

  if (failures != 0) return 1;
  std::cout << "test_resource_lifecycle: PASS\n";
  return 0;
}
