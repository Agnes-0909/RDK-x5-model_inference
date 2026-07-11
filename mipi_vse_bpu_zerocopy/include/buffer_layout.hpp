#ifndef MIPI_VSE_BPU_ZEROCOPY_BUFFER_LAYOUT_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_BUFFER_LAYOUT_HPP_

#include <cstdint>

namespace zerocopy {

enum class PixelFormat { Unknown, Nv12 };

struct BufferLayout {
  int width = 0;
  int height = 0;
  int stride_y = 0;
  int stride_uv = 0;
  std::uint32_t y_bytes = 0;
  std::uint32_t uv_bytes = 0;
  std::uint64_t y_phy = 0;
  std::uint64_t uv_phy = 0;
  void* y_virt = nullptr;
  void* uv_virt = nullptr;
  int plane_count = 0;
  PixelFormat format = PixelFormat::Unknown;
};

struct ExternalMemory {
  std::uint64_t phy_addr = 0;
  void* virt_addr = nullptr;
  std::uint32_t bytes = 0;
  std::uint32_t total_bytes = 0;
};

ExternalMemory validateZeroCopyLayout(const BufferLayout& layout);

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_BUFFER_LAYOUT_HPP_
