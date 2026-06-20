#include "image_codec.hpp"

#include <algorithm>
#include <chrono>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "logger.hpp"
#include "protocol.hpp"

namespace mt {

TransportFrameInfo BuildTransportFrameInfo(const ModelInputInfo& model_input_info,
                                           ImageLoadMode mode) {
  TransportFrameInfo info{};
  info.source_width = model_input_info.source_width;
  info.source_height = model_input_info.source_height;
  if (mode == ImageLoadMode::kSourceFrame) {
    info.width = model_input_info.source_width;
    info.height = model_input_info.source_height;
    info.scale = 1.0F;
    info.preprocessed = false;
    return info;
  }

  info.width = model_input_info.input_width;
  info.height = model_input_info.input_height;
  info.pad_x = model_input_info.pad_x;
  info.pad_y = model_input_info.pad_y;
  info.scale = model_input_info.scale;
  info.preprocessed = true;
  return info;
}

bool loadImageAsNv12(const std::string& image_path, ImageLoadMode mode,
                     std::vector<uint8_t>& nv12_buffer, std::string& error,
                     ImageLoadMetrics* metrics, ModelInputInfo* model_input_info) {
  const auto start = std::chrono::steady_clock::now();
  const auto imread_start = start;
  cv::Mat image = cv::imread(image_path);
  const auto imread_end = std::chrono::steady_clock::now();
  if (image.empty()) {
    error = "failed to read image: " + image_path;
    return false;
  }

  const auto resize_start = imread_end;
  const float scale = std::min(
      static_cast<float>(kModelInputWidth) / static_cast<float>(image.cols),
      static_cast<float>(kModelInputHeight) / static_cast<float>(image.rows));
  const int resized_width =
      std::max(1, static_cast<int>(std::round(image.cols * scale)));
  const int resized_height =
      std::max(1, static_cast<int>(std::round(image.rows * scale)));
  const int pad_x = (static_cast<int>(kModelInputWidth) - resized_width) / 2;
  const int pad_y = (static_cast<int>(kModelInputHeight) - resized_height) / 2;

  cv::Mat model_input;
  if (mode == ImageLoadMode::kSourceFrame) {
    model_input = image;
  } else {
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(resized_width, resized_height), 0, 0,
               cv::INTER_LINEAR);
    model_input = cv::Mat(static_cast<int>(kModelInputHeight),
                          static_cast<int>(kModelInputWidth), image.type(),
                          cv::Scalar(114, 114, 114));
    resized.copyTo(model_input(
        cv::Rect(pad_x, pad_y, resized_width, resized_height)));
  }
  const auto resize_end = std::chrono::steady_clock::now();

  const auto convert_start = resize_end;
  cv::Mat yuv_i420;
  cv::cvtColor(model_input, yuv_i420, cv::COLOR_BGR2YUV_I420);
  const auto convert_end = std::chrono::steady_clock::now();
  const uint32_t output_width = static_cast<uint32_t>(model_input.cols);
  const uint32_t output_height = static_cast<uint32_t>(model_input.rows);
  const size_t nv12_bytes =
      static_cast<size_t>(output_width) * output_height * 3 / 2;
  nv12_buffer.resize(nv12_bytes);

  const auto pack_start = convert_end;
  const size_t y_plane_bytes = static_cast<size_t>(output_width) * output_height;
  const size_t uv_plane_bytes = y_plane_bytes / 4;
  const uint8_t* src = yuv_i420.ptr<uint8_t>();
  std::copy(src, src + y_plane_bytes, nv12_buffer.begin());
  const uint8_t* u_plane = src + y_plane_bytes;
  const uint8_t* v_plane = u_plane + uv_plane_bytes;
  uint8_t* uv_dst = nv12_buffer.data() + y_plane_bytes;
  for (size_t i = 0; i < uv_plane_bytes; ++i) {
    uv_dst[i * 2] = u_plane[i];
    uv_dst[i * 2 + 1] = v_plane[i];
  }
  const auto end = std::chrono::steady_clock::now();

  if (metrics != nullptr) {
    metrics->imread_ms = elapsedMs(imread_start, imread_end);
    metrics->resize_ms = elapsedMs(resize_start, resize_end);
    metrics->bgr_to_i420_ms = elapsedMs(convert_start, convert_end);
    metrics->pack_nv12_ms = elapsedMs(pack_start, end);
    metrics->total_ms = elapsedMs(start, end);
  }
  if (model_input_info != nullptr) {
    model_input_info->input_width = kModelInputWidth;
    model_input_info->input_height = kModelInputHeight;
    model_input_info->source_width = static_cast<uint32_t>(image.cols);
    model_input_info->source_height = static_cast<uint32_t>(image.rows);
    if (mode == ImageLoadMode::kSourceFrame) {
      model_input_info->pad_x = 0;
      model_input_info->pad_y = 0;
      model_input_info->scale = 1.0F;
    } else {
      model_input_info->pad_x = static_cast<uint32_t>(pad_x);
      model_input_info->pad_y = static_cast<uint32_t>(pad_y);
      model_input_info->scale = scale;
    }
  }

  return true;
}

}  // namespace mt
