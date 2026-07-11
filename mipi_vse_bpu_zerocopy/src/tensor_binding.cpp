#include "zero_copy_yolo_runner.hpp"

#include <stdexcept>

namespace zerocopy {

InputBinding makeInputBinding(const ModelInputSpec& spec,
                              const ExternalMemory& memory) {
  constexpr ModelInputSpec expected{1, 3, 640, 640, 614400};
  if (spec.batch != expected.batch || spec.channels != expected.channels ||
      spec.height != expected.height || spec.width != expected.width ||
      spec.bytes != expected.bytes) {
    throw std::invalid_argument(
        "model input must be NCHW 1x3x640x640 NV12 with 614400 bytes");
  }
  if (memory.phy_addr == 0 || memory.virt_addr == nullptr) {
    throw std::invalid_argument("registered input addresses must be nonzero");
  }
  if (memory.bytes != expected.bytes || memory.total_bytes != expected.bytes) {
    throw std::invalid_argument(
        "registered input memory must contain exactly 614400 bytes");
  }
  return InputBinding{memory.phy_addr, memory.virt_addr, memory.bytes};
}

}  // namespace zerocopy
