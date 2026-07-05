#include "mipi_zero_copy_app.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

void testDefaultConfigMatchesGs130wDualCamera() {
  const auto config = mipi_zero_copy::defaultConfig();

  assert(config.sensor == "sc132gs-1280p");
  assert(config.device_mode == "dual");
  assert(config.dual_combine == 0);
  assert(config.out_format == "nv12");
  assert(config.image_width == 1088);
  assert(config.image_height == 1280);
  assert(config.channel == 0);
  assert(config.channel2 == 2);
  assert(config.infer_channels.size() == 2);
  assert(config.infer_channels[0] == "left");
  assert(config.infer_channels[1] == "right");
  assert(config.vis_rotate_cw == 90);
}

void testParseArgsOverridesRuntimeOptions() {
  const char* argv[] = {
      "mipi_zero_copy_infer",
      "--model",
      "/tmp/model.bin",
      "--frames",
      "12",
      "--infer-channel",
      "combine",
      "--score",
      "0.4",
      "--output-dir",
      "/tmp/mipi_run",
      "--save-vis",
      "--save-every",
      "3",
      "--log-jsonl",
      "--warmup",
      "2",
      "--vis-rotate",
      "180",
      "--no-gdc",
      "--help",
  };
  int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));

  std::string error;
  auto config = mipi_zero_copy::defaultConfig();
  const bool ok = mipi_zero_copy::parseArgs(argc, const_cast<char**>(argv),
                                            config, error);

  assert(ok);
  assert(config.model_path == "/tmp/model.bin");
  assert(config.frame_count == 12);
  assert(config.infer_channels.size() == 1);
  assert(config.infer_channels[0] == "combine");
  assert(config.score_threshold == 0.4F);
  assert(config.output_dir == "/tmp/mipi_run");
  assert(config.save_visualizations);
  assert(config.save_every == 3);
  assert(config.log_jsonl);
  assert(config.warmup_frames == 2);
  assert(config.vis_rotate_cw == 180);
  assert(!config.gdc_enable);
  assert(config.show_help);
}

void testParseArgsRejectsInvalidOutputOptions() {
  {
    const char* argv[] = {"mipi_zero_copy_infer", "--save-every", "0"};
    int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));
    std::string error;
    auto config = mipi_zero_copy::defaultConfig();
    const bool ok = mipi_zero_copy::parseArgs(argc, const_cast<char**>(argv),
                                              config, error);
    assert(!ok);
    assert(error == "--save-every must be greater than 0");
  }

  {
    const char* argv[] = {"mipi_zero_copy_infer", "--warmup", "-1"};
    int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));
    std::string error;
    auto config = mipi_zero_copy::defaultConfig();
    const bool ok = mipi_zero_copy::parseArgs(argc, const_cast<char**>(argv),
                                              config, error);
    assert(!ok);
    assert(error == "--warmup must be greater than or equal to 0");
  }

  {
    const char* argv[] = {"mipi_zero_copy_infer", "--vis-rotate", "45"};
    int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));
    std::string error;
    auto config = mipi_zero_copy::defaultConfig();
    const bool ok = mipi_zero_copy::parseArgs(argc, const_cast<char**>(argv),
                                              config, error);
    assert(!ok);
    assert(error == "--vis-rotate must be one of 0, 90, 180, 270");
  }
}

void testNormalizePathsExpandsHomeDirectory() {
  setenv("HOME", "/home/test", 1);
  auto config = mipi_zero_copy::defaultConfig();
  config.model_path = "~/mipi_proj/model/model.bin";
  config.config_path = "~/mipi_proj/config";
  config.calibration_path = "~/mipi_proj/config/calibration.yaml";
  config.output_dir = "~/mipi_proj/out";

  mipi_zero_copy::normalizePaths(config);

  assert(config.model_path == "/home/test/mipi_proj/model/model.bin");
  assert(config.config_path == "/home/test/mipi_proj/config");
  assert(config.calibration_path == "/home/test/mipi_proj/config/calibration.yaml");
  assert(config.output_dir == "/home/test/mipi_proj/out");
}

void testMakeCapInfoUsesCameraSettings() {
  auto config = mipi_zero_copy::defaultConfig();
  config.framerate = 25;
  config.calibration_path = "/tmp/calib.yaml";

  const auto cap_info = mipi_zero_copy::makeCapInfo(config);

  assert(cap_info.sensor_type == "sc132gs-1280p");
  assert(cap_info.width == 1088);
  assert(cap_info.height == 1280);
  assert(cap_info.fps == 25);
  assert(cap_info.channel_ == 0);
  assert(cap_info.channel2_ == 2);
  assert(cap_info.device_mode_ == "dual");
  assert(cap_info.dual_combine_ == 0);
  assert(!cap_info.gdc_enable_);
  assert(cap_info.out_format_name == "nv12");
}

void testNv12FrameViewPointsAtVideoBufferData() {
  auto manager = std::make_shared<mipi_cam::BuffQueueManage>();
  auto buffer = std::make_shared<mipi_cam::VideoBuffer>(manager);
  buffer->width = 8;
  buffer->height = 4;
  buffer->stride = 8;
  buffer->buff.resize(8 * 4 * 3 / 2);
  std::memset(buffer->buff.data(), 123, buffer->buff.size());

  const auto frame = mipi_zero_copy::makeNv12FrameView(*buffer);

  assert(frame.data == reinterpret_cast<const uint8_t*>(buffer->buff.data()));
  assert(frame.y_plane == reinterpret_cast<const uint8_t*>(buffer->buff.data()));
  assert(frame.uv_plane ==
         reinterpret_cast<const uint8_t*>(buffer->buff.data()) + 8 * 4);
  assert(frame.width == 8);
  assert(frame.height == 4);
  assert(frame.stride_y == 8);
  assert(frame.stride_uv == 8);
  assert(frame.crop_width == 8);
  assert(frame.crop_height == 4);
  assert(!frame.preprocessed);
}

}  // namespace

int main() {
  testDefaultConfigMatchesGs130wDualCamera();
  testParseArgsOverridesRuntimeOptions();
  testParseArgsRejectsInvalidOutputOptions();
  testNormalizePathsExpandsHomeDirectory();
  testMakeCapInfoUsesCameraSettings();
  testNv12FrameViewPointsAtVideoBufferData();
  std::cout << "mipi_zero_copy_config tests passed\n";
  return 0;
}
