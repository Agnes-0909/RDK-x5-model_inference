#include "app_config.hpp"

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
void checkInvalid(Fn&& fn, const std::string& expected) {
  try {
    fn();
    std::cerr << "expected invalid_argument containing: " << expected << '\n';
    ++failures;
  } catch (const std::invalid_argument& error) {
    CHECK(std::string(error.what()).find(expected) != std::string::npos);
  }
}

}  // namespace

int main() {
  const auto defaults = zerocopy::defaultConfig();
  CHECK(defaults.model_path == "/root/mipi_proj/model/model.bin");
  CHECK(defaults.frames == 100);
  CHECK(defaults.left_host == 0);
  CHECK(defaults.right_host == 2);
  CHECK(defaults.frame_timeout_ms == 2000);
  CHECK(defaults.max_consecutive_timeouts == 5);
  CHECK(defaults.dnn_timeout_ms == 5000);
  CHECK(std::fabs(defaults.score_threshold - 0.25F) < 1e-6F);
  CHECK(std::fabs(defaults.nms_threshold - 0.45F) < 1e-6F);
  CHECK(defaults.log_dir == "logs");
  CHECK(defaults.log_level == zerocopy::LogLevel::kInfo);
  CHECK(defaults.log_max_mb == 100);
  CHECK(defaults.log_files == 5);

  char app[] = "app";
  char frames[] = "--frames";
  char zero[] = "0";
  char score[] = "--score";
  char point_four[] = "0.4";
  char* argv[] = {app, frames, zero, score, point_four};
  const auto parsed = zerocopy::parseArgs(5, argv);
  CHECK(parsed.frames == 0);
  CHECK(std::fabs(parsed.score_threshold - 0.4F) < 1e-6F);

  char log_dir_option[] = "--log-dir";
  char log_dir[] = "/tmp/run";
  char log_level_option[] = "--log-level";
  char debug[] = "debug";
  char log_max_option[] = "--log-max-mb";
  char two[] = "2";
  char log_files_option[] = "--log-files";
  char three[] = "3";
  char* log_argv[] = {app, log_dir_option, log_dir, log_level_option, debug,
                      log_max_option, two, log_files_option, three};
  const auto log_parsed = zerocopy::parseArgs(9, log_argv);
  CHECK(log_parsed.log_dir == "/tmp/run");
  CHECK(log_parsed.log_level == zerocopy::LogLevel::kDebug);
  CHECK(log_parsed.log_max_mb == 2);
  CHECK(log_parsed.log_files == 3);

  checkInvalid([] {
    char a[] = "app", f[] = "--frames", v[] = "-1";
    char* args[] = {a, f, v};
    zerocopy::parseArgs(3, args);
  }, "frames");
  checkInvalid([] {
    char a[] = "app", l[] = "--left-host", lv[] = "2";
    char r[] = "--right-host", rv[] = "2";
    char* args[] = {a, l, lv, r, rv};
    zerocopy::parseArgs(5, args);
  }, "different");
  checkInvalid([] {
    char a[] = "app", s[] = "--score", v[] = "1.0";
    char* args[] = {a, s, v};
    zerocopy::parseArgs(3, args);
  }, "score");
  checkInvalid([] {
    char a[] = "app", u[] = "--unknown";
    char* args[] = {a, u};
    zerocopy::parseArgs(2, args);
  }, "unknown option");
  checkInvalid([] {
    char a[] = "app", l[] = "--log-level", v[] = "verbose";
    char* args[] = {a, l, v};
    zerocopy::parseArgs(3, args);
  }, "log level");
  checkInvalid([] {
    char a[] = "app", m[] = "--log-max-mb", v[] = "0";
    char* args[] = {a, m, v};
    zerocopy::parseArgs(3, args);
  }, "log");
  checkInvalid([] {
    char a[] = "app", f[] = "--log-files", v[] = "0";
    char* args[] = {a, f, v};
    zerocopy::parseArgs(3, args);
  }, "log");

  if (failures != 0) {
    return 1;
  }
  std::cout << "test_app_config: PASS\n";
  return 0;
}
