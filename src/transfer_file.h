#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>

// The SDK writes inside a hidden directory; readers only see the final rename.
class TransferFile final {
 public:
  bool prepare(const std::string& directory, const std::string& name, std::string& error) {
    if (name.empty() || std::filesystem::path(name).filename().string() != name || name == "." || name == "..") {
      error = "Invalid destination filename"; return false;
    }
    static std::atomic<unsigned long> sequence{0};
    std::error_code ec;
    target_ = std::filesystem::absolute(std::filesystem::path(directory) / name, ec);
    if (ec) { error = ec.message(); return false; }
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    staging_dir_ = target_.parent_path() / ".sonshell-incoming" /
        (std::to_string(::getpid()) + "-" + std::to_string(stamp) + "-" + std::to_string(sequence++));
    std::filesystem::create_directories(staging_dir_, ec);
    if (ec) { error = ec.message(); return false; }
    return true;
  }
  std::string staging_directory() const { return staging_dir_.string(); }
  std::string target() const { return target_.string(); }
  bool publish(std::string& error) {
    std::error_code ec;
    const auto staged = staging_dir_ / target_.filename();
    if (!std::filesystem::is_regular_file(staged, ec) || std::filesystem::file_size(staged, ec) == 0 || ec) {
      error = "Completed transfer has no readable file"; return false;
    }
    if (std::filesystem::exists(target_, ec)) { error = "Destination already exists"; return false; }
    std::filesystem::rename(staged, target_, ec);
    if (ec) { error = ec.message(); return false; }
    std::filesystem::remove(staging_dir_, ec);
    return true;
  }
  void discard() {
    std::error_code ignored;
    if (!staging_dir_.empty()) std::filesystem::remove_all(staging_dir_, ignored);
  }
 private:
  std::filesystem::path target_, staging_dir_;
};
