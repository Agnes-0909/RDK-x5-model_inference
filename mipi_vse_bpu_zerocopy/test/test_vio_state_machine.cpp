#include "vio_pipeline.hpp"

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
