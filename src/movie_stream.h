#pragma once
#include "CRSDK/CameraRemote_SDK.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <stdexcept>
#include <vector>

class MovieStream final {
 public:
  struct NoData : std::runtime_error {
    std::string camera_host, computer_ip;
    NoData(std::string camera, std::string computer)
        : std::runtime_error("No video data from camera: check inbound TCP 55001, 55003, 55005"),
          camera_host(std::move(camera)), computer_ip(std::move(computer)) {}
  };
  using Event = std::function<void(const std::string&)>;
  void run(SCRSDK::CrDeviceHandle handle, const std::string& camera_host,
      unsigned slot, unsigned content, unsigned file, const std::string& socket_path, const Event& event);
  void reset();
  void cancel();
  bool control(const std::string& operation, unsigned seconds = 0);
  void warning(unsigned warning);
  void data(unsigned type, int size, const unsigned char* bytes);
 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<std::vector<unsigned char>> packets_;
  size_t buffered_ = 0;
  bool active_ = false, cancel_ = false, ready_ = false, stopped_ = false, ended_ = false;
  unsigned failure_ = 0, seek_ = 0;
  int desired_ = -1;
};
