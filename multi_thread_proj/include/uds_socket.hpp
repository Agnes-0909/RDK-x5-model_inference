#pragma once

#include <cstddef>
#include <string>

namespace mt {

class UdsSocket {
 public:
  UdsSocket();
  ~UdsSocket();

  bool bindAndListen(const std::string& path);
  bool connectTo(const std::string& path);
  bool acceptOne();
  bool sendAll(const void* data, size_t bytes);
  bool recvAll(void* data, size_t bytes);
  const std::string& lastError() const;

 private:
  bool setActiveFd(int fd);

  int listen_fd_ = -1;
  int active_fd_ = -1;
  std::string path_;
  std::string last_error_;
};

}  // namespace mt
