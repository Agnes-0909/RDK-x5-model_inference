#include "zero_copy_yolo_runner.hpp"

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
}  // namespace

int main() {
  const zerocopy::ZeroCopyYoloRunner::Config runner_defaults{};
  CHECK(runner_defaults.bpu_core_id == zerocopy::kX5BpuCore0);

  const zerocopy::ModelInputSpec spec{1, 3, 640, 640, 614400};
  const zerocopy::ExternalMemory memory{
      0x100000, reinterpret_cast<void*>(0x200000), 614400, 614400};
  const auto binding = zerocopy::makeInputBinding(spec, memory);
  CHECK(binding.phy_addr == memory.phy_addr);
  CHECK(binding.virt_addr == memory.virt_addr);
  CHECK(binding.bytes == 614400);

  bool rejected = false;
  try {
    zerocopy::makeInputBinding(
        spec, {0x100000, reinterpret_cast<void*>(0x200000), 600000, 600000});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  CHECK(rejected);

  rejected = false;
  try {
    zerocopy::makeInputBinding({1, 3, 320, 320, 153600}, memory);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  CHECK(rejected);

  if (failures != 0) return 1;
  std::cout << "test_tensor_binding: PASS\n";
  return 0;
}
