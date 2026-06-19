#include "yolo_infer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <sstream>

#include <opencv2/imgproc.hpp>

namespace rdk {
namespace {

constexpr uint8_t kLetterboxPadValue = 114;

bool isFloatType(int32_t tensor_type) {
  return tensor_type == HB_DNN_TENSOR_TYPE_F32 ||
         tensor_type == HB_DNN_TENSOR_TYPE_F64 ||
         tensor_type == HB_DNN_TENSOR_TYPE_F16;
}

bool isSignedType(int32_t tensor_type) {
  return tensor_type == HB_DNN_TENSOR_TYPE_S8 ||
         tensor_type == HB_DNN_TENSOR_TYPE_S16 ||
         tensor_type == HB_DNN_TENSOR_TYPE_S32 ||
         tensor_type == HB_DNN_TENSOR_TYPE_S64;
}

bool isNv12Type(int32_t tensor_type) {
  return tensor_type == HB_DNN_IMG_TYPE_NV12 ||
         tensor_type == HB_DNN_IMG_TYPE_NV12_SEPARATE;
}

bool tensorHwc(const hbDNNTensorProperties& properties, int& height, int& width,
               int& channels) {
  const auto& shape = properties.validShape;
  if (shape.numDimensions != 4) {
    return false;
  }
  if (properties.tensorLayout == HB_DNN_LAYOUT_NCHW) {
    channels = shape.dimensionSize[1];
    height = shape.dimensionSize[2];
    width = shape.dimensionSize[3];
    return true;
  }
  height = shape.dimensionSize[1];
  width = shape.dimensionSize[2];
  channels = shape.dimensionSize[3];
  return true;
}

}  // namespace

YoloInfer::YoloInfer(ModelInferenceBase::Config config)
    : ModelInferenceBase(std::move(config)) {}

YoloInfer::YoloInfer(ModelInferenceBase::Config config, Options options)
    : ModelInferenceBase(std::move(config)), options_(options) {}

bool YoloInfer::infer(const cv::Mat& image) {
  detections_.clear();
  return ModelInferenceBase::infer(&image);
}

const YoloInfer::Options& YoloInfer::options() const { return options_; }

const std::vector<YoloInfer::Detection>& YoloInfer::detections() const {
  return detections_;
}

std::string YoloInfer::dumpModelInfo() const {
  std::ostringstream oss;
  oss << "model_name=" << modelName() << "\n";
  for (size_t i = 0; i < inputProperties().size(); ++i) {
    const auto& prop = inputProperties()[i];
    oss << "input[" << i << "] valid=" << shapeToString(prop.validShape)
        << ", aligned=" << shapeToString(prop.alignedShape)
        << ", layout=" << prop.tensorLayout << ", type=" << prop.tensorType
        << ", quanti=" << prop.quantiType
        << ", aligned_bytes=" << prop.alignedByteSize << "\n";
  }
  for (size_t i = 0; i < outputProperties().size(); ++i) {
    const auto& prop = outputProperties()[i];
    oss << "output[" << i << "] valid=" << shapeToString(prop.validShape)
        << ", aligned=" << shapeToString(prop.alignedShape)
        << ", layout=" << prop.tensorLayout << ", type=" << prop.tensorType
        << ", quanti=" << prop.quantiType
        << ", aligned_bytes=" << prop.alignedByteSize << "\n";
  }
  return oss.str();
}

bool YoloInfer::preprocess(const void* input, TensorList& input_tensors) {
  if (input == nullptr) {
    setLastError("YoloInfer preprocess input is null");
    return false;
  }
  if (input_tensors.empty()) {
    setLastError("YoloInfer requires at least one input tensor");
    return false;
  }

  const auto& image = *static_cast<const cv::Mat*>(input);
  if (image.empty()) {
    setLastError("input image is empty");
    return false;
  }

  hbDNNTensor& tensor = input_tensors[0];
  const auto& shape = tensor.properties.validShape;
  int input_h = options_.input_height;
  int input_w = options_.input_width;
  if (isNv12Type(tensor.properties.tensorType) && shape.numDimensions >= 4) {
    if (tensor.properties.tensorLayout == HB_DNN_LAYOUT_NCHW) {
      input_h = dimension(shape, 2);
      input_w = dimension(shape, 3);
    } else {
      input_h = dimension(shape, 1);
      input_w = dimension(shape, 2);
    }
  } else if (shape.numDimensions == 4) {
    if (tensor.properties.tensorLayout == HB_DNN_LAYOUT_NCHW) {
      input_h = dimension(shape, 2);
      input_w = dimension(shape, 3);
    } else {
      input_h = dimension(shape, 1);
      input_w = dimension(shape, 2);
    }
  }
  options_.input_height = input_h;
  options_.input_width = input_w;

  letterbox_.source_width = image.cols;
  letterbox_.source_height = image.rows;

  cv::Mat resized;
  if (options_.keep_ratio) {
    const float scale =
        std::min(static_cast<float>(input_w) / static_cast<float>(image.cols),
                 static_cast<float>(input_h) / static_cast<float>(image.rows));
    const int resized_w = static_cast<int>(std::round(image.cols * scale));
    const int resized_h = static_cast<int>(std::round(image.rows * scale));
    letterbox_.scale = scale;
    letterbox_.pad_x = (input_w - resized_w) / 2;
    letterbox_.pad_y = (input_h - resized_h) / 2;

    cv::Mat tmp;
    cv::resize(image, tmp, cv::Size(resized_w, resized_h), 0, 0,
               cv::INTER_LINEAR);
    resized = cv::Mat(input_h, input_w, image.type(),
                      cv::Scalar(kLetterboxPadValue, kLetterboxPadValue,
                                 kLetterboxPadValue));
    tmp.copyTo(resized(cv::Rect(letterbox_.pad_x, letterbox_.pad_y, resized_w,
                                resized_h)));
  } else {
    letterbox_.scale =
        static_cast<float>(input_w) / static_cast<float>(image.cols);
    letterbox_.pad_x = 0;
    letterbox_.pad_y = 0;
    cv::resize(image, resized, cv::Size(input_w, input_h), 0, 0,
               cv::INTER_LINEAR);
  }

  if (letterbox_.scale <= 0.0F) {
    setLastError("invalid letterbox scale");
    return false;
  }

  if (isNv12Type(tensor.properties.tensorType)) {
    cv::Mat nv12 = bgrToNv12(resized);
    const size_t bytes = static_cast<size_t>(input_h) * input_w * 3 / 2;
    if (tensor.properties.tensorType == HB_DNN_IMG_TYPE_NV12_SEPARATE) {
      const size_t y_bytes = static_cast<size_t>(input_h) * input_w;
      const size_t uv_bytes = y_bytes / 2;
      if (tensor.sysMem[0].memSize < y_bytes || tensor.sysMem[1].memSize < uv_bytes) {
        setLastError("NV12 separate tensor buffer is smaller than expected");
        return false;
      }
      std::memcpy(tensor.sysMem[0].virAddr, nv12.data, y_bytes);
      std::memcpy(tensor.sysMem[1].virAddr, nv12.data + y_bytes, uv_bytes);
      if (!flushTensor(tensor, HB_SYS_MEM_CACHE_CLEAN)) {
        return false;
      }
      return true;
    }
    return copyToInputTensor(0, nv12.data, bytes);
  }

  cv::Mat rgb;
  if (resized.channels() == 3) {
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
  } else if (resized.channels() == 1) {
    cv::cvtColor(resized, rgb, cv::COLOR_GRAY2RGB);
  } else {
    setLastError("unsupported image channel count");
    return false;
  }

  const bool nchw = tensor.properties.tensorLayout == HB_DNN_LAYOUT_NCHW;
  const int channels = 3;
  const size_t element_count =
      static_cast<size_t>(channels) * input_h * input_w;

  if (isFloatType(tensor.properties.tensorType)) {
    std::vector<float> buffer(element_count);
    if (nchw) {
      for (int c = 0; c < channels; ++c) {
        for (int y = 0; y < input_h; ++y) {
          const auto* row = rgb.ptr<cv::Vec3b>(y);
          for (int x = 0; x < input_w; ++x) {
            buffer[static_cast<size_t>(c) * input_h * input_w +
                   static_cast<size_t>(y) * input_w + x] =
                static_cast<float>(row[x][c]);
          }
        }
      }
    } else {
      for (int y = 0; y < input_h; ++y) {
        const auto* row = rgb.ptr<cv::Vec3b>(y);
        for (int x = 0; x < input_w; ++x) {
          for (int c = 0; c < channels; ++c) {
            buffer[(static_cast<size_t>(y) * input_w + x) * channels + c] =
                static_cast<float>(row[x][c]);
          }
        }
      }
    }
    return copyToInputTensor(0, buffer.data(),
                             buffer.size() * sizeof(buffer.front()));
  }

  std::vector<uint8_t> buffer(element_count);
  if (nchw) {
    for (int c = 0; c < channels; ++c) {
      for (int y = 0; y < input_h; ++y) {
        const auto* row = rgb.ptr<cv::Vec3b>(y);
        for (int x = 0; x < input_w; ++x) {
          buffer[static_cast<size_t>(c) * input_h * input_w +
                 static_cast<size_t>(y) * input_w + x] = row[x][c];
        }
      }
    }
  } else {
    if (rgb.isContinuous()) {
      std::memcpy(buffer.data(), rgb.data, buffer.size());
    } else {
      for (int y = 0; y < input_h; ++y) {
        std::memcpy(buffer.data() + static_cast<size_t>(y) * input_w * channels,
                    rgb.ptr(y), static_cast<size_t>(input_w) * channels);
      }
    }
  }
  return copyToInputTensor(0, buffer.data(), buffer.size());
}

bool YoloInfer::postprocess(const TensorList& output_tensors) {
  std::vector<Detection> proposals;
  if (isMultiScaleHead(output_tensors)) {
    if (!parseMultiScaleOutputs(output_tensors, proposals)) {
      return false;
    }
  } else {
    for (const auto& tensor : output_tensors) {
      if (!parseYoloOutput(tensor, proposals)) {
        return false;
      }
    }
  }

  applyNms(proposals);
  detections_ = std::move(proposals);
  return true;
}

float YoloInfer::readOutputValue(const hbDNNTensor& tensor, size_t flat_index,
                                 int channel) const {
  const auto& prop = tensor.properties;
  float value = 0.0F;

  switch (prop.tensorType) {
    case HB_DNN_TENSOR_TYPE_F32:
      value = tensorData<const float>(tensor)[flat_index];
      break;
    case HB_DNN_TENSOR_TYPE_F64:
      value = static_cast<float>(tensorData<const double>(tensor)[flat_index]);
      break;
    case HB_DNN_TENSOR_TYPE_F16:
      value = static_cast<float>(tensorData<const cv::float16_t>(tensor)[flat_index]);
      break;
    case HB_DNN_TENSOR_TYPE_S8:
      value = static_cast<float>(tensorData<const int8_t>(tensor)[flat_index]);
      break;
    case HB_DNN_TENSOR_TYPE_U8:
    case HB_DNN_IMG_TYPE_RGB:
    case HB_DNN_IMG_TYPE_BGR:
    case HB_DNN_IMG_TYPE_Y:
      value = static_cast<float>(tensorData<const uint8_t>(tensor)[flat_index]);
      break;
    case HB_DNN_TENSOR_TYPE_S16:
      value = static_cast<float>(tensorData<const int16_t>(tensor)[flat_index]);
      break;
    case HB_DNN_TENSOR_TYPE_U16:
      value =
          static_cast<float>(tensorData<const uint16_t>(tensor)[flat_index]);
      break;
    case HB_DNN_TENSOR_TYPE_S32:
      value = static_cast<float>(tensorData<const int32_t>(tensor)[flat_index]);
      break;
    case HB_DNN_TENSOR_TYPE_U32:
      value =
          static_cast<float>(tensorData<const uint32_t>(tensor)[flat_index]);
      break;
    default:
      value = static_cast<float>(tensorData<const uint8_t>(tensor)[flat_index]);
      break;
  }

  if (prop.quantiType == SCALE && prop.scale.scaleData != nullptr &&
      !isFloatType(prop.tensorType)) {
    int scale_index = 0;
    if (prop.scale.scaleLen > 1) {
      scale_index = std::max(0, std::min(channel, prop.scale.scaleLen - 1));
    }
    const float scale = prop.scale.scaleData[scale_index];
    const float zero_point =
        (prop.scale.zeroPointLen > scale_index &&
         prop.scale.zeroPointData != nullptr)
            ? static_cast<float>(prop.scale.zeroPointData[scale_index])
            : 0.0F;
    value = (value - zero_point) * scale;
  } else if (prop.quantiType == SHIFT && prop.shift.shiftData != nullptr &&
             !isFloatType(prop.tensorType)) {
    int shift_index = 0;
    if (prop.shift.shiftLen > 1) {
      shift_index = std::max(0, std::min(channel, prop.shift.shiftLen - 1));
    }
    value = std::ldexp(value, -static_cast<int>(prop.shift.shiftData[shift_index]));
  }

  return value;
}

bool YoloInfer::parseYoloOutput(const hbDNNTensor& tensor,
                                std::vector<Detection>& proposals) const {
  const auto& shape = tensor.properties.validShape;
  if (shape.numDimensions < 3) {
    setLastError("unsupported YOLO output shape: " + shapeToString(shape));
    return false;
  }

  const int expected_attrs = options_.num_classes + 4;
  const int expected_attrs_with_obj = options_.num_classes + 5;
  int attrs = 0;
  int boxes = 0;
  bool channel_first = false;

  if (shape.numDimensions == 3) {
    const int dim1 = dimension(shape, 1);
    const int dim2 = dimension(shape, 2);
    if (dim1 == expected_attrs || dim1 == expected_attrs_with_obj) {
      attrs = dim1;
      boxes = dim2;
      channel_first = true;
    } else if (dim2 == expected_attrs || dim2 == expected_attrs_with_obj) {
      attrs = dim2;
      boxes = dim1;
      channel_first = false;
    }
  }

  if (attrs == 0 || boxes == 0) {
    setLastError("unsupported YOLO decoded output shape: " +
                 shapeToString(shape));
    return false;
  }

  for (int i = 0; i < boxes; ++i) {
    auto value_at = [&](int attr) {
      const size_t index =
          channel_first
              ? static_cast<size_t>(attr) * boxes + i
              : static_cast<size_t>(i) * attrs + attr;
      return readOutputValue(tensor, index, attr);
    };

    const float cx = value_at(0);
    const float cy = value_at(1);
    const float width = value_at(2);
    const float height = value_at(3);
    const bool has_objectness = attrs == expected_attrs_with_obj;
    const float objectness = has_objectness ? value_at(4) : 1.0F;
    const int class_start = has_objectness ? 5 : 4;

    int best_class = -1;
    float best_score = 0.0F;
    for (int c = 0; c < options_.num_classes; ++c) {
      const float score = objectness * value_at(class_start + c);
      if (score > best_score) {
        best_score = score;
        best_class = c;
      }
    }

    if (best_score < options_.score_threshold) {
      continue;
    }

    Detection detection;
    detection.class_id = best_class;
    detection.score = best_score;
    detection.box = restoreBox(cx, cy, width, height);
    proposals.push_back(detection);
  }

  return true;
}

bool YoloInfer::parseMultiScaleOutputs(
    const TensorList& output_tensors, std::vector<Detection>& proposals) const {
  struct HeadDesc {
    int cls_index = -1;
    int reg_index = -1;
    int stride = 0;
    int height = 0;
    int width = 0;
  };

  std::vector<HeadDesc> heads;
  heads.reserve(3);

  for (size_t i = 0; i < output_tensors.size(); ++i) {
    const auto& tensor = output_tensors[i];
    int h = 0;
    int w = 0;
    int c = 0;
    if (!tensorHwc(tensor.properties, h, w, c)) {
      continue;
    }

    auto it = std::find_if(heads.begin(), heads.end(), [&](const HeadDesc& head) {
      return head.height == h && head.width == w;
    });
    if (it == heads.end()) {
      heads.push_back(HeadDesc{});
      it = std::prev(heads.end());
      it->height = h;
      it->width = w;
      if (h > 0) {
        it->stride = options_.input_height / h;
      }
    }

    if (c == options_.num_classes) {
      it->cls_index = static_cast<int>(i);
    } else if (c == options_.reg * 4) {
      it->reg_index = static_cast<int>(i);
    }
  }

  std::sort(heads.begin(), heads.end(),
            [](const HeadDesc& lhs, const HeadDesc& rhs) {
              return lhs.stride < rhs.stride;
            });

  if (heads.size() != 3) {
    std::ostringstream oss;
    oss << "expected 3 output scales, got " << heads.size();
    setLastError(oss.str());
    return false;
  }

  for (const auto& head : heads) {
    if (head.cls_index < 0 || head.reg_index < 0 || head.stride <= 0) {
      setLastError("failed to match multi-scale cls/reg output pair");
      return false;
    }
    if (!parseScalePair(output_tensors[head.cls_index],
                        output_tensors[head.reg_index], head.stride,
                        proposals)) {
      return false;
    }
  }

  return true;
}

bool YoloInfer::parseScalePair(const hbDNNTensor& cls_tensor,
                               const hbDNNTensor& reg_tensor, int stride,
                               std::vector<Detection>& proposals) const {
  const auto& cls_shape = cls_tensor.properties.validShape;
  const auto& reg_shape = reg_tensor.properties.validShape;
  if (cls_shape.numDimensions != 4 || reg_shape.numDimensions != 4) {
    setLastError("multi-scale output must be 4D tensor");
    return false;
  }

  int h = 0;
  int w = 0;
  int cls_c = 0;
  int reg_h = 0;
  int reg_w = 0;
  int reg_c = 0;
  if (!tensorHwc(cls_tensor.properties, h, w, cls_c) ||
      !tensorHwc(reg_tensor.properties, reg_h, reg_w, reg_c)) {
    setLastError("failed to parse multi-scale output layout");
    return false;
  }
  if (cls_c != options_.num_classes || reg_c != options_.reg * 4 ||
      reg_h != h || reg_w != w) {
    setLastError("multi-scale cls/reg output shapes do not match");
    return false;
  }

  const float conf_thres_raw =
      -std::log(1.0F / options_.score_threshold - 1.0F);

  for (int gh = 0; gh < h; ++gh) {
    for (int gw = 0; gw < w; ++gw) {
      int best_class = 0;
      float best_logit = readOutputValue(
          cls_tensor, tensorOffset(cls_tensor, 0, gh, gw, 0), 0);
      for (int c = 1; c < options_.num_classes; ++c) {
        const float logit = readOutputValue(
            cls_tensor, tensorOffset(cls_tensor, 0, gh, gw, c), c);
        if (logit > best_logit) {
          best_logit = logit;
          best_class = c;
        }
      }

      if (best_logit < conf_thres_raw) {
        continue;
      }

      float ltrb[4] = {0.0F, 0.0F, 0.0F, 0.0F};
      for (int side = 0; side < 4; ++side) {
        float max_logit = -std::numeric_limits<float>::infinity();
        for (int bin = 0; bin < options_.reg; ++bin) {
          const int channel = side * options_.reg + bin;
          const float value = readOutputValue(
              reg_tensor, tensorOffset(reg_tensor, 0, gh, gw, channel), channel);
          if (value > max_logit) {
            max_logit = value;
          }
        }

        float sum = 0.0F;
        for (int bin = 0; bin < options_.reg; ++bin) {
          const int channel = side * options_.reg + bin;
          const float value = readOutputValue(
              reg_tensor, tensorOffset(reg_tensor, 0, gh, gw, channel), channel);
          const float exp_value = std::exp(value - max_logit);
          ltrb[side] += exp_value * static_cast<float>(bin);
          sum += exp_value;
        }
        if (sum <= 0.0F) {
          ltrb[side] = 0.0F;
        } else {
          ltrb[side] /= sum;
        }
      }

      if (ltrb[0] + ltrb[2] <= 0.0F || ltrb[1] + ltrb[3] <= 0.0F) {
        continue;
      }

      const float score = sigmoid(best_logit);
      const float center_x = (static_cast<float>(gw) + 0.5F) * stride;
      const float center_y = (static_cast<float>(gh) + 0.5F) * stride;

      Detection detection;
      detection.class_id = best_class;
      detection.score = score;
      detection.box =
          restoreBoxFromCorners(center_x - ltrb[0] * stride,
                                center_y - ltrb[1] * stride,
                                center_x + ltrb[2] * stride,
                                center_y + ltrb[3] * stride);
      proposals.push_back(detection);
    }
  }

  return true;
}

bool YoloInfer::isMultiScaleHead(const TensorList& output_tensors) const {
  if (output_tensors.size() != 6) {
    return false;
  }

  int cls_count = 0;
  int reg_count = 0;
  for (const auto& tensor : output_tensors) {
    int h = 0;
    int w = 0;
    int channels = 0;
    if (!tensorHwc(tensor.properties, h, w, channels)) {
      return false;
    }
    if (channels == options_.num_classes) {
      ++cls_count;
    } else if (channels == options_.reg * 4) {
      ++reg_count;
    } else {
      return false;
    }
  }

  return cls_count == 3 && reg_count == 3;
}

size_t YoloInfer::tensorOffset(const hbDNNTensor& tensor, int n, int h, int w,
                               int c) const {
  const auto& shape = tensor.properties.validShape;
  const auto& aligned = tensor.properties.alignedShape;
  const bool nchw = tensor.properties.tensorLayout == HB_DNN_LAYOUT_NCHW;
  if (shape.numDimensions != 4) {
    return 0;
  }

  if (nchw) {
    const size_t channels = static_cast<size_t>(dimension(aligned, 1));
    const size_t height = static_cast<size_t>(dimension(aligned, 2));
    const size_t width = static_cast<size_t>(dimension(aligned, 3));
    return ((static_cast<size_t>(n) * channels + static_cast<size_t>(c)) *
                height +
            static_cast<size_t>(h)) *
               width +
           static_cast<size_t>(w);
  }

  const size_t height = static_cast<size_t>(dimension(aligned, 1));
  const size_t width = static_cast<size_t>(dimension(aligned, 2));
  const size_t channels = static_cast<size_t>(dimension(aligned, 3));
  return ((static_cast<size_t>(n) * height + static_cast<size_t>(h)) * width +
          static_cast<size_t>(w)) *
             channels +
         static_cast<size_t>(c);
}

cv::Mat YoloInfer::bgrToNv12(const cv::Mat& bgr) {
  cv::Mat yuv_i420;
  cv::cvtColor(bgr, yuv_i420, cv::COLOR_BGR2YUV_I420);

  const int height = bgr.rows;
  const int width = bgr.cols;
  const int y_size = height * width;
  const int uv_plane_size = y_size / 4;

  cv::Mat nv12(height * 3 / 2, width, CV_8UC1);
  std::memcpy(nv12.data, yuv_i420.data, static_cast<size_t>(y_size));

  const uint8_t* u_plane = yuv_i420.data + y_size;
  const uint8_t* v_plane = u_plane + uv_plane_size;
  uint8_t* uv = nv12.data + y_size;
  for (int i = 0; i < uv_plane_size; ++i) {
    *uv++ = u_plane[i];
    *uv++ = v_plane[i];
  }

  return nv12;
}

float YoloInfer::sigmoid(float value) {
  if (value >= 0.0F) {
    const float exp_neg = std::exp(-value);
    return 1.0F / (1.0F + exp_neg);
  }
  const float exp_pos = std::exp(value);
  return exp_pos / (1.0F + exp_pos);
}

void YoloInfer::applyNms(std::vector<Detection>& proposals) {
  std::sort(proposals.begin(), proposals.end(),
            [](const Detection& lhs, const Detection& rhs) {
              return lhs.score > rhs.score;
            });

  std::vector<Detection> kept;
  std::vector<bool> removed(proposals.size(), false);
  for (size_t i = 0; i < proposals.size(); ++i) {
    if (removed[i]) {
      continue;
    }
    kept.push_back(proposals[i]);
    for (size_t j = i + 1; j < proposals.size(); ++j) {
      if (removed[j] || proposals[i].class_id != proposals[j].class_id) {
        continue;
      }
      if (intersectionOverUnion(proposals[i].box, proposals[j].box) >
          options_.nms_threshold) {
        removed[j] = true;
      }
    }
  }
  proposals = std::move(kept);
}

cv::Rect2f YoloInfer::restoreBox(float cx, float cy, float width,
                                 float height) const {
  return restoreBoxFromCorners(cx - width * 0.5F, cy - height * 0.5F,
                               cx + width * 0.5F, cy + height * 0.5F);
}

cv::Rect2f YoloInfer::restoreBoxFromCorners(float x1, float y1, float x2,
                                            float y2) const {
  if (letterbox_.scale <= 0.0F) {
    return cv::Rect2f();
  }

  x1 = (x1 - static_cast<float>(letterbox_.pad_x)) / letterbox_.scale;
  y1 = (y1 - static_cast<float>(letterbox_.pad_y)) / letterbox_.scale;
  x2 = (x2 - static_cast<float>(letterbox_.pad_x)) / letterbox_.scale;
  y2 = (y2 - static_cast<float>(letterbox_.pad_y)) / letterbox_.scale;

  x1 = std::max(0.0F, std::min(x1, static_cast<float>(letterbox_.source_width)));
  y1 =
      std::max(0.0F, std::min(y1, static_cast<float>(letterbox_.source_height)));
  x2 = std::max(0.0F, std::min(x2, static_cast<float>(letterbox_.source_width)));
  y2 =
      std::max(0.0F, std::min(y2, static_cast<float>(letterbox_.source_height)));

  return cv::Rect2f(cv::Point2f(x1, y1),
                    cv::Point2f(std::max(x1, x2), std::max(y1, y2)));
}

float YoloInfer::intersectionOverUnion(const cv::Rect2f& lhs,
                                       const cv::Rect2f& rhs) {
  const float inter_area = (lhs & rhs).area();
  const float union_area = lhs.area() + rhs.area() - inter_area;
  return union_area <= 0.0F ? 0.0F : inter_area / union_area;
}

int YoloInfer::elementSize(int32_t tensor_type) {
  switch (tensor_type) {
    case HB_DNN_TENSOR_TYPE_F64:
    case HB_DNN_TENSOR_TYPE_S64:
    case HB_DNN_TENSOR_TYPE_U64:
      return 8;
    case HB_DNN_TENSOR_TYPE_F32:
    case HB_DNN_TENSOR_TYPE_S32:
    case HB_DNN_TENSOR_TYPE_U32:
      return 4;
    case HB_DNN_TENSOR_TYPE_F16:
    case HB_DNN_TENSOR_TYPE_S16:
    case HB_DNN_TENSOR_TYPE_U16:
      return 2;
    default:
      return 1;
  }
}

size_t YoloInfer::shapeElementCount(const hbDNNTensorShape& shape) {
  if (shape.numDimensions <= 0) {
    return 0;
  }
  size_t count = 1;
  for (int i = 0; i < shape.numDimensions; ++i) {
    count *= static_cast<size_t>(std::max(1, shape.dimensionSize[i]));
  }
  return count;
}

int YoloInfer::dimension(const hbDNNTensorShape& shape, int index) {
  if (index < 0 || index >= shape.numDimensions) {
    return 0;
  }
  return shape.dimensionSize[index];
}

}  // namespace rdk
