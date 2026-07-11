#include "app_config.hpp"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace zerocopy {
namespace {

int parseInt(const std::string& option, const char* value) {
  if (value == nullptr || *value == '\0') {
    throw std::invalid_argument(option + " requires a value");
  }
  errno = 0;
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' ||
      parsed < std::numeric_limits<int>::min() ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(option + " has an invalid integer value");
  }
  return static_cast<int>(parsed);
}

float parseFloat(const std::string& option, const char* value) {
  if (value == nullptr || *value == '\0') {
    throw std::invalid_argument(option + " requires a value");
  }
  errno = 0;
  char* end = nullptr;
  const float parsed = std::strtof(value, &end);
  if (errno != 0 || end == value || *end != '\0') {
    throw std::invalid_argument(option + " has an invalid float value");
  }
  return parsed;
}

const char* requireValue(int argc, char** argv, int& index,
                         const std::string& option) {
  if (index + 1 >= argc) {
    throw std::invalid_argument(option + " requires a value");
  }
  return argv[++index];
}

void validate(const AppConfig& config) {
  if (config.model_path.empty()) {
    throw std::invalid_argument("model path must not be empty");
  }
  if (config.frames < 0) {
    throw std::invalid_argument("frames must be zero or positive");
  }
  if (config.left_host < 0 || config.right_host < 0) {
    throw std::invalid_argument("MIPI hosts must be zero or positive");
  }
  if (config.left_host == config.right_host) {
    throw std::invalid_argument("left and right hosts must be different");
  }
  if (config.frame_timeout_ms <= 0 || config.max_consecutive_timeouts <= 0 ||
      config.dnn_timeout_ms <= 0) {
    throw std::invalid_argument("timeouts and timeout limit must be positive");
  }
  if (!(config.score_threshold > 0.0F && config.score_threshold < 1.0F)) {
    throw std::invalid_argument("score threshold must be between zero and one");
  }
  if (!(config.nms_threshold > 0.0F && config.nms_threshold < 1.0F)) {
    throw std::invalid_argument("nms threshold must be between zero and one");
  }
}

}  // namespace

AppConfig defaultConfig() { return AppConfig{}; }

AppConfig parseArgs(int argc, char** argv) {
  AppConfig config = defaultConfig();
  for (int i = 1; i < argc; ++i) {
    const std::string option = argv[i];
    if (option == "--model") {
      config.model_path = requireValue(argc, argv, i, option);
    } else if (option == "--frames") {
      config.frames = parseInt(option, requireValue(argc, argv, i, option));
    } else if (option == "--left-host") {
      config.left_host = parseInt(option, requireValue(argc, argv, i, option));
    } else if (option == "--right-host") {
      config.right_host = parseInt(option, requireValue(argc, argv, i, option));
    } else if (option == "--frame-timeout-ms") {
      config.frame_timeout_ms =
          parseInt(option, requireValue(argc, argv, i, option));
    } else if (option == "--max-timeouts") {
      config.max_consecutive_timeouts =
          parseInt(option, requireValue(argc, argv, i, option));
    } else if (option == "--dnn-timeout-ms") {
      config.dnn_timeout_ms =
          parseInt(option, requireValue(argc, argv, i, option));
    } else if (option == "--score") {
      config.score_threshold =
          parseFloat(option, requireValue(argc, argv, i, option));
    } else if (option == "--nms") {
      config.nms_threshold =
          parseFloat(option, requireValue(argc, argv, i, option));
    } else if (option == "--help" || option == "-h") {
      throw std::invalid_argument("help requested");
    } else {
      throw std::invalid_argument("unknown option: " + option);
    }
  }
  validate(config);
  return config;
}

std::string usage(const char* program) {
  std::ostringstream stream;
  stream << "Usage: " << program
         << " [--model PATH] [--frames N] [--left-host N]"
            " [--right-host N] [--frame-timeout-ms N] [--max-timeouts N]"
            " [--dnn-timeout-ms N] [--score F] [--nms F]";
  return stream.str();
}

}  // namespace zerocopy
