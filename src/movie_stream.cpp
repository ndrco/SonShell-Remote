#include "movie_stream.h"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <poll.h>
#include <stdexcept>
#include <sstream>
#include <exception>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
namespace sdk = SCRSDK;
struct Socket {
  int fd = -1;
  ~Socket() { if (fd >= 0) ::close(fd); }
};
void check(sdk::CrError result, const char* operation) {
  if (result != sdk::CrError_None) {
    std::ostringstream message; message << operation << ": Sony SDK 0x" << std::hex << result;
    throw std::runtime_error(message.str());
  }
}
std::string local_ip(const std::string& camera) {
  Socket route{::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)};
  sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(9);
  if (route.fd < 0 || inet_pton(AF_INET, camera.c_str(), &address.sin_addr) != 1 ||
      ::connect(route.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    throw std::runtime_error("Could not determine the network address for video preview");
  socklen_t size = sizeof(address);
  if (::getsockname(route.fd, reinterpret_cast<sockaddr*>(&address), &size)) throw std::runtime_error("Could not determine the computer address");
  char result[INET_ADDRSTRLEN]{};
  if (!inet_ntop(AF_INET, &address.sin_addr, result, sizeof(result))) throw std::runtime_error("Invalid computer address");
  return result;
}
}
void MovieStream::reset() { std::lock_guard<std::mutex> lock(mutex_); if (!active_) cancel_ = false; }
void MovieStream::cancel() { std::lock_guard<std::mutex> lock(mutex_); cancel_ = true; changed_.notify_all(); }
bool MovieStream::control(const std::string& operation, unsigned seconds) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!active_ || seconds > 86400) return false;
  if (operation == "pause") desired_ = 0;
  else if (operation == "play") desired_ = 1;
  else if (operation == "seek") { desired_ = 2; seek_ = seconds * 100; }
  else return false;
  changed_.notify_all(); return true;
}
void MovieStream::warning(unsigned value) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!active_) return;
  if (value == sdk::CrNotify_Playback_StatusChanged) ready_ = true;
  else if (value == sdk::CrNotify_Playback_Result_StopComplete) stopped_ = true;
  else if (value == sdk::CrNotify_Playback_Result_NormalTermination) ended_ = true;
  else if (value >= sdk::CrWarning_Playback_Result_CameraOperateTermination && value <= sdk::CrWarning_Playback_Result_KeepAliveTimeout) { failure_ = value; ended_ = true; }
  else if (value >= sdk::CrWarning_Playback_Result_Start_Fail && value <= sdk::CrWarning_Playback_Result_Pause_Fail) failure_ = value;
  changed_.notify_all();
}
void MovieStream::data(unsigned type, int size, const unsigned char* bytes) {
  if (type != sdk::CrMoviePlaybackDataType_Video || size <= 0 || !bytes) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!active_ || cancel_) return;
  if (size > 4 * 1024 * 1024 || buffered_ + size > 8 * 1024 * 1024) { failure_ = 0xffffffff; changed_.notify_all(); return; }
  packets_.emplace_back(bytes, bytes + size); buffered_ += size; changed_.notify_all();
}
void MovieStream::run(sdk::CrDeviceHandle handle, const std::string& host,
    unsigned slot, unsigned content, unsigned file, const std::string& path, const Event& event) {
  Socket socket{::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)};
  sockaddr_un address{}; address.sun_family = AF_UNIX;
  if (path.size() >= sizeof(address.sun_path) || socket.fd < 0) throw std::runtime_error("Video preview channel unavailable");
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  if (::connect(socket.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) throw std::runtime_error("Could not connect the video decoder");
  sdk::CrMoviePlaybackSetting setting;
  const auto computer = local_ip(host);
  check(setting.SetIpAddress(computer.c_str()), "SetIpAddress");
  setting.slotId = static_cast<sdk::CrSlotNumber>(slot); setting.contentsId = content; setting.fileId = file;
  setting.downTime = 15000; setting.videoPort = setting.audioPort = setting.metaPort = 0;
  check(sdk::SetDeviceSetting(handle, sdk::Setting_Key_EnableLiveView, 0), "DisableLiveView");
  check(sdk::SetMoviePlaybackSetting(handle, &setting, 1), "SetMoviePlaybackSetting");
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_ = true; ready_ = stopped_ = ended_ = false; failure_ = 0; desired_ = -1; packets_.clear(); buffered_ = 0;
  }
  bool started = false;
  std::exception_ptr error;
  try {
    event("preparing"); check(sdk::ControlMoviePlayback(handle, sdk::CrMoviePlaybackControlType_Start, 0), "StartMoviePlayback"); started = true;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (!changed_.wait_for(lock, std::chrono::seconds(20), [this] { return ready_ || failure_ || cancel_; })) throw std::runtime_error("Camera did not prepare video playback: no SDK response within 20 seconds");
      if (failure_) throw std::runtime_error("Camera rejected video preview: " + std::to_string(failure_));
    }
    bool play = true;
    { std::lock_guard<std::mutex> lock(mutex_); play = !cancel_; }
    if (play) { check(sdk::ControlMoviePlayback(handle, sdk::CrMoviePlaybackControlType_Play, 0), "PlayMoviePlayback"); event("playing"); }
    auto last_data = std::chrono::steady_clock::now();
    bool paused = false;
    size_t sent = 0;
    for (;;) {
      std::vector<unsigned char> packet; int command = -1; unsigned seek = 0;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait_for(lock, std::chrono::milliseconds(50), [this] { return cancel_ || ended_ || failure_ || !packets_.empty() || desired_ >= 0; });
        if (cancel_) break;
        if (failure_) throw std::runtime_error("Video stream failure: " + std::to_string(failure_));
        if (ended_ && packets_.empty()) break;
        command = desired_; seek = seek_; desired_ = -1;
        if (!packets_.empty()) { packet = std::move(packets_.front()); buffered_ -= packet.size(); packets_.pop_front(); }
      }
      if (command >= 0) {
        check(sdk::ControlMoviePlayback(handle, command == 2 ? sdk::CrMoviePlaybackControlType_Seek : command == 0 ? sdk::CrMoviePlaybackControlType_Pause : sdk::CrMoviePlaybackControlType_Play, seek), "ControlMoviePlayback");
        if (command != 2) paused = command == 0;
        event(paused ? "paused" : "playing"); last_data = std::chrono::steady_clock::now();
      }
      size_t offset = 0;
      const auto write_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (offset < packet.size()) {
        { std::lock_guard<std::mutex> lock(mutex_); if (cancel_) break; }
        const auto count = ::send(socket.fd, packet.data() + offset, packet.size() - offset, MSG_NOSIGNAL);
        if (count > 0) { offset += size_t(count); sent += size_t(count); }
        else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
          pollfd waiting{socket.fd, POLLOUT, 0}; ::poll(&waiting, 1, 50);
          if (std::chrono::steady_clock::now() > write_deadline) throw std::runtime_error("Decoder is not accepting the video stream");
        } else if (count < 0 && errno == EINTR) continue;
        else throw std::runtime_error("Video preview channel closed");
      }
      if (!packet.empty()) last_data = std::chrono::steady_clock::now();
      if (!paused && std::chrono::steady_clock::now() - last_data > std::chrono::seconds(12)) {
        if (!sent) throw NoData(host, computer);
        throw std::runtime_error("Camera stopped sending video data");
      }
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (failure_) throw std::runtime_error("Camera ended video preview: " + std::to_string(failure_));
      if (!sent && !cancel_) throw NoData(host, computer);
    }
  } catch (...) { error = std::current_exception(); }
  bool acknowledged = !started;
  if (started) {
    const auto result = sdk::ControlMoviePlayback(handle, sdk::CrMoviePlaybackControlType_Stop, 0);
    std::unique_lock<std::mutex> lock(mutex_);
    acknowledged = ended_ || (result == sdk::CrError_None && changed_.wait_for(lock, std::chrono::seconds(5), [this] { return stopped_ || ended_; }));
  }
  { std::lock_guard<std::mutex> lock(mutex_); active_ = false; cancel_ = false; packets_.clear(); buffered_ = 0; }
  if (!acknowledged) { event("fatal"); throw std::runtime_error("Camera did not confirm video stream stop"); }
  if (error) std::rethrow_exception(error);
  event("stopped");
}
