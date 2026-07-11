#ifndef MIPI_VSE_BPU_ZEROCOPY_APP_CONFIG_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_APP_CONFIG_HPP_

#include <string>

namespace zerocopy {

enum class LogLevel {
  kDebug = 0,
  kInfo = 1,
  kWarn = 2,
  kError = 3,
};

struct AppConfig {
  std::string model_path = "/root/mipi_proj/model/model.bin";
  int frames = 100;
  int left_host = 0;
  int right_host = 2;
  int frame_timeout_ms = 2000;
  int max_consecutive_timeouts = 5;
  int dnn_timeout_ms = 5000;
  float score_threshold = 0.25F;
  float nms_threshold = 0.45F;
  std::string log_dir = "logs";
  LogLevel log_level = LogLevel::kInfo;
  int log_max_mb = 100;
  int log_files = 5;
};

AppConfig defaultConfig();
AppConfig parseArgs(int argc, char** argv);
std::string usage(const char* program);

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_APP_CONFIG_HPP_
