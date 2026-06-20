#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "protocol.hpp"

namespace mt {

struct ModelInputInfo {
  uint32_t input_width = 0;
  uint32_t input_height = 0;
  uint32_t source_width = 0;
  uint32_t source_height = 0;
  uint32_t pad_x = 0;
  uint32_t pad_y = 0;
  float scale = 1.0F;
};

struct ImageLoadMetrics {
  int64_t imread_ms = 0;
  int64_t resize_ms = 0;
  int64_t bgr_to_i420_ms = 0;
  int64_t pack_nv12_ms = 0;
  int64_t total_ms = 0;
};

enum class ImageLoadMode : uint32_t {
  kSourceFrame = 0,
  kModelInputPreprocessed = 1,
};

struct TransportFrameInfo {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t source_width = 0;
  uint32_t source_height = 0;
  uint32_t pad_x = 0;
  uint32_t pad_y = 0;
  float scale = 1.0F;
  bool preprocessed = false;
};

TransportFrameInfo BuildTransportFrameInfo(const ModelInputInfo& model_input_info,
                                           ImageLoadMode mode);

bool loadImageAsNv12(const std::string& image_path, ImageLoadMode mode,
                     std::vector<uint8_t>& nv12_buffer, std::string& error,
                     ImageLoadMetrics* metrics = nullptr,
                     ModelInputInfo* model_input_info = nullptr);

}  // namespace mt
