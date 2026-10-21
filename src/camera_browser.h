#pragma once
#include "CRSDK/CameraRemote_SDK.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// One worker owns the card snapshot. SDK callbacks only signal terminal results.
// Preview and original transfers share the same SDK callback and must serialize.
class CameraBrowser final {
 public:
  using Output = std::function<void(const std::string&)>;
  CameraBrowser(std::mutex& transfers, Output output);
  ~CameraBrowser();
  void start(SCRSDK::CrDeviceHandle handle, std::string cache, std::string identity);
  void stop();
  bool command(const std::vector<std::string>& args);
  bool transfer_result(CrInt32u notify, CrInt32u percent);
  void delete_result(CrInt32u warning, CrInt32 result, CrInt32 content, CrInt32 slot);
  void contents_changed();
 private:
  struct File { unsigned id, format; unsigned long long size; std::string path; };
  struct Content {
    unsigned slot, id;
    bool protected_file, deletable;
    std::string stamp, signature;
    std::vector<File> files;
  };
  void run();
  void execute(const std::vector<std::string>& args);
  void list(const std::string& request);
  std::vector<Content> read_slot(unsigned slot);
  bool enabled(unsigned code);
  void transfer(const std::vector<std::string>& args, const Content& content, const File& file);
  void erase(const std::vector<std::string>& args, const Content& content);
  void event(const std::string& type, const std::string& request, const std::string& fields = {});
  void error(const std::string& request, const std::string& message);
  std::mutex& transfers_;
  Output output_;
  SCRSDK::CrDeviceHandle handle_ = 0;
  std::string cache_, identity_, epoch_;
  std::map<std::string, Content> catalog_;
  std::thread worker_;
  std::mutex queue_mutex_, result_mutex_;
  std::condition_variable queue_cv_, result_cv_;
  std::deque<std::vector<std::string>> queue_;
  std::atomic<bool> stopping_{true}, canceled_{false};
  std::atomic<unsigned long long> revision_{0};
  unsigned long long catalog_revision_ = 0;
  bool transferring_ = false, deleting_ = false, terminal_ = false;
  unsigned result_ = 0, delete_slot_ = 0, delete_content_ = 0, progress_ = 101;
  std::string active_request_;
};
