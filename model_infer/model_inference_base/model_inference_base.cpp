#include "model_inference_base.hpp"

#include <cstring>
#include <sstream>
#include <utility>

#include "dnn/hb_sys.h"

namespace rdk {
namespace {

bool hasSysMem(const hbSysMem& mem) {
  return mem.virAddr != nullptr || mem.phyAddr != 0 || mem.memSize != 0;
}

}  // namespace

ModelInferenceBase::ModelInferenceBase(Config config)
    : config_(std::move(config)) {}

ModelInferenceBase::~ModelInferenceBase() { release(); }

bool ModelInferenceBase::initialize() {
  if (initialized_) {
    return true;
  }
  if (config_.model_file.empty()) {
    setLastError("model file is empty");
    return false;
  }
  if (!loadModel() || !queryModelInfo() || !prepareInputTensors() ||
      !prepareOutputTensors()) {
    release();
    return false;
  }

  initialized_ = true;
  return true;
}

void ModelInferenceBase::release() {
  for (auto& tensor : input_tensors_) {
    releaseTensorBuffer(tensor);
  }
  for (auto& tensor : output_tensors_) {
    releaseTensorBuffer(tensor);
  }
  input_tensors_.clear();
  output_tensors_.clear();
  input_properties_.clear();
  output_properties_.clear();

  if (packed_handle_ != nullptr) {
    const int32_t ret = hbDNNRelease(packed_handle_);
    if (ret != 0) {
      setDnnError("hbDNNRelease", ret);
    }
  }
  packed_handle_ = nullptr;
  dnn_handle_ = nullptr;
  initialized_ = false;
}

bool ModelInferenceBase::isInitialized() const { return initialized_; }

bool ModelInferenceBase::infer(const void* input) {
  if (!initialized_ && !initialize()) {
    return false;
  }
  if (!preprocess(input, input_tensors_)) {
    if (last_error_.empty()) {
      setLastError("preprocess failed");
    }
    return false;
  }
  if (!execute()) {
    return false;
  }
  if (!postprocess(output_tensors_)) {
    if (last_error_.empty()) {
      setLastError("postprocess failed");
    }
    return false;
  }
  return true;
}

const ModelInferenceBase::Config& ModelInferenceBase::config() const {
  return config_;
}

const std::string& ModelInferenceBase::modelName() const {
  return resolved_model_name_;
}

const std::string& ModelInferenceBase::lastError() const { return last_error_; }

const std::vector<hbDNNTensor>& ModelInferenceBase::inputTensors() const {
  return input_tensors_;
}

const std::vector<hbDNNTensor>& ModelInferenceBase::outputTensors() const {
  return output_tensors_;
}

const std::vector<hbDNNTensorProperties>&
ModelInferenceBase::inputProperties() const {
  return input_properties_;
}

const std::vector<hbDNNTensorProperties>&
ModelInferenceBase::outputProperties() const {
  return output_properties_;
}

bool ModelInferenceBase::prepareInputTensors() {
  input_tensors_.resize(input_properties_.size());
  for (size_t i = 0; i < input_properties_.size(); ++i) {
    std::memset(&input_tensors_[i], 0, sizeof(hbDNNTensor));
    input_tensors_[i].properties = input_properties_[i];
    if (!allocateTensorBuffer(input_tensors_[i])) {
      return false;
    }
  }
  return true;
}

bool ModelInferenceBase::prepareOutputTensors() {
  output_tensors_.resize(output_properties_.size());
  for (size_t i = 0; i < output_properties_.size(); ++i) {
    std::memset(&output_tensors_[i], 0, sizeof(hbDNNTensor));
    output_tensors_[i].properties = output_properties_[i];
    if (!allocateTensorBuffer(output_tensors_[i])) {
      return false;
    }
  }
  return true;
}

bool ModelInferenceBase::execute() {
  hbDNNInferCtrlParam infer_ctrl_param;
  HB_DNN_INITIALIZE_INFER_CTRL_PARAM(&infer_ctrl_param);
  infer_ctrl_param.bpuCoreId = config_.bpu_core_id;
  infer_ctrl_param.dspCoreId = config_.dsp_core_id;
  infer_ctrl_param.priority = config_.priority;

  hbDNNTaskHandle_t task_handle = nullptr;
  hbDNNTensor* output = output_tensors_.data();
  int32_t ret = hbDNNInfer(&task_handle, &output, input_tensors_.data(),
                           dnn_handle_, &infer_ctrl_param);
  if (ret != 0) {
    setDnnError("hbDNNInfer", ret);
    releaseTask(task_handle);
    return false;
  }

  ret = hbDNNWaitTaskDone(task_handle, config_.timeout_ms);
  if (ret != 0) {
    setDnnError("hbDNNWaitTaskDone", ret);
    releaseTask(task_handle);
    return false;
  }

  for (size_t i = 0; i < output_tensors_.size(); ++i) {
    if (!invalidateOutputTensor(i)) {
      releaseTask(task_handle);
      return false;
    }
  }

  releaseTask(task_handle);
  return true;
}

bool ModelInferenceBase::allocateTensorBuffer(hbDNNTensor& tensor) const {
  const size_t bytes = alignedByteSize(tensor.properties);
  if (bytes == 0) {
    setLastError("tensor aligned byte size is 0");
    return false;
  }
  const int32_t ret =
      hbSysAllocCachedMem(&tensor.sysMem[0], static_cast<uint32_t>(bytes));
  if (ret != 0) {
    std::ostringstream oss;
    oss << "hbSysAllocCachedMem failed, code=" << ret << ", bytes=" << bytes;
    setLastError(oss.str());
    return false;
  }
  return true;
}

void ModelInferenceBase::releaseTensorBuffer(hbDNNTensor& tensor) const {
  for (auto& mem : tensor.sysMem) {
    if (hasSysMem(mem)) {
      hbSysFreeMem(&mem);
      std::memset(&mem, 0, sizeof(hbSysMem));
    }
  }
}

bool ModelInferenceBase::flushTensor(hbDNNTensor& tensor, int32_t flag) const {
  for (auto& mem : tensor.sysMem) {
    if (!hasSysMem(mem)) {
      continue;
    }
    const int32_t ret = hbSysFlushMem(&mem, flag);
    if (ret != 0) {
      std::ostringstream oss;
      oss << "hbSysFlushMem failed, code=" << ret << ", flag=" << flag;
      setLastError(oss.str());
      return false;
    }
  }
  return true;
}

bool ModelInferenceBase::flushInputTensor(size_t index) const {
  if (index >= input_tensors_.size()) {
    setLastError("input tensor index out of range");
    return false;
  }
  return flushTensor(const_cast<hbDNNTensor&>(input_tensors_[index]),
                     HB_SYS_MEM_CACHE_CLEAN);
}

bool ModelInferenceBase::invalidateOutputTensor(size_t index) const {
  if (index >= output_tensors_.size()) {
    setLastError("output tensor index out of range");
    return false;
  }
  return flushTensor(const_cast<hbDNNTensor&>(output_tensors_[index]),
                     HB_SYS_MEM_CACHE_INVALIDATE);
}

bool ModelInferenceBase::copyToInputTensor(size_t index, const void* data,
                                           size_t bytes) {
  if (index >= input_tensors_.size()) {
    setLastError("input tensor index out of range");
    return false;
  }
  if (data == nullptr) {
    setLastError("input data is null");
    return false;
  }

  hbDNNTensor& tensor = input_tensors_[index];
  const size_t capacity = tensor.sysMem[0].memSize;
  if (bytes > capacity) {
    std::ostringstream oss;
    oss << "input data is larger than tensor buffer, bytes=" << bytes
        << ", capacity=" << capacity;
    setLastError(oss.str());
    return false;
  }

  std::memcpy(tensor.sysMem[0].virAddr, data, bytes);
  return flushInputTensor(index);
}

size_t ModelInferenceBase::alignedByteSize(
    const hbDNNTensorProperties& properties) {
  return static_cast<size_t>(properties.alignedByteSize);
}

std::string ModelInferenceBase::shapeToString(const hbDNNTensorShape& shape) {
  std::ostringstream oss;
  oss << "[";
  for (int32_t i = 0; i < shape.numDimensions; ++i) {
    if (i != 0) {
      oss << ", ";
    }
    oss << shape.dimensionSize[i];
  }
  oss << "]";
  return oss.str();
}

void ModelInferenceBase::setLastError(std::string message) const {
  last_error_ = std::move(message);
}

void ModelInferenceBase::setDnnError(const std::string& api,
                                     int32_t code) const {
  std::ostringstream oss;
  oss << api << " failed, code=" << code;
  const char* desc = hbDNNGetErrorDesc(code);
  if (desc != nullptr) {
    oss << ", desc=" << desc;
  }
  setLastError(oss.str());
}

bool ModelInferenceBase::loadModel() {
  const char* model_file = config_.model_file.c_str();
  int32_t ret = hbDNNInitializeFromFiles(&packed_handle_, &model_file, 1);
  if (ret != 0) {
    setDnnError("hbDNNInitializeFromFiles", ret);
    return false;
  }

  char const** model_name_list = nullptr;
  int32_t model_name_count = 0;
  ret = hbDNNGetModelNameList(&model_name_list, &model_name_count,
                              packed_handle_);
  if (ret != 0) {
    setDnnError("hbDNNGetModelNameList", ret);
    return false;
  }
  if (model_name_count <= 0 || model_name_list == nullptr) {
    setLastError("no model found in packed model file");
    return false;
  }

  resolved_model_name_ =
      config_.model_name.empty() ? model_name_list[0] : config_.model_name;
  ret = hbDNNGetModelHandle(&dnn_handle_, packed_handle_,
                            resolved_model_name_.c_str());
  if (ret != 0) {
    setDnnError("hbDNNGetModelHandle", ret);
    return false;
  }
  return true;
}

bool ModelInferenceBase::queryModelInfo() {
  int32_t input_count = 0;
  int32_t output_count = 0;
  int32_t ret = hbDNNGetInputCount(&input_count, dnn_handle_);
  if (ret != 0) {
    setDnnError("hbDNNGetInputCount", ret);
    return false;
  }
  ret = hbDNNGetOutputCount(&output_count, dnn_handle_);
  if (ret != 0) {
    setDnnError("hbDNNGetOutputCount", ret);
    return false;
  }

  input_properties_.resize(static_cast<size_t>(input_count));
  output_properties_.resize(static_cast<size_t>(output_count));
  for (int32_t i = 0; i < input_count; ++i) {
    ret = hbDNNGetInputTensorProperties(&input_properties_[i], dnn_handle_, i);
    if (ret != 0) {
      setDnnError("hbDNNGetInputTensorProperties", ret);
      return false;
    }
  }
  for (int32_t i = 0; i < output_count; ++i) {
    ret =
        hbDNNGetOutputTensorProperties(&output_properties_[i], dnn_handle_, i);
    if (ret != 0) {
      setDnnError("hbDNNGetOutputTensorProperties", ret);
      return false;
    }
  }
  return true;
}

void ModelInferenceBase::releaseTask(hbDNNTaskHandle_t task_handle) const {
  if (task_handle != nullptr) {
    const int32_t ret = hbDNNReleaseTask(task_handle);
    if (ret != 0) {
      setDnnError("hbDNNReleaseTask", ret);
    }
  }
}

}  // namespace rdk
