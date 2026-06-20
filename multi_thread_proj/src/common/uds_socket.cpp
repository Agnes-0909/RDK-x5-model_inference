#include "uds_socket.hpp"

#include <cerrno>
#include <cstring>
#include <sstream>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace mt {

namespace {

bool transferAll(int fd, const void* data, size_t bytes, bool sending) {
  size_t transferred = 0;
  uint8_t* out = static_cast<uint8_t*>(const_cast<void*>(data));
  while (transferred < bytes) {
    const ssize_t rc = sending
                           ? send(fd, out + transferred, bytes - transferred, 0)
                           : recv(fd, out + transferred, bytes - transferred, 0);
    if (rc <= 0) {
      return false;
    }
    transferred += static_cast<size_t>(rc);
  }
  return true;
}

}  // namespace

UdsSocket::UdsSocket() = default;

UdsSocket::~UdsSocket() {
  if (active_fd_ >= 0) {
    close(active_fd_);
  }
  if (listen_fd_ >= 0) {
    close(listen_fd_);
  }
}

bool UdsSocket::bindAndListen(const std::string& path) {
  path_ = path;
  listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    last_error_ = "socket() failed";
    return false;
  }

  ::unlink(path.c_str());
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
  if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    last_error_ = "bind() failed";
    return false;
  }
  if (listen(listen_fd_, 1) != 0) {
    last_error_ = "listen() failed";
    return false;
  }
  return true;
}

bool UdsSocket::connectTo(const std::string& path) {
  path_ = path;
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    last_error_ = "socket() failed";
    return false;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(fd);
    last_error_ = "connect() failed";
    return false;
  }

  return setActiveFd(fd);
}

bool UdsSocket::acceptOne() {
  if (listen_fd_ < 0) {
    last_error_ = "listen fd not initialized";
    return false;
  }
  const int fd = accept(listen_fd_, nullptr, nullptr);
  if (fd < 0) {
    last_error_ = "accept() failed";
    return false;
  }
  return setActiveFd(fd);
}

bool UdsSocket::sendAll(const void* data, size_t bytes) {
  if (active_fd_ < 0) {
    last_error_ = "active fd not initialized";
    return false;
  }
  if (!transferAll(active_fd_, data, bytes, true)) {
    last_error_ = "send() failed";
    return false;
  }
  return true;
}

bool UdsSocket::recvAll(void* data, size_t bytes) {
  if (active_fd_ < 0) {
    last_error_ = "active fd not initialized";
    return false;
  }
  if (!transferAll(active_fd_, data, bytes, false)) {
    last_error_ = "recv() failed";
    return false;
  }
  return true;
}

const std::string& UdsSocket::lastError() const { return last_error_; }

bool UdsSocket::setActiveFd(int fd) {
  if (active_fd_ >= 0) {
    close(active_fd_);
  }
  active_fd_ = fd;
  return true;
}

}  // namespace mt
