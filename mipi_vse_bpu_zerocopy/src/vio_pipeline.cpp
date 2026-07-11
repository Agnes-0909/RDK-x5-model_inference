#include "vio_pipeline.hpp"

#include <cstring>
#include <memory>
#include <sstream>
#include <utility>

#include "hb_camera_interface.h"
#include "hbn_api.h"
#include "vse_cfg.h"
#include "vp_sensors.h"

namespace zerocopy {
namespace {

struct Pipeline {
  int host = -1;
  camera_config_t camera{};
  mipi_config_t mipi{};
  vin_node_attr_t vin_node_attr{};
  vin_ichn_attr_t vin_ichn_attr{};
  vin_ochn_attr_t vin_ochn_attr{};
  vin_attr_ex_t vin_attr_ex{};
  isp_attr_t isp_attr{};
  isp_ichn_attr_t isp_ichn_attr{};
  isp_ochn_attr_t isp_ochn_attr{};
  camera_handle_t camera_handle = -1;
  hbn_vnode_handle_t vin = 0;
  hbn_vnode_handle_t isp = 0;
  hbn_vnode_handle_t vse = 0;
  hbn_vflow_handle_t flow = 0;
  bool camera_created = false;
  bool vin_created = false;
  bool isp_created = false;
  bool vse_created = false;
  bool flow_created = false;
  bool flow_started = false;
};

void cloneSensorConfig(Pipeline& pipeline, int host) {
  const vp_sensor_config_t& source =
      sc132gs_linear_1088x1280_raw10_30fps_1lane;
  pipeline.host = host;
  pipeline.camera = *source.camera_config;
  pipeline.mipi = *source.camera_config->mipi_cfg;
  pipeline.camera.mipi_cfg = &pipeline.mipi;
  pipeline.vin_node_attr = *source.vin_node_attr;
  pipeline.vin_ichn_attr = *source.vin_ichn_attr;
  pipeline.vin_ochn_attr = *source.vin_ochn_attr;
  pipeline.vin_attr_ex = *source.vin_attr_ex;
  pipeline.isp_attr = *source.isp_attr;
  pipeline.isp_ichn_attr = *source.isp_ichn_attr;
  pipeline.isp_ochn_attr = *source.isp_ochn_attr;
  pipeline.mipi.rx_attr.phy = host;
  pipeline.vin_node_attr.cim_attr.mipi_rx = host;
  pipeline.vin_node_attr.cim_attr.func.ts_src = host + 1;
  pipeline.vin_node_attr.lpwm_attr.enable = 0;
  pipeline.isp_attr.input_mode = 2;
}

BufferLayout makeLayout(const hbn_vnode_image_t& image) {
  BufferLayout layout;
  layout.width = image.buffer.width;
  layout.height = image.buffer.height;
  layout.stride_y = image.buffer.stride;
  layout.stride_uv = image.buffer.stride;
  layout.y_bytes = static_cast<std::uint32_t>(image.buffer.size[0]);
  layout.uv_bytes = static_cast<std::uint32_t>(image.buffer.size[1]);
  layout.y_phy = image.buffer.phys_addr[0];
  layout.uv_phy = image.buffer.phys_addr[1];
  layout.y_virt = image.buffer.virt_addr[0];
  layout.uv_virt = image.buffer.virt_addr[1];
  layout.plane_count = image.buffer.plane_cnt;
  layout.format = image.buffer.format == FRM_FMT_NV12 ? PixelFormat::Nv12
                                                     : PixelFormat::Unknown;
  return layout;
}

}  // namespace

class DualVioPipeline::Impl {
 public:
  Impl(int left_host, int right_host)
      : left_host_(left_host), right_host_(right_host) {}

  ~Impl() { stop(); }

  bool initialize() {
    if (initialized_) return true;
    cloneSensorConfig(right_, right_host_);
    if (!initializeOne(right_)) {
      stop();
      return false;
    }
    cloneSensorConfig(left_, left_host_);
    if (!initializeOne(left_)) {
      stop();
      return false;
    }
    initialized_ = true;
    return true;
  }

  bool acquire(Pipeline& pipeline, int timeout_ms, VseFrame& frame) {
    hbn_vnode_image_t image{};
    const int ret = hbn_vnode_getframe(pipeline.vse, 0, timeout_ms, &image);
    if (ret != 0) {
      std::ostringstream message;
      message << "hbn_vnode_getframe(host=" << pipeline.host
              << ") failed with code " << ret;
      last_error_ = message.str();
      return false;
    }
    auto owned_image = std::make_shared<hbn_vnode_image_t>(image);
    const hbn_vnode_handle_t handle = pipeline.vse;
    frame = VseFrame(
        image.info.frame_id, makeLayout(image), [handle, owned_image] {
          return hbn_vnode_releaseframe(handle, 0, owned_image.get());
        });
    last_error_.clear();
    return true;
  }

  bool acquireLeft(int timeout_ms, VseFrame& frame) {
    return acquire(left_, timeout_ms, frame);
  }
  bool acquireRight(int timeout_ms, VseFrame& frame) {
    return acquire(right_, timeout_ms, frame);
  }

  void stop() noexcept {
    stopOne(left_);
    stopOne(right_);
    initialized_ = false;
  }

  const std::string& lastError() const { return last_error_; }

 private:
  bool call(const char* operation, int ret, int host) {
    if (ret == 0) return true;
    std::ostringstream message;
    message << operation << "(host=" << host << ") failed with code " << ret;
    last_error_ = message.str();
    return false;
  }

  bool initializeOne(Pipeline& pipeline) {
    int ret = hbn_camera_create(&pipeline.camera, &pipeline.camera_handle);
    if (!call("hbn_camera_create", ret, pipeline.host)) return false;
    pipeline.camera_created = true;

    ret = hbn_vnode_open(HB_VIN, pipeline.host, AUTO_ALLOC_ID, &pipeline.vin);
    if (!call("hbn_vnode_open(VIN)", ret, pipeline.host)) return false;
    pipeline.vin_created = true;
    if (!call("hbn_vnode_set_attr(VIN)",
              hbn_vnode_set_attr(pipeline.vin, &pipeline.vin_node_attr),
              pipeline.host) ||
        !call("hbn_vnode_set_ichn_attr(VIN)",
              hbn_vnode_set_ichn_attr(pipeline.vin, 0,
                                      &pipeline.vin_ichn_attr),
              pipeline.host) ||
        !call("hbn_vnode_set_ochn_attr(VIN)",
              hbn_vnode_set_ochn_attr(pipeline.vin, 0,
                                      &pipeline.vin_ochn_attr),
              pipeline.host)) {
      return false;
    }
    hbn_buf_alloc_attr_t vin_buffers{};
    vin_buffers.buffers_num = 3;
    vin_buffers.is_contig = 1;
    vin_buffers.flags = HB_MEM_USAGE_CPU_READ_OFTEN |
                        HB_MEM_USAGE_CPU_WRITE_OFTEN | HB_MEM_USAGE_CACHED |
                        HB_MEM_USAGE_HW_CIM |
                        HB_MEM_USAGE_GRAPHIC_CONTIGUOUS_BUF;
    if (!call("hbn_vnode_set_ochn_buf_attr(VIN)",
              hbn_vnode_set_ochn_buf_attr(pipeline.vin, 0, &vin_buffers),
              pipeline.host)) {
      return false;
    }
    pipeline.vin_attr_ex.ex_attr_type = VIN_STATIC_MCLK_ATTR;
    if (!call("hbn_vnode_set_attr_ex(VIN MCLK)",
              hbn_vnode_set_attr_ex(pipeline.vin, &pipeline.vin_attr_ex),
              pipeline.host)) {
      return false;
    }

    ret = hbn_vnode_open(HB_ISP, 0, AUTO_ALLOC_ID, &pipeline.isp);
    if (!call("hbn_vnode_open(ISP)", ret, pipeline.host)) return false;
    pipeline.isp_created = true;
    if (!call("hbn_vnode_set_attr(ISP)",
              hbn_vnode_set_attr(pipeline.isp, &pipeline.isp_attr),
              pipeline.host) ||
        !call("hbn_vnode_set_ichn_attr(ISP)",
              hbn_vnode_set_ichn_attr(pipeline.isp, 0,
                                      &pipeline.isp_ichn_attr),
              pipeline.host) ||
        !call("hbn_vnode_set_ochn_attr(ISP)",
              hbn_vnode_set_ochn_attr(pipeline.isp, 0,
                                      &pipeline.isp_ochn_attr),
              pipeline.host)) {
      return false;
    }
    hbn_buf_alloc_attr_t isp_buffers{};
    isp_buffers.buffers_num = 3;
    isp_buffers.is_contig = 1;
    isp_buffers.flags = HB_MEM_USAGE_CPU_READ_OFTEN |
                        HB_MEM_USAGE_CPU_WRITE_OFTEN | HB_MEM_USAGE_CACHED |
                        HB_MEM_USAGE_HW_ISP |
                        HB_MEM_USAGE_GRAPHIC_CONTIGUOUS_BUF;
    if (!call("hbn_vnode_set_ochn_buf_attr(ISP)",
              hbn_vnode_set_ochn_buf_attr(pipeline.isp, 0, &isp_buffers),
              pipeline.host)) {
      return false;
    }

    ret = hbn_vnode_open(HB_VSE, 0, AUTO_ALLOC_ID, &pipeline.vse);
    if (!call("hbn_vnode_open(VSE)", ret, pipeline.host)) return false;
    pipeline.vse_created = true;
    vse_attr_t vse_attr{};
    vse_ichn_attr_t vse_input{};
    vse_input.width = 1088;
    vse_input.height = 1280;
    vse_input.fmt = FRM_FMT_NV12;
    vse_input.bit_width = 8;
    vse_ochn_attr_t vse_output{};
    vse_output.chn_en = CAM_TRUE;
    vse_output.roi.x = 0;
    vse_output.roi.y = 96;
    vse_output.roi.w = 1088;
    vse_output.roi.h = 1088;
    vse_output.target_w = 640;
    vse_output.target_h = 640;
    vse_output.fmt = FRM_FMT_NV12;
    vse_output.bit_width = 8;
    vse_output.fps.src = 0;
    vse_output.fps.dst = 0;
    if (!call("hbn_vnode_set_attr(VSE)",
              hbn_vnode_set_attr(pipeline.vse, &vse_attr), pipeline.host) ||
        !call("hbn_vnode_set_ichn_attr(VSE)",
              hbn_vnode_set_ichn_attr(pipeline.vse, 0, &vse_input),
              pipeline.host) ||
        !call("hbn_vnode_set_ochn_attr(VSE)",
              hbn_vnode_set_ochn_attr(pipeline.vse, 0, &vse_output),
              pipeline.host)) {
      return false;
    }
    hbn_buf_alloc_attr_t vse_buffers{};
    vse_buffers.buffers_num = 3;
    vse_buffers.is_contig = 1;
    vse_buffers.flags = HB_MEM_USAGE_CPU_READ_OFTEN |
                        HB_MEM_USAGE_CPU_WRITE_OFTEN | HB_MEM_USAGE_CACHED |
                        HB_MEM_USAGE_GRAPHIC_CONTIGUOUS_BUF;
    if (!call("hbn_vnode_set_ochn_buf_attr(VSE)",
              hbn_vnode_set_ochn_buf_attr(pipeline.vse, 0, &vse_buffers),
              pipeline.host)) {
      return false;
    }

    if (!call("hbn_vflow_create", hbn_vflow_create(&pipeline.flow),
              pipeline.host)) {
      return false;
    }
    pipeline.flow_created = true;
    if (!call("hbn_vflow_add_vnode(VIN)",
              hbn_vflow_add_vnode(pipeline.flow, pipeline.vin), pipeline.host) ||
        !call("hbn_vflow_add_vnode(ISP)",
              hbn_vflow_add_vnode(pipeline.flow, pipeline.isp), pipeline.host) ||
        !call("hbn_vflow_add_vnode(VSE)",
              hbn_vflow_add_vnode(pipeline.flow, pipeline.vse), pipeline.host) ||
        !call("hbn_vflow_bind_vnode(VIN-ISP)",
              hbn_vflow_bind_vnode(pipeline.flow, pipeline.vin, 0,
                                   pipeline.isp, 0),
              pipeline.host) ||
        !call("hbn_vflow_bind_vnode(ISP-VSE)",
              hbn_vflow_bind_vnode(pipeline.flow, pipeline.isp, 0,
                                   pipeline.vse, 0),
              pipeline.host) ||
        !call("hbn_camera_attach_to_vin",
              hbn_camera_attach_to_vin(pipeline.camera_handle, pipeline.vin),
              pipeline.host) ||
        !call("hbn_vflow_start", hbn_vflow_start(pipeline.flow),
              pipeline.host)) {
      return false;
    }
    pipeline.flow_started = true;
    return true;
  }

  static void stopOne(Pipeline& pipeline) noexcept {
    if (pipeline.flow_started) hbn_vflow_stop(pipeline.flow);
    pipeline.flow_started = false;
    if (pipeline.vse_created) hbn_vnode_close(pipeline.vse);
    pipeline.vse_created = false;
    if (pipeline.isp_created) hbn_vnode_close(pipeline.isp);
    pipeline.isp_created = false;
    if (pipeline.vin_created) hbn_vnode_close(pipeline.vin);
    pipeline.vin_created = false;
    if (pipeline.camera_created) hbn_camera_destroy(pipeline.camera_handle);
    pipeline.camera_created = false;
    if (pipeline.flow_created) hbn_vflow_destroy(pipeline.flow);
    pipeline.flow_created = false;
  }

  int left_host_;
  int right_host_;
  Pipeline left_;
  Pipeline right_;
  bool initialized_ = false;
  std::string last_error_;
};

DualVioPipeline::DualVioPipeline(int left_host, int right_host)
    : impl_(std::make_unique<Impl>(left_host, right_host)) {}

DualVioPipeline::~DualVioPipeline() = default;

bool DualVioPipeline::initialize() { return impl_->initialize(); }
bool DualVioPipeline::acquireLeft(int timeout_ms, VseFrame& frame) {
  return impl_->acquireLeft(timeout_ms, frame);
}
bool DualVioPipeline::acquireRight(int timeout_ms, VseFrame& frame) {
  return impl_->acquireRight(timeout_ms, frame);
}
void DualVioPipeline::stop() noexcept { impl_->stop(); }
const std::string& DualVioPipeline::lastError() const {
  return impl_->lastError();
}

}  // namespace zerocopy
