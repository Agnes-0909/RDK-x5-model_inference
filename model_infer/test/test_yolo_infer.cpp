#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "yolo_infer.hpp"

namespace {

struct Args {
  std::string model_path;
  std::string image_path;
  std::string output_path;
  float score_threshold = 0.25F;
  float nms_threshold = 0.45F;
  int num_classes = 80;
};

void printUsage(const char* program) {
  std::cerr << "Usage: " << program
            << " --model model.bin --image image.jpg [options]\n\n"
            << "Options:\n"
            << "  --output result.jpg        save visualization image\n"
            << "  --score 0.25              score threshold\n"
            << "  --nms 0.45                NMS IoU threshold\n"
            << "  --classes 80              class count\n";
}

bool parseArgs(int argc, char** argv, Args& args) {
  for (int i = 1; i < argc; ++i) {
    const std::string key = argv[i];
    auto requireValue = [&](const std::string& name) -> const char* {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << name << "\n";
        return nullptr;
      }
      return argv[++i];
    };

    if (key == "--model") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      args.model_path = value;
    } else if (key == "--image") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      args.image_path = value;
    } else if (key == "--output") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      args.output_path = value;
    } else if (key == "--score") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      args.score_threshold = std::strtof(value, nullptr);
    } else if (key == "--nms") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      args.nms_threshold = std::strtof(value, nullptr);
    } else if (key == "--classes") {
      const char* value = requireValue(key);
      if (value == nullptr) return false;
      args.num_classes = std::atoi(value);
    } else if (key == "--help" || key == "-h") {
      printUsage(argv[0]);
      return false;
    } else {
      std::cerr << "Unknown argument: " << key << "\n";
      return false;
    }
  }

  if (args.model_path.empty() || args.image_path.empty()) {
    return false;
  }
  return true;
}

void drawDetections(cv::Mat& image,
                    const std::vector<rdk::YoloInfer::Detection>& detections) {
  for (const auto& detection : detections) {
    cv::rectangle(image, detection.box, cv::Scalar(0, 255, 0), 2);
    std::ostringstream label;
    label << detection.class_id << ":" << std::fixed << std::setprecision(2)
          << detection.score;
    cv::putText(image, label.str(), detection.box.tl() + cv::Point2f(0, -4),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
  }
}

}  // namespace

int main(int argc, char** argv) {
  Args args;
  if (!parseArgs(argc, argv, args)) {
    printUsage(argv[0]);
    return 1;
  }

  cv::Mat image = cv::imread(args.image_path);
  if (image.empty()) {
    std::cerr << "Failed to read image: " << args.image_path << "\n";
    return 1;
  }

  rdk::ModelInferenceBase::Config config;
  config.model_file = args.model_path;
  config.timeout_ms = 5000;

  rdk::YoloInfer::Options options;
  options.num_classes = args.num_classes;
  options.score_threshold = args.score_threshold;
  options.nms_threshold = args.nms_threshold;

  rdk::YoloInfer model(config, options);
  if (!model.initialize()) {
    std::cerr << "Initialize failed: " << model.lastError() << "\n";
    return 1;
  }

  std::cout << model.dumpModelInfo() << std::endl;

  if (!model.infer(image)) {
    std::cerr << "Infer failed: " << model.lastError() << "\n";
    return 1;
  }

  const auto& detections = model.detections();
  std::cout << "detections: " << detections.size() << "\n";
  for (const auto& detection : detections) {
    std::cout << "class=" << detection.class_id << ", score=" << std::fixed
              << std::setprecision(4) << detection.score << ", box=["
              << detection.box.x << ", " << detection.box.y << ", "
              << detection.box.width << ", " << detection.box.height << "]\n";
  }

  if (!args.output_path.empty()) {
    cv::Mat vis = image.clone();
    drawDetections(vis, detections);
    if (!cv::imwrite(args.output_path, vis)) {
      std::cerr << "Failed to write output image: " << args.output_path << "\n";
      return 1;
    }
    std::cout << "output saved to: " << args.output_path << "\n";
  }

  return 0;
}
