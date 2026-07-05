#include "mipi_zero_copy_app.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace mipi_zero_copy {
namespace {

constexpr const char* kDefaultModelPath =
    "~/mipi_proj/model/model.bin";
constexpr const char* kDefaultConfigPath = "~/mipi_proj/config";
constexpr const char* kDefaultCalibrationPath =
    "~/mipi_proj/config/SC132gs_dual_calibration.yaml";

bool requireValue(int argc, char** argv, int index, std::string& error) {
  if (index + 1 < argc) {
    return true;
  }
  error = std::string("missing value for ") + argv[index];
  return false;
}

bool parseInt(const char* text, int& value) {
  char* end = nullptr;
  const long parsed = std::strtol(text, &end, 10);
  if (end == text || *end != '\0') {
    return false;
  }
  value = static_cast<int>(parsed);
  return true;
}

bool parseFloat(const char* text, float& value) {
  char* end = nullptr;
  const float parsed = std::strtof(text, &end);
  if (end == text || *end != '\0') {
    return false;
  }
  value = parsed;
  return true;
}

std::vector<std::string> splitChannels(const std::string& text) {
  std::vector<std::string> channels;
  std::stringstream ss(text);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (!item.empty()) {
      channels.push_back(item);
    }
  }
  return channels;
}

std::string expandUserPath(const std::string& path) {
  if (path.empty() || path[0] != '~') {
    return path;
  }
  if (path.size() > 1 && path[1] != '/') {
    return path;
  }

  const char* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') {
    return path;
  }
  if (path.size() == 1) {
    return home;
  }
  return std::string(home) + path.substr(1);
}

std::string makeAbsolutePath(const std::string& path) {
  if (path.empty()) {
    return path;
  }
  const std::filesystem::path expanded(expandUserPath(path));
  if (expanded.is_absolute()) {
    return expanded.lexically_normal().string();
  }
  return std::filesystem::absolute(expanded).lexically_normal().string();
}

}  // namespace

AppConfig defaultConfig() {
  AppConfig config;
  config.model_path = kDefaultModelPath;
  config.config_path = kDefaultConfigPath;
  config.sensor = "sc132gs-1280p";
  config.calibration_path = kDefaultCalibrationPath;
  config.infer_channels = {"left", "right"};
  return config;
}

std::string usage(const char* program) {
  std::ostringstream oss;
  oss << "Usage: " << program << " [options]\n"
      << "  --model PATH              RDK .bin model path\n"
      << "  --sensor NAME             sensor config name, default sc132gs-1280p\n"
      << "  --width N                 capture width, default 1088\n"
      << "  --height N                capture height, default 1280\n"
      << "  --fps N                   capture fps, default 30\n"
      << "  --channel N               first MIPI host, default 0\n"
      << "  --channel2 N              second MIPI host, default 2\n"
      << "  --infer-channel LIST      comma-separated getFrame channels\n"
      << "  --frames N                frames per channel, 0 means continuous\n"
      << "  --score F                 score threshold, default 0.25\n"
      << "  --nms F                   NMS threshold, default 0.45\n"
      << "  --classes N               class count, default 80\n"
      << "  --bpu-core N              BPU core id, default runtime any\n"
      << "  --timeout-ms N            DNN timeout, default 5000\n"
      << "  --config-path PATH        camera config directory\n"
      << "  --calibration PATH        camera calibration yaml\n"
      << "  --output-dir PATH         directory for JSONL, summary and visual output\n"
      << "  --save-vis                save JPEG visualizations with detection boxes\n"
      << "  --save-every N            save one visualization every N frames, default 1\n"
      << "  --vis-rotate N            rotate visualizations clockwise: 0,90,180,270; default 90\n"
      << "  --log-jsonl               write per-frame timing and detection JSONL\n"
      << "  --warmup N                exclude first N frame indexes from summary stats\n"
      << "  --dual-combine N          hobot_mipi_cam dual_combine mode\n"
      << "  --no-gdc                  disable GDC\n"
      << "  --help                    print this message\n";
  return oss.str();
}

bool parseArgs(int argc, char** argv, AppConfig& config, std::string& error) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto parse_int_option = [&](int& field) {
      if (!requireValue(argc, argv, i, error)) {
        return false;
      }
      if (!parseInt(argv[++i], field)) {
        error = "invalid integer for " + arg + ": " + argv[i];
        return false;
      }
      return true;
    };
    auto parse_float_option = [&](float& field) {
      if (!requireValue(argc, argv, i, error)) {
        return false;
      }
      if (!parseFloat(argv[++i], field)) {
        error = "invalid float for " + arg + ": " + argv[i];
        return false;
      }
      return true;
    };

    if (arg == "--help" || arg == "-h") {
      config.show_help = true;
    } else if (arg == "--model") {
      if (!requireValue(argc, argv, i, error)) return false;
      config.model_path = argv[++i];
    } else if (arg == "--sensor") {
      if (!requireValue(argc, argv, i, error)) return false;
      config.sensor = argv[++i];
    } else if (arg == "--width") {
      if (!parse_int_option(config.image_width)) return false;
    } else if (arg == "--height") {
      if (!parse_int_option(config.image_height)) return false;
    } else if (arg == "--fps") {
      if (!parse_int_option(config.framerate)) return false;
    } else if (arg == "--channel") {
      if (!parse_int_option(config.channel)) return false;
    } else if (arg == "--channel2") {
      if (!parse_int_option(config.channel2)) return false;
    } else if (arg == "--frames") {
      if (!parse_int_option(config.frame_count)) return false;
    } else if (arg == "--score") {
      if (!parse_float_option(config.score_threshold)) return false;
    } else if (arg == "--nms") {
      if (!parse_float_option(config.nms_threshold)) return false;
    } else if (arg == "--classes") {
      if (!parse_int_option(config.num_classes)) return false;
    } else if (arg == "--bpu-core") {
      if (!parse_int_option(config.bpu_core_id)) return false;
    } else if (arg == "--timeout-ms") {
      if (!parse_int_option(config.timeout_ms)) return false;
    } else if (arg == "--config-path") {
      if (!requireValue(argc, argv, i, error)) return false;
      config.config_path = argv[++i];
    } else if (arg == "--calibration") {
      if (!requireValue(argc, argv, i, error)) return false;
      config.calibration_path = argv[++i];
    } else if (arg == "--output-dir") {
      if (!requireValue(argc, argv, i, error)) return false;
      config.output_dir = argv[++i];
    } else if (arg == "--save-vis") {
      config.save_visualizations = true;
    } else if (arg == "--save-every") {
      if (!parse_int_option(config.save_every)) return false;
      if (config.save_every <= 0) {
        error = "--save-every must be greater than 0";
        return false;
      }
    } else if (arg == "--vis-rotate") {
      if (!parse_int_option(config.vis_rotate_cw)) return false;
      if (config.vis_rotate_cw != 0 && config.vis_rotate_cw != 90 &&
          config.vis_rotate_cw != 180 && config.vis_rotate_cw != 270) {
        error = "--vis-rotate must be one of 0, 90, 180, 270";
        return false;
      }
    } else if (arg == "--log-jsonl") {
      config.log_jsonl = true;
    } else if (arg == "--warmup") {
      if (!parse_int_option(config.warmup_frames)) return false;
      if (config.warmup_frames < 0) {
        error = "--warmup must be greater than or equal to 0";
        return false;
      }
    } else if (arg == "--dual-combine") {
      if (!parse_int_option(config.dual_combine)) return false;
    } else if (arg == "--infer-channel") {
      if (!requireValue(argc, argv, i, error)) return false;
      config.infer_channels = splitChannels(argv[++i]);
      if (config.infer_channels.empty()) {
        error = "empty channel list for --infer-channel";
        return false;
      }
    } else if (arg == "--no-gdc") {
      config.gdc_enable = false;
    } else {
      error = "unknown option: " + arg;
      return false;
    }
  }

  return true;
}

void normalizePaths(AppConfig& config) {
  config.model_path = makeAbsolutePath(config.model_path);
  config.config_path = makeAbsolutePath(config.config_path);
  config.calibration_path = makeAbsolutePath(config.calibration_path);
  if (!config.output_dir.empty()) {
    config.output_dir = makeAbsolutePath(config.output_dir);
  }
}

mipi_cam::MIPI_CAP_INFO_ST makeCapInfo(const AppConfig& config) {
  mipi_cam::MIPI_CAP_INFO_ST info;
  info.config_path = config.config_path;
  info.sensor_type = config.sensor;
  info.out_format_name = config.out_format;
  info.width = config.image_width;
  info.height = config.image_height;
  info.sub_width = config.sub_image_width;
  info.sub_height = config.sub_image_height;
  info.fps = config.framerate;
  info.channel_ = config.channel;
  info.channel2_ = config.channel2;
  info.rotation_ = 0.0;
  info.cal_rotation_ = 0.0;
  info.stream_mode_ = config.stream_mode;
  info.device_mode_ = config.device_mode;
  info.dual_combine_ = config.dual_combine;
  info.lpwm_enable_ = config.lpwm_enable;
  info.gdc_enable_ = config.gdc_enable;
  info.gdc_bin_file_.clear();
  info.frame_ts_type_ = "sensor";
  info.link_type_ = config.link_type;
  info.link_port_ = config.link_port;
  info.gsml_cfg_file_.clear();
  info.cal_alpha_ = 0.0;
  info.sub_stream_enable_ = config.sub_stream_enable;
  info.sync_awb_ = config.sync_awb;
  info.sync_ae_ = config.sync_ae;
  info.print_isp_log_ = config.print_isp_log;
  return info;
}

rdk::YoloInfer::Nv12Frame makeNv12FrameView(
    const mipi_cam::VideoBuffer& buffer) {
  rdk::YoloInfer::Nv12Frame frame;
  const auto* data = reinterpret_cast<const uint8_t*>(buffer.buff.data());
  const int stride = buffer.stride > 0 ? buffer.stride : buffer.width;

  frame.data = data;
  frame.y_plane = data;
  frame.uv_plane = data + static_cast<size_t>(stride) * buffer.height;
  frame.width = buffer.width;
  frame.height = buffer.height;
  frame.stride_y = stride;
  frame.stride_uv = stride;
  frame.crop_x = 0;
  frame.crop_y = 0;
  frame.crop_width = buffer.width;
  frame.crop_height = buffer.height;
  frame.source_width = buffer.width;
  frame.source_height = buffer.height;
  frame.preprocessed = false;
  return frame;
}

}  // namespace mipi_zero_copy
