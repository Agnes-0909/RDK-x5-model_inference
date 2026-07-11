#include "buffer_layout.hpp"

#include <stdexcept>

namespace zerocopy {

ExternalMemory validateZeroCopyLayout(const BufferLayout& layout) {
  constexpr int kWidth = 640;
  constexpr int kHeight = 640;
  constexpr std::uint32_t kYBytes = 640U * 640U;
  constexpr std::uint32_t kUvBytes = kYBytes / 2U;
  constexpr std::uint32_t kTotalBytes = kYBytes + kUvBytes;

  if (layout.format != PixelFormat::Nv12) {
    throw std::invalid_argument("VSE output must be NV12");
  }
  if (layout.plane_count != 2) {
    throw std::invalid_argument("NV12 VSE output must have exactly two planes");
  }
  if (layout.width != kWidth || layout.height != kHeight) {
    throw std::invalid_argument("VSE output dimensions must be 640x640");
  }
  if (layout.stride_y != kWidth || layout.stride_uv != kWidth) {
    throw std::invalid_argument("VSE Y and UV stride must both equal 640");
  }
  if (layout.y_bytes != kYBytes || layout.uv_bytes != kUvBytes) {
    throw std::invalid_argument("VSE plane byte sizes must be 409600 and 204800");
  }
  if (layout.y_phy == 0 || layout.uv_phy == 0 || layout.y_virt == nullptr ||
      layout.uv_virt == nullptr) {
    throw std::invalid_argument("VSE physical and virtual addresses must be nonzero");
  }
  if (layout.uv_phy != layout.y_phy + kYBytes) {
    throw std::invalid_argument(
        "UV physical address is not contiguous with Y");
  }

  ExternalMemory memory;
  memory.phy_addr = layout.y_phy;
  memory.virt_addr = layout.y_virt;
  memory.bytes = kTotalBytes;
  memory.total_bytes = kTotalBytes;
  return memory;
}

}  // namespace zerocopy
