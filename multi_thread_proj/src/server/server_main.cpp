#include "server_app.hpp"

int main(int argc, char** argv) {
  mt::ServerConfig config;
  if (!mt::ParseServerArgs(argc, argv, config)) {
    return 1;
  }

  mt::ServerApp app;
  if (!app.initialize(config)) {
    return 1;
  }

  return app.run();
}
