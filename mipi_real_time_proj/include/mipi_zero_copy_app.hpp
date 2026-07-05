#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hobot_mipi_comm.hpp"
#include "yolo_infer.hpp"

namespace mipi_zero_copy {

struct AppConfig {
  std::string model_path;
  std::string config_path;
  std::string sensor;
  std::string calibration_path;
  std::string output_dir;
  std::string out_format = "nv12";
  std::string device_mode = "dual";
  std::vector<std::string> infer_channels;
  int image_width = 1088;
  int image_height = 1280;
  int sub_image_width = 960;
  int sub_image_height = 540;
  int framerate = 30;
  int channel = 0;
  int channel2 = 2;
  int dual_combine = 0;
  int stream_mode = 0;
  int link_type = 0;
  int link_port = 0;
  int frame_count = 0;
  int timeout_ms = 5000;
  int bpu_core_id = -1;
  int num_classes = 80;
  int save_every = 1;
  int warmup_frames = 0;
  int vis_rotate_cw = 90;
  bool lpwm_enable = false;
  bool gdc_enable = false;
  bool sub_stream_enable = false;
  bool sync_awb = false;
  bool sync_ae = false;
  bool print_isp_log = false;
  bool show_help = false;
  bool save_visualizations = false;
  bool log_jsonl = false;
  float score_threshold = 0.25F;
  float nms_threshold = 0.45F;
};

AppConfig defaultConfig();
std::string usage(const char* program);
bool parseArgs(int argc, char** argv, AppConfig& config, std::string& error);
void normalizePaths(AppConfig& config);
mipi_cam::MIPI_CAP_INFO_ST makeCapInfo(const AppConfig& config);
rdk::YoloInfer::Nv12Frame makeNv12FrameView(
    const mipi_cam::VideoBuffer& buffer);

}  // namespace mipi_zero_copy
