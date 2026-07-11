#include "vio_pipeline.hpp"

#include <array>
#include <optional>
#include <iostream>
#include <string>
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
}  // namespace

int main() {
  CHECK(!zerocopy::shouldSetMclkAttribute(false));
  CHECK(zerocopy::shouldSetMclkAttribute(true));
  const auto host0 = zerocopy::makeSensorRouting(0, 0, 4);
  CHECK(host0.camera_phy == 0 && host0.vin_rx == 0 && host0.timestamp_source == 5);
  const auto host2 = zerocopy::makeSensorRouting(0, 2, 5);
  CHECK(host2.camera_phy == 0 && host2.vin_rx == 2 && host2.timestamp_source == 6);
  const auto active_high = zerocopy::sensorPowerSequence(true);
  CHECK((active_high == std::array<int, 3>{1, 0, 1}));
  const auto active_low = zerocopy::sensorPowerSequence(false);
  CHECK((active_low == std::array<int, 3>{0, 1, 0}));
  std::vector<int> probed;
  const auto address = zerocopy::detectSensorAddress(
      {0x30, 0x32, 0x33}, [&](int candidate) {
        probed.push_back(candidate);
        return candidate == 0x32;
      });
  CHECK(address.has_value() && *address == 0x32);
  CHECK((probed == std::vector<int>{0x30, 0x32}));
  CHECK(!zerocopy::detectSensorAddress({0x30, 0x33},
                                       [](int) { return false; })
             .has_value());

  std::vector<std::string> calls;
  {
    zerocopy::CleanupStack cleanup;
    cleanup.push([&] { calls.push_back("camera"); });
    cleanup.push([&] { calls.push_back("vin"); });
    cleanup.push([&] { calls.push_back("isp"); });
    cleanup.push([&] { calls.push_back("vse"); });
    cleanup.push([&] { calls.push_back("vflow"); });
    cleanup.run();
    cleanup.run();
  }
  const std::vector<std::string> expected{"vflow", "vse", "isp", "vin",
                                          "camera"};
  CHECK(calls == expected);

  calls.clear();
  {
    zerocopy::CleanupStack rollback;
    rollback.push([&] { calls.push_back("camera"); });
    rollback.push([&] { calls.push_back("vin"); });
  }
  CHECK((calls == std::vector<std::string>{"vin", "camera"}));

  if (failures != 0) return 1;
  std::cout << "test_vio_state_machine: PASS\n";
  return 0;
}
