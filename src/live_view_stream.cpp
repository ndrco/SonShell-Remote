#include "live_view_stream.h"
#include "CRSDK/CrImageDataBlock.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

LiveViewStream::~LiveViewStream() { stop(); }

bool LiveViewStream::start(SCRSDK::CrDeviceHandle handle, const std::string& path) {
  std::lock_guard<std::mutex> lock(lifecycle_);
  stop_locked();
  if (!handle || path.empty()) return false;
  if (SCRSDK::SetDeviceSetting(handle, SCRSDK::Setting_Key_EnableLiveView, 1) != SCRSDK::CrError_None)
    return false;
  stop_.store(false);
  worker_ = std::thread([this, handle, path] {
    SCRSDK::CrImageDataBlock block;
    std::vector<CrInt8u> buffer;
    const std::string temporary = path + ".part";
    while (!stop_.load()) {
      SCRSDK::CrImageInfo info{};
      if (SCRSDK::GetLiveViewImageInfo(handle, &info) == SCRSDK::CrError_None &&
          info.GetBufferSize() > 0 && info.GetBufferSize() <= 32 * 1024 * 1024) {
        buffer.resize(info.GetBufferSize());
        block.SetSize(static_cast<CrInt32u>(buffer.size()));
        block.SetData(buffer.data());
        if (SCRSDK::GetLiveViewImage(handle, &block) == SCRSDK::CrError_None &&
            block.GetImageSize() > 0 && block.GetImageSize() <= buffer.size()) {
          std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
          output.write(reinterpret_cast<const char*>(block.GetImageData()), block.GetImageSize());
          output.close();
          if (output) std::rename(temporary.c_str(), path.c_str());
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(65));
    }
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
  });
  return true;
}

void LiveViewStream::stop() {
  std::lock_guard<std::mutex> lock(lifecycle_);
  stop_locked();
}

void LiveViewStream::stop_locked() {
  stop_.store(true);
  if (worker_.joinable()) worker_.join();
}
