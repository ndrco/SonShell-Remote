#pragma once
#include <atomic>
#include <string>
#include <mutex>
#include <thread>
#include "CRSDK/CameraRemote_SDK.h"

// Publishes only the newest JPEG; SDK ownership stays in the CLI runtime.
class LiveViewStream final {
 public:
  ~LiveViewStream();
  bool start(SCRSDK::CrDeviceHandle handle, const std::string& path);
  void stop();
 private:
  void stop_locked();
  std::mutex lifecycle_;
  std::atomic<bool> stop_{true};
  std::thread worker_;
};
