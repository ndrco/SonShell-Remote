#pragma once
#include "CRSDK/CameraRemote_SDK.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class MovieStream final {
 public:
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
