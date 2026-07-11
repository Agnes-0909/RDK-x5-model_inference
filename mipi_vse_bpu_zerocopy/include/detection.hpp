#ifndef MIPI_VSE_BPU_ZEROCOPY_DETECTION_HPP_
#define MIPI_VSE_BPU_ZEROCOPY_DETECTION_HPP_

namespace zerocopy {

struct Box {
  float x1 = 0.0F;
  float y1 = 0.0F;
  float x2 = 0.0F;
  float y2 = 0.0F;
};

struct Detection {
  int class_id = -1;
  float score = 0.0F;
  Box box;
};

}  // namespace zerocopy

#endif  // MIPI_VSE_BPU_ZEROCOPY_DETECTION_HPP_
