#include <string>

#include "logger.hpp"

int main() {
  if (mt::shortFileName("/project/multi_thread_proj/src/manager/manager_app.cpp") !=
      "manager_app.cpp") {
    return 1;
  }

  if (mt::shortFileName("src/server/worker.cpp") != "worker.cpp") {
    return 2;
  }

  if (mt::shortFileName("logger.cpp") != "logger.cpp") {
    return 3;
  }

  return 0;
}
