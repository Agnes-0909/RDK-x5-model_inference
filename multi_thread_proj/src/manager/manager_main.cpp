#include "manager_app.hpp"

int main(int argc, char** argv) {
  mt::ManagerConfig config;
  if (!mt::ParseManagerArgs(argc, argv, config)) {
    return 1;
  }

  mt::ManagerApp app;
  if (!app.initialize(config)) {
    return 1;
  }

  return app.run();
}
