#include "server_app.hpp"

int main() {
  {
    mt::ServerConfig config;
    const char* argv[] = {"mt_server", "--model", "/tmp/model.bin",
                          "--bpu-core-id", "1"};
    if (!mt::ParseServerArgs(5, const_cast<char**>(argv), config)) {
      return 1;
    }
    if (config.model_path != "/tmp/model.bin") {
      return 2;
    }
    if (config.bpu_core_id != 1) {
      return 3;
    }
  }

  {
    mt::ServerConfig config;
    const char* argv[] = {"mt_server", "--model", "/tmp/model.bin"};
    if (!mt::ParseServerArgs(3, const_cast<char**>(argv), config)) {
      return 4;
    }
    if (config.bpu_core_id != -1) {
      return 5;
    }
  }

  return 0;
}
