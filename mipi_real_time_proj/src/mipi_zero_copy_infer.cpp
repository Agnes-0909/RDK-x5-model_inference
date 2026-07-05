#include "mipi_zero_copy_app.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "hobot_mipi_calibration.hpp"
#include "hobot_mipi_factory.hpp"
#include "inference_trace.hpp"
#include "rclcpp/rclcpp.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {

std::atomic_bool g_running{true};

void handleSignal(int) { g_running = false; }

using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

cv::Rect rotateRectClockwise(const cv::Rect& box, int image_width,
                             int image_height, int degrees) {
  switch (degrees) {
    case 90:
      return cv::Rect(image_height - box.y - box.height, box.x, box.height,
                      box.width);
    case 180:
      return cv::Rect(image_width - box.x - box.width,
                      image_height - box.y - box.height, box.width,
                      box.height);
    case 270:
      return cv::Rect(box.y, image_width - box.x - box.width, box.height,
                      box.width);
    case 0:
    default:
      return box;
  }
}

cv::Mat rotateImageClockwise(const cv::Mat& image, int degrees) {
  cv::Mat rotated;
  switch (degrees) {
    case 90:
      cv::rotate(image, rotated, cv::ROTATE_90_CLOCKWISE);
      return rotated;
    case 180:
      cv::rotate(image, rotated, cv::ROTATE_180);
      return rotated;
    case 270:
      cv::rotate(image, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);
      return rotated;
    case 0:
    default:
      return image;
  }
}

std::string jsonEscape(const std::string& text) {
  std::ostringstream oss;
  for (const char ch : text) {
    switch (ch) {
      case '\\':
        oss << "\\\\";
        break;
      case '"':
        oss << "\\\"";
        break;
      case '\n':
        oss << "\\n";
        break;
      case '\r':
        oss << "\\r";
        break;
      case '\t':
        oss << "\\t";
        break;
      default:
        oss << ch;
        break;
    }
  }
  return oss.str();
}

struct FrameTiming {
  double get_frame_ms = 0.0;
  double wrap_nv12_ms = 0.0;
  double infer_ms = 0.0;
  double print_result_ms = 0.0;
  double save_visualization_ms = 0.0;
  double total_frame_ms = 0.0;
};

struct ChannelStats {
  int samples = 0;
  int detections = 0;
  double get_frame_ms = 0.0;
  double wrap_nv12_ms = 0.0;
  double infer_ms = 0.0;
  double print_result_ms = 0.0;
  double save_visualization_ms = 0.0;
  double total_frame_ms = 0.0;
  double max_total_frame_ms = 0.0;

  void add(const FrameTiming& timing, int detection_count) {
    ++samples;
    detections += detection_count;
    get_frame_ms += timing.get_frame_ms;
    wrap_nv12_ms += timing.wrap_nv12_ms;
    infer_ms += timing.infer_ms;
    print_result_ms += timing.print_result_ms;
    save_visualization_ms += timing.save_visualization_ms;
    total_frame_ms += timing.total_frame_ms;
    max_total_frame_ms = std::max(max_total_frame_ms, timing.total_frame_ms);
  }

  double avg(double total) const {
    return samples > 0 ? total / static_cast<double>(samples) : 0.0;
  }
};

class RunArtifacts {
 public:
  bool open(const mipi_zero_copy::AppConfig& config) {
    enabled_ = !config.output_dir.empty() || config.log_jsonl ||
               config.save_visualizations;
    save_visualizations_ = config.save_visualizations;
    log_jsonl_ = config.log_jsonl;
    save_every_ = config.save_every;
    warmup_frames_ = config.warmup_frames;
    vis_rotate_cw_ = config.vis_rotate_cw;
    if (!enabled_) {
      return true;
    }

    output_dir_ = config.output_dir.empty() ? "mipi_zero_copy_output"
                                            : config.output_dir;
    vis_dir_ = output_dir_ / "vis";
    std::error_code ec;
    std::filesystem::create_directories(output_dir_, ec);
    if (ec) {
      last_error_ = "failed to create output directory " +
                    output_dir_.string() + ": " + ec.message();
      return false;
    }
    if (save_visualizations_) {
      std::filesystem::create_directories(vis_dir_, ec);
      if (ec) {
        last_error_ = "failed to create visualization directory " +
                      vis_dir_.string() + ": " + ec.message();
        return false;
      }
    }
    if (log_jsonl_) {
      jsonl_.open(output_dir_ / "frames.jsonl", std::ios::out);
      if (!jsonl_) {
        last_error_ = "failed to open frames.jsonl";
        return false;
      }
    }
    return true;
  }

  bool shouldSave(int frame_index) const {
    return save_visualizations_ && save_every_ > 0 &&
           (frame_index % save_every_) == 0;
  }

  bool saveVisualization(int frame_index, const std::string& channel,
                         const rdk::YoloInfer::Nv12Frame& frame,
                         const std::vector<rdk::YoloInfer::Detection>& detections,
                         std::string& path, std::string& error) const {
    if (!save_visualizations_) {
      return true;
    }
    if (frame.y_plane == nullptr || frame.uv_plane == nullptr) {
      error = "NV12 planes are null";
      return false;
    }

    const int stride_y = frame.stride_y > 0 ? frame.stride_y : frame.width;
    const int stride_uv = frame.stride_uv > 0 ? frame.stride_uv : stride_y;
    cv::Mat y(frame.height, frame.width, CV_8UC1,
              const_cast<uint8_t*>(frame.y_plane), stride_y);
    cv::Mat uv(frame.height / 2, frame.width / 2, CV_8UC2,
               const_cast<uint8_t*>(frame.uv_plane), stride_uv);
    cv::Mat bgr;
    cv::cvtColorTwoPlane(y, uv, bgr, cv::COLOR_YUV2BGR_NV12);

    cv::Mat display = rotateImageClockwise(bgr, vis_rotate_cw_);
    for (const auto& det : detections) {
      cv::Rect box(cv::Point(static_cast<int>(std::round(det.box.x)),
                             static_cast<int>(std::round(det.box.y))),
                   cv::Size(static_cast<int>(std::round(det.box.width)),
                            static_cast<int>(std::round(det.box.height))));
      box &= cv::Rect(0, 0, bgr.cols, bgr.rows);
      if (box.empty()) {
        continue;
      }
      box = rotateRectClockwise(box, bgr.cols, bgr.rows, vis_rotate_cw_);
      box &= cv::Rect(0, 0, display.cols, display.rows);
      if (box.empty()) {
        continue;
      }
      cv::rectangle(display, box, cv::Scalar(0, 255, 0), 2);
      std::ostringstream label;
      label << "cls=" << det.class_id << " " << std::fixed
            << std::setprecision(2) << det.score;
      const int base_y = std::max(0, box.y - 6);
      cv::putText(display, label.str(), cv::Point(box.x, base_y),
                  cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
    }

    std::ostringstream name;
    name << "frame_" << std::setw(6) << std::setfill('0') << frame_index
         << "_" << channel << ".jpg";
    const auto file_path = vis_dir_ / name.str();
    if (!cv::imwrite(file_path.string(), display)) {
      error = "cv::imwrite failed for " + file_path.string();
      return false;
    }
    path = file_path.string();
    return true;
  }

  void writeFrameRecord(int frame_index, const std::string& channel,
                        const rdk::YoloInfer::Nv12Frame& frame,
                        const std::vector<rdk::YoloInfer::Detection>& detections,
                        const FrameTiming& timing,
                        const std::string& visualization_path,
                        bool counted) {
    if (counted) {
      stats_[channel].add(timing, static_cast<int>(detections.size()));
    }

    if (!jsonl_) {
      return;
    }
    jsonl_ << std::fixed << std::setprecision(3)
           << "{\"frame\":" << frame_index
           << ",\"channel\":\"" << jsonEscape(channel) << "\""
           << ",\"counted\":" << (counted ? "true" : "false")
           << ",\"width\":" << frame.width
           << ",\"height\":" << frame.height
           << ",\"stride_y\":" << frame.stride_y
           << ",\"detections_count\":" << detections.size()
           << ",\"timing_ms\":{"
           << "\"get_frame\":" << timing.get_frame_ms
           << ",\"wrap_nv12\":" << timing.wrap_nv12_ms
           << ",\"infer_total\":" << timing.infer_ms
           << ",\"print_result\":" << timing.print_result_ms
           << ",\"save_visualization\":" << timing.save_visualization_ms
           << ",\"total_frame\":" << timing.total_frame_ms << "}";
    if (!visualization_path.empty()) {
      jsonl_ << ",\"visualization\":\"" << jsonEscape(visualization_path)
             << "\"";
    }
    jsonl_ << ",\"detections\":[";
    for (size_t i = 0; i < detections.size(); ++i) {
      if (i != 0) {
        jsonl_ << ",";
      }
      const auto& det = detections[i];
      jsonl_ << "{\"class_id\":" << det.class_id
             << ",\"score\":" << det.score
             << ",\"box\":{\"x\":" << det.box.x
             << ",\"y\":" << det.box.y
             << ",\"width\":" << det.box.width
             << ",\"height\":" << det.box.height << "}}";
    }
    jsonl_ << "]}\n";
    jsonl_.flush();
  }

  void writeSummary() const {
    if (!enabled_) {
      return;
    }
    std::ofstream summary(output_dir_ / "summary.txt", std::ios::out);
    if (!summary) {
      std::cerr << "[warn] failed to write summary.txt\n";
      return;
    }
    summary << std::fixed << std::setprecision(3);
    summary << "warmup_frames=" << warmup_frames_ << "\n";
    summary << "save_visualizations=" << (save_visualizations_ ? 1 : 0)
            << "\n";
    summary << "save_every=" << save_every_ << "\n";
    summary << "vis_rotate_cw=" << vis_rotate_cw_ << "\n";
    for (const auto& [channel, stat] : stats_) {
      summary << "\n[channel " << channel << "]\n";
      summary << "samples=" << stat.samples << "\n";
      summary << "detections=" << stat.detections << "\n";
      summary << "avg_get_frame_ms=" << stat.avg(stat.get_frame_ms) << "\n";
      summary << "avg_wrap_nv12_ms=" << stat.avg(stat.wrap_nv12_ms) << "\n";
      summary << "avg_infer_ms=" << stat.avg(stat.infer_ms) << "\n";
      summary << "avg_print_result_ms=" << stat.avg(stat.print_result_ms)
              << "\n";
      summary << "avg_save_visualization_ms="
              << stat.avg(stat.save_visualization_ms) << "\n";
      summary << "avg_total_frame_ms=" << stat.avg(stat.total_frame_ms)
              << "\n";
      summary << "max_total_frame_ms=" << stat.max_total_frame_ms << "\n";
      const double fps = stat.avg(stat.total_frame_ms) > 0.0
                             ? 1000.0 / stat.avg(stat.total_frame_ms)
                             : 0.0;
      summary << "approx_fps_per_channel=" << fps << "\n";
    }
    std::cout << "[artifact] summary=" << (output_dir_ / "summary.txt")
              << "\n";
  }

  const std::string& lastError() const { return last_error_; }
  const std::filesystem::path& outputDir() const { return output_dir_; }

 private:
  bool enabled_ = false;
  bool save_visualizations_ = false;
  bool log_jsonl_ = false;
  int save_every_ = 1;
  int warmup_frames_ = 0;
  int vis_rotate_cw_ = 90;
  std::filesystem::path output_dir_;
  std::filesystem::path vis_dir_;
  std::ofstream jsonl_;
  std::unordered_map<std::string, ChannelStats> stats_;
  std::string last_error_;
};

class ReturnedFrame {
 public:
  explicit ReturnedFrame(std::shared_ptr<mipi_cam::VideoBuffer> frame)
      : frame_(std::move(frame)) {}
  ~ReturnedFrame() {
    if (frame_) {
      frame_->return_empty_que();
    }
  }

  ReturnedFrame(const ReturnedFrame&) = delete;
  ReturnedFrame& operator=(const ReturnedFrame&) = delete;

  mipi_cam::VideoBuffer& get() { return *frame_; }
  explicit operator bool() const { return static_cast<bool>(frame_); }

 private:
  std::shared_ptr<mipi_cam::VideoBuffer> frame_;
};

class CameraSession {
 public:
  bool open(const mipi_zero_copy::AppConfig& config) {
    cap_ = mipi_cam::createMipiCap(mipi_cam::getBoardType());
    if (!cap_) {
      last_error_ = "createMipiCap failed";
      return false;
    }

    if (cap_->initEnv() != 0) {
      last_error_ = "HobotMipiCap::initEnv failed";
      return false;
    }

    auto cap_info = mipi_zero_copy::makeCapInfo(config);
    if (cap_info.device_mode_ == "dual") {
      std::vector<sensor_msgs::msg::CameraInfo> cam_info(2);
      if (mipi_cam::mipi_calibration::GetInstance().getDualCamCalibrationIml(
              cam_info[0], cam_info[1], config.calibration_path)) {
        cap_->setCamInfo(cam_info);
      } else {
        std::cerr << "[warn] failed to load dual calibration: "
                  << config.calibration_path << "\n";
      }
    } else {
      std::vector<sensor_msgs::msg::CameraInfo> cam_info(1);
      if (mipi_cam::mipi_calibration::GetInstance().getCamCalibrationIml_single(
              cam_info[0], config.calibration_path)) {
        cap_->setCamInfo(cam_info);
      } else {
        std::cerr << "[warn] failed to load camera calibration: "
                  << config.calibration_path << "\n";
      }
    }

    if (cap_->init(cap_info) != 0) {
      last_error_ = "HobotMipiCap::init failed";
      return false;
    }
    initialized_ = true;

    if (cap_->start() != 0) {
      last_error_ = "HobotMipiCap::start failed";
      return false;
    }
    started_ = true;
    return true;
  }

  void close() {
    if (cap_ && started_) {
      cap_->stop();
      started_ = false;
    }
    if (cap_ && initialized_) {
      cap_->deInit();
      initialized_ = false;
    }
    cap_.reset();
  }

  ReturnedFrame getFrame(const std::string& channel) {
    if (!cap_) {
      return ReturnedFrame(nullptr);
    }
    return ReturnedFrame(cap_->getFrame(channel));
  }

  const std::string& lastError() const { return last_error_; }

  ~CameraSession() { close(); }

 private:
  std::shared_ptr<mipi_cam::HobotMipiCap> cap_;
  bool initialized_ = false;
  bool started_ = false;
  std::string last_error_;
};

void printDetections(const std::string& channel, int frame_index,
                     const rdk::YoloInfer& infer) {
  const auto& detections = infer.detections();
  std::cout << "[result] frame=" << frame_index << " channel=" << channel
            << " detections=" << detections.size() << "\n";
  for (size_t i = 0; i < detections.size(); ++i) {
    const auto& det = detections[i];
    std::cout << "  #" << i << " class=" << det.class_id
              << " score=" << det.score << " box=(" << det.box.x << ","
              << det.box.y << "," << det.box.width << ","
              << det.box.height << ")\n";
  }
}

}  // namespace

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  auto config = mipi_zero_copy::defaultConfig();
  std::string error;
  if (!mipi_zero_copy::parseArgs(argc, argv, config, error)) {
    std::cerr << error << "\n" << mipi_zero_copy::usage(argv[0]);
    rclcpp::shutdown();
    return 2;
  }
  if (config.show_help) {
    std::cout << mipi_zero_copy::usage(argv[0]);
    rclcpp::shutdown();
    return 0;
  }
  mipi_zero_copy::normalizePaths(config);

  rdk::ModelInferenceBase::Config infer_config;
  infer_config.model_file = config.model_path;
  infer_config.timeout_ms = config.timeout_ms;
  if (config.bpu_core_id >= 0) {
    infer_config.bpu_core_id = config.bpu_core_id;
  }

  rdk::YoloInfer::Options yolo_options;
  yolo_options.num_classes = config.num_classes;
  yolo_options.score_threshold = config.score_threshold;
  yolo_options.nms_threshold = config.nms_threshold;

  rdk::YoloInfer infer(infer_config, yolo_options);
  if (!infer.initialize()) {
    std::cerr << "[error] model initialize failed: " << infer.lastError()
              << "\n";
    rclcpp::shutdown();
    return 1;
  }
  std::cout << infer.dumpModelInfo();

  CameraSession camera;
  if (!camera.open(config)) {
    std::cerr << "[error] camera open failed: " << camera.lastError() << "\n";
    rclcpp::shutdown();
    return 1;
  }

  RunArtifacts artifacts;
  if (!artifacts.open(config)) {
    std::cerr << "[error] artifact setup failed: " << artifacts.lastError()
              << "\n";
    rclcpp::shutdown();
    return 1;
  }
  if (!artifacts.outputDir().empty()) {
    std::cout << "[artifact] output_dir=" << artifacts.outputDir() << "\n";
  }

  int frame_index = 0;
  while (g_running &&
         (config.frame_count <= 0 || frame_index < config.frame_count)) {
    for (size_t channel_index = 0; channel_index < config.infer_channels.size();
         ++channel_index) {
      const auto& channel = config.infer_channels[channel_index];
      if (!g_running) {
        break;
      }

      const auto frame_start = Clock::now();
      const auto get_frame_start = frame_start;
      auto frame = camera.getFrame(channel);
      const auto get_frame_end = Clock::now();
      if (!frame) {
        std::cerr << "[warn] getFrame returned null for channel=" << channel
                  << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }

      const auto wrap_start = Clock::now();
      auto nv12 = mipi_zero_copy::makeNv12FrameView(frame.get());
      const auto wrap_end = Clock::now();
      rdk::SetInferenceTraceContext(
          rdk::InferenceTraceContext{
              0, static_cast<uint32_t>(channel_index),
              static_cast<uint64_t>(frame_index), true});
      const auto infer_start = Clock::now();
      if (!infer.infer(nv12)) {
        rdk::ClearInferenceTraceContext();
        std::cerr << "[error] infer failed frame=" << frame_index
                  << " channel=" << channel << " error=" << infer.lastError()
                  << "\n";
        continue;
      }
      const auto infer_end = Clock::now();
      rdk::ClearInferenceTraceContext();
      const auto print_start = Clock::now();
      std::cout << "[perf] frame=" << frame_index << " channel=" << channel
                << " width=" << nv12.width << " height=" << nv12.height
                << " stride=" << nv12.stride_y
                << " infer_ms=" << elapsedMs(infer_start, infer_end) << "\n";
      printDetections(channel, frame_index, infer);
      const auto print_end = Clock::now();

      std::string visualization_path;
      double save_visualization_ms = 0.0;
      if (artifacts.shouldSave(frame_index)) {
        std::string save_error;
        const auto save_start = Clock::now();
        if (!artifacts.saveVisualization(frame_index, channel, nv12,
                                         infer.detections(),
                                         visualization_path, save_error)) {
          std::cerr << "[warn] save visualization failed frame=" << frame_index
                    << " channel=" << channel << ": " << save_error << "\n";
        } else {
          std::cout << "[artifact] visualization=" << visualization_path
                    << "\n";
        }
        save_visualization_ms = elapsedMs(save_start, Clock::now());
      }

      const auto frame_end = Clock::now();
      FrameTiming timing;
      timing.get_frame_ms = elapsedMs(get_frame_start, get_frame_end);
      timing.wrap_nv12_ms = elapsedMs(wrap_start, wrap_end);
      timing.infer_ms = elapsedMs(infer_start, infer_end);
      timing.print_result_ms = elapsedMs(print_start, print_end);
      timing.save_visualization_ms = save_visualization_ms;
      timing.total_frame_ms = elapsedMs(frame_start, frame_end);
      const bool counted = frame_index >= config.warmup_frames;
      std::cout << std::fixed << std::setprecision(3)
                << "[perf] [mipi_zero_copy_chain]"
                << " frame=" << frame_index
                << " channel=" << channel
                << " counted=" << (counted ? 1 : 0)
                << " get_frame_ms=" << timing.get_frame_ms
                << " wrap_nv12_ms=" << timing.wrap_nv12_ms
                << " infer_ms=" << timing.infer_ms
                << " print_result_ms=" << timing.print_result_ms
                << " save_visualization_ms=" << timing.save_visualization_ms
                << " total_frame_ms=" << timing.total_frame_ms << "\n";
      artifacts.writeFrameRecord(frame_index, channel, nv12,
                                 infer.detections(), timing,
                                 visualization_path, counted);
    }
    ++frame_index;
  }

  artifacts.writeSummary();
  camera.close();
  infer.release();
  rclcpp::shutdown();
  return 0;
}
