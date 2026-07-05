#include "hobot_mipi_comm.hpp"

#include <rclcpp/rclcpp.hpp>

#include <cstdio>
#include <cstring>

namespace mipi_cam {

int exec_cmd_ex(const char *cmd, char *res, int max) {
  if (cmd == nullptr || res == nullptr || max <= 0) {
    return -1;
  }

  FILE *pipe = popen(cmd, "r");
  if (pipe == nullptr) {
    RCLCPP_ERROR(rclcpp::get_logger("mipi_cam"),
                 "error, cannot popen cmd: %s\n", cmd);
    return -1;
  }

  char tmp[1024] = {0};
  const int length = max > static_cast<int>(sizeof(tmp)) ? sizeof(tmp) : max;
  while (fgets(tmp, length, pipe) != nullptr) {
    std::sscanf(tmp, "%s", res);
  }

  pclose(pipe);
  return std::strlen(res);
}

}  // namespace mipi_cam
