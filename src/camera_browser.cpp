#include "camera_browser.h"
#include "transfer_file.h"
#include "capture_date.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
namespace sdk = SCRSDK;
std::string json(const std::string& value) {
  std::ostringstream out; out << '"';
  for (unsigned char ch : value) {
    if (ch == '"' || ch == '\\') out << '\\' << ch;
    else if (ch < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(ch) << std::dec;
    else out << ch;
  }
  out << '"'; return out.str();
}
std::string unhex(const std::string& value) {
  if (value.empty() || value.size() % 2 || value.size() > 16000) throw std::runtime_error("Неверный каталог назначения");
  std::string out;
  auto digit = [](char c) -> unsigned { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; throw std::runtime_error("Неверное кодирование пути"); };
  for (size_t i = 0; i < value.size(); i += 2) out += char(digit(value[i]) * 16 + digit(value[i + 1]));
  if (out.find('\0') != std::string::npos) throw std::runtime_error("Недопустимый путь");
  return out;
}
unsigned number(const std::string& value) {
  size_t used = 0; auto result = std::stoul(value, &used);
  if (used != value.size() || result > 0xffffffffu) throw std::runtime_error("Неверный идентификатор");
  return static_cast<unsigned>(result);
}
std::string key(unsigned slot, unsigned id) { return std::to_string(slot) + ":" + std::to_string(id); }
std::string date(const sdk::CrCaptureDate& d) {
  std::ostringstream out;
  out << d.year << '-' << std::setw(2) << std::setfill('0') << unsigned(d.month) << '-'
      << std::setw(2) << unsigned(d.day) << 'T' << std::setw(2) << unsigned(d.hour) << ':'
      << std::setw(2) << unsigned(d.minute) << ':' << std::setw(2) << unsigned(d.sec) << '.' << std::setw(3) << d.msec;
  return out.str();
}
std::string sdk_error(unsigned code) {
  std::ostringstream out; out << "Ошибка Sony SDK: 0x" << std::hex << code; return out.str();
}
std::filesystem::path relative_path(std::string path) {
  if (path.find('\0') != std::string::npos) throw std::runtime_error("Неверный путь на карте");
  std::replace(path.begin(), path.end(), '\\', '/');
  const std::filesystem::path remote(path);
  if (remote.filename().empty()) throw std::runtime_error("Пустое имя файла на карте");
  std::filesystem::path out;
  bool first = true;
  for (const auto& part : remote) {
    auto name = part.string();
    if (part == "/" || part == "." || part.empty()) continue;
    if (part == "..") throw std::runtime_error("Небезопасный путь на карте: переход в родительский каталог");
    // Sony paths can contain a camera volume namespace (e.g. C:/DCIM/...).
    // It is not a host drive or a directory to recreate under the selected root.
    if (first && name.find(':') != std::string::npos && part != remote.filename()) {
      first = false; continue;
    }
    first = false;
    std::string local;
    for (unsigned char ch : name) {
      if (ch == ':') local += "%3A";
      else if (ch == '%') local += "%25";
      else local += char(ch);
    }
    out /= local;
  }
  if (out.empty() || out.filename() == ".") throw std::runtime_error("Пустое имя файла на карте");
  return out;
}
}

CameraBrowser::CameraBrowser(std::mutex& transfers, Output output) : transfers_(transfers), output_(std::move(output)) {}
CameraBrowser::~CameraBrowser() { stop(); }
void CameraBrowser::start(sdk::CrDeviceHandle handle, std::string cache, std::string identity, std::string camera_host) {
  stop(); handle_ = handle; cache_ = std::move(cache); identity_ = std::move(identity); camera_host_ = std::move(camera_host);
  stopping_ = false; canceled_ = false; catalog_.clear(); epoch_.clear();
  worker_ = std::thread([this] { run(); });
}
void CameraBrowser::stop() {
  stopping_ = true; canceled_ = true; movie_.cancel();
  queue_cv_.notify_all(); result_cv_.notify_all();
  if (worker_.joinable()) worker_.join();
  handle_ = 0;
  std::lock_guard<std::mutex> lock(queue_mutex_); queue_.clear();
}
void CameraBrowser::event(const std::string& type, const std::string& request, const std::string& fields) {
  output_("{\"type\":" + json(type) + ",\"request\":" + json(request) + (fields.empty() ? "" : "," + fields) + "}");
}
void CameraBrowser::error(const std::string& request, const std::string& message) { event("error", request, "\"message\":" + json(message)); }
void CameraBrowser::contents_changed() {
  ++revision_;
  event("changed", ""); // Metadata notification only: never schedule a transfer here.
}
bool CameraBrowser::command(const std::vector<std::string>& args) {
  if (args.size() == 3 && (args[1] == "movie-play" || args[1] == "movie-pause")) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return args[2] == movie_request_ && movie_.control(args[1] == "movie-play" ? "play" : "pause");
  }
  if (args.size() == 4 && args[1] == "movie-seek") {
    try { std::lock_guard<std::mutex> lock(queue_mutex_); return args[2] == movie_request_ && movie_.control("seek", number(args[3])); } catch (...) { return false; }
  }
  if (args.size() == 2 && args[1] == "cancel") {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    canceled_ = true; movie_.cancel();
    for (const auto& pending : queue_) error(pending[2], "Операция отменена");
    queue_.clear(); result_cv_.notify_all();
    return true;
  }
  if (args.size() < 3 || stopping_) return false;
  if (args[2].empty() || args[2].size() > 80 || args[2].find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-") != std::string::npos) return false;
  std::lock_guard<std::mutex> lock(queue_mutex_);
  if (queue_.size() >= 8) { error(args[2], "Очередь камеры занята"); return false; }
  queue_.push_back(args); queue_cv_.notify_one(); return true;
}
void CameraBrowser::run() {
  while (!stopping_) {
    std::vector<std::string> args;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_) break;
      args = std::move(queue_.front()); queue_.pop_front(); canceled_ = false; movie_.reset();
    }
    try { execute(args); }
    catch (const MovieStream::NoData& e) {
      event("error", args[2], "\"code\":\"movie-no-data\",\"camera_host\":" + json(e.camera_host) + ",\"computer_ip\":" + json(e.computer_ip) + ",\"message\":" + json(e.what()));
    } catch (const std::exception& e) { error(args[2], e.what()); }
  }
}
bool CameraBrowser::enabled(unsigned code) {
  sdk::CrDeviceProperty* properties = nullptr; CrInt32 count = 0;
  const auto err = sdk::GetDeviceProperties(handle_, &properties, &count);
  bool result = false;
  if (err == sdk::CrError_None && properties) for (CrInt32 i = 0; i < count; ++i)
    if (properties[i].GetCode() == code) result = properties[i].GetCurrentValue() == 1;
  if (properties) sdk::ReleaseDeviceProperties(handle_, properties);
  return result;
}
bool CameraBrowser::slot_supported(unsigned slot) {
  sdk::CrDeviceProperty* properties = nullptr; CrInt32 count = 0;
  const auto err = sdk::GetDeviceProperties(handle_, &properties, &count);
  bool supported = slot == 1;
  if (err == sdk::CrError_None && properties) for (CrInt32 i = 0; i < count; ++i) {
    const auto code = properties[i].GetCode();
    const auto status = slot == 1 ? sdk::CrDeviceProperty_MediaSLOT1_Status : sdk::CrDeviceProperty_MediaSLOT2_Status;
    const auto list = slot == 1 ? sdk::CrDeviceProperty_MediaSLOT1_ContentsInfoListEnableStatus : sdk::CrDeviceProperty_MediaSLOT2_ContentsInfoListEnableStatus;
    if ((code == status && properties[i].IsGetEnableCurrentValue()) ||
        (code == list && properties[i].GetCurrentValue() == 1)) supported = true;
  }
  if (properties) sdk::ReleaseDeviceProperties(handle_, properties);
  if (err != sdk::CrError_None) throw std::runtime_error(sdk_error(err));
  return supported;
}
std::vector<CameraBrowser::Content> CameraBrowser::read_slot(unsigned slot) {
  const bool deletable = enabled(slot == 1 ? sdk::CrDeviceProperty_DeleteContentOperationEnableStatusSLOT1 : sdk::CrDeviceProperty_DeleteContentOperationEnableStatusSLOT2);
  if (!enabled(slot == 1 ? sdk::CrDeviceProperty_MediaSLOT1_ContentsInfoListEnableStatus : sdk::CrDeviceProperty_MediaSLOT2_ContentsInfoListEnableStatus))
    throw std::runtime_error("Список карты недоступен: проверь наличие карты, режим и поддержку камерой");
  sdk::CrContentsInfo* list = nullptr; CrInt32u count = 0;
  sdk::CrCaptureDate base_date;
  auto err = sdk::GetRemoteTransferContentsInfoList(handle_, static_cast<sdk::CrSlotNumber>(slot), sdk::CrGetContentsInfoListType_All, &base_date, 0, &list, &count);
  struct Release { sdk::CrDeviceHandle handle; sdk::CrContentsInfo* data; ~Release() { if (data) sdk::ReleaseRemoteTransferContentsInfoList(handle, data); } } release{handle_, list};
  if (err != sdk::CrError_None) throw std::runtime_error(sdk_error(err));
  std::vector<Content> result;
  for (CrInt32u i = 0; list && i < count && !stopping_ && !canceled_; ++i) {
    const auto& entry = list[i];
    if (entry.dummyContent) continue;
    Content content{slot, entry.contentId, entry.protectionStatus, deletable, date(entry.creationDatetimeUTC), {}, {}, {}};
    const auto& local = entry.creationDatetimeLocaltime; const auto& utc = entry.creationDatetimeUTC;
    content.capture_date = capture_date_folder(local.year, local.month, local.day);
    if (content.capture_date.empty()) content.capture_date = capture_date_folder(utc.year, utc.month, utc.day);
    if (content.capture_date.empty()) content.capture_date = "Unknown-Date";
    // Stable import identity excludes contentId, rating and modification date.
    content.signature = identity_ + ":" + std::to_string(slot) + ":" + content.stamp;
    bool has_media = false;
    for (CrInt32u fi = 0; entry.files && fi < entry.filesNum; ++fi) {
      const auto& f = entry.files[fi];
      if (!f.filePath) continue;
      has_media = has_media || f.fileFormat == sdk::CrContentsFile_FileFormat_Raw || f.fileFormat == sdk::CrContentsFile_FileFormat_Jpeg || f.fileFormat == sdk::CrContentsFile_FileFormat_Heif || f.fileFormat == sdk::CrContentsFile_FileFormat_Mp4;
      File file{f.fileId, unsigned(f.fileFormat), f.fileSize, std::string(f.filePath)};
      content.signature += ":" + file.path + ":" + std::to_string(file.size);
      content.files.push_back(std::move(file));
    }
    if (has_media && !content.files.empty()) result.push_back(std::move(content));
  }
  return result;
}
void CameraBrowser::list(const std::string& request) {
  catalog_.clear(); catalog_revision_ = revision_;
  epoch_ = request;
  event("begin", request, "\"epoch\":" + json(epoch_));
  for (unsigned slot : {1u, 2u}) {
    try {
      if (!slot_supported(slot)) continue;
      for (auto& c : read_slot(slot)) {
        std::string files = "[";
        for (const auto& f : c.files) {
          if (files.size() > 1) files += ',';
          files += "{\"id\":" + std::to_string(f.id) + ",\"format\":" + std::to_string(f.format) + ",\"size\":" + json(std::to_string(f.size)) + ",\"path\":" + json(f.path) + "}";
        }
        files += ']';
        event("item", request, "\"epoch\":" + json(epoch_) + ",\"slot\":" + std::to_string(c.slot) + ",\"id\":" + std::to_string(c.id) + ",\"stamp\":" + json(c.stamp) + ",\"capture_date\":" + json(c.capture_date) + ",\"identity\":" + json(c.signature) + ",\"protected\":" + (c.protected_file ? "true" : "false") + ",\"deletable\":" + (c.deletable ? "true" : "false") + ",\"files\":" + files);
        catalog_[key(c.slot, c.id)] = std::move(c);
      }
    } catch (const std::exception& e) {
      event("slot-error", request, "\"slot\":" + std::to_string(slot) + ",\"message\":" + json(e.what()));
    }
  }
  event("end", request, "\"epoch\":" + json(epoch_) + ",\"stale\":" + (canceled_ || revision_ != catalog_revision_ ? "true" : "false"));
}
void CameraBrowser::execute(const std::vector<std::string>& args) {
  if (args[1] == "list" && args.size() == 3) { list(args[2]); return; }
  const bool erase_command = args[1] == "delete";
  if ((erase_command && args.size() != 6) || (!erase_command && args.size() != 8)) throw std::runtime_error("Неверная команда карты");
  if (args[3] != epoch_) throw std::runtime_error("Каталог устарел; обнови список камеры");
  const auto found = catalog_.find(key(number(args[4]), number(args[5])));
  if (found == catalog_.end()) throw std::runtime_error("Кадр отсутствует в каталоге");
  // Re-read before destructive action; content IDs can be reused after card changes.
  if (erase_command) { erase(args, found->second); return; }
  const auto& content = found->second;
  if (revision_ != catalog_revision_) {
    const auto current = read_slot(content.slot);
    if (std::none_of(current.begin(), current.end(), [&](const Content& c) { return c.id == content.id && c.signature == content.signature; }))
      throw std::runtime_error("Содержимое карты изменилось; обнови список");
  }
  auto file = std::find_if(content.files.begin(), content.files.end(), [&](const File& f) { return f.id == number(args[6]); });
  if (file == content.files.end()) throw std::runtime_error("Файл отсутствует в каталоге");
  if (args[1] == "movie") {
    if (file->format != sdk::CrContentsFile_FileFormat_Mp4) throw std::runtime_error("Выбери видео MP4");
    const auto socket = std::filesystem::path(unhex(args[7]));
    if (!socket.is_absolute() || std::filesystem::canonical(socket.parent_path()) != std::filesystem::canonical(cache_) ||
        !std::filesystem::is_socket(std::filesystem::symlink_status(socket))) throw std::runtime_error("Неверный канал видеопревью");
    std::unique_lock<std::mutex> serial(transfers_);
    { std::lock_guard<std::mutex> lock(queue_mutex_); movie_request_ = args[2]; }
    struct ClearRequest {
      std::mutex& mutex; std::string& request;
      ~ClearRequest() { std::lock_guard<std::mutex> lock(mutex); request.clear(); }
    } clear_request{queue_mutex_, movie_request_};
    movie_.run(handle_, camera_host_, content.slot, content.id, file->id, socket.string(), [this, &args](const std::string& state) {
      if (state == "fatal") { stopping_ = true; event("fatal", args[2], "\"message\":" + json("Камера не подтвердила остановку видео; требуется переподключение")); }
      else event(state == "stopped" ? "movie-stopped" : "movie-state", args[2], "\"state\":" + json(state));
    });
    return;
  }
  if (args[1] != "copy" && args[1] != "preview") throw std::runtime_error("Неизвестная команда карты");
  transfer(args, content, *file);
}
bool CameraBrowser::transfer_result(CrInt32u notify, CrInt32u percent) {
  std::lock_guard<std::mutex> lock(result_mutex_);
  if (!transferring_) return false;
  transfer_activity_ = std::chrono::steady_clock::now();
  if (notify == sdk::CrNotify_RemoteTransfer_InProgress) {
    if (percent <= 100 && (progress_ == 101 || percent >= progress_ + 5)) {
      progress_ = percent; event("progress", active_request_, "\"percent\":" + std::to_string(percent));
    }
  } else { result_ = notify; terminal_ = true; result_cv_.notify_all(); }
  return true;
}
void CameraBrowser::transfer(const std::vector<std::string>& args, const Content& content, const File& file) {
  std::unique_lock<std::mutex> serial(transfers_);
  if (stopping_ || canceled_) throw std::runtime_error("Операция отменена");
  const bool preview = args[1] == "preview";
  std::filesystem::path destination;
  if (preview) {
    if (args[7] != "screen" && args[7] != "thumb") throw std::runtime_error("Неизвестный размер превью");
    if (cache_.empty()) throw std::runtime_error("Каталог превью недоступен");
    destination = std::filesystem::path(cache_) / (args[2] + ".jpg");
  } else {
    auto root = std::filesystem::path(unhex(args[7]));
    if (!root.is_absolute()) throw std::runtime_error("Выбери абсолютный каталог назначения");
    std::filesystem::create_directories(root);
    root = std::filesystem::canonical(root);
    const auto relative = std::filesystem::path(content.capture_date) / std::filesystem::path("Slot_" + std::to_string(content.slot)) / relative_path(file.path);
    auto checked = root;
    for (const auto& part : relative) {
      checked /= part;
      if (std::filesystem::is_symlink(std::filesystem::symlink_status(checked))) throw std::runtime_error("Символическая ссылка в пути назначения");
    }
    destination = root / relative;
    // Never overwrite or assume that an existing file is the same photograph.
    const auto base = destination;
    for (unsigned i = 1; std::filesystem::exists(destination); ++i)
      destination = base.parent_path() / (base.stem().string() + "_" + std::to_string(i) + base.extension().string());
  }
  TransferFile output;
  std::string message;
  if (!output.prepare(destination.parent_path().string(), destination.filename().string(), message)) throw std::runtime_error(message);
  struct Discard { TransferFile& file; ~Discard() { file.discard(); } } discard{output};
  auto directory = output.staging_directory(); auto name = destination.filename().string();
  {
    std::lock_guard<std::mutex> lock(result_mutex_);
    transferring_ = true; terminal_ = false; active_request_ = args[2]; progress_ = 101; transfer_activity_ = std::chrono::steady_clock::now();
  }
  auto err = preview ? sdk::GetRemoteTransferContentsCompressedDataFile(handle_, static_cast<sdk::CrSlotNumber>(content.slot), content.id, file.id,
      (args[7] == "thumb" || file.format == sdk::CrContentsFile_FileFormat_Mp4) ? sdk::CrGetContentsCompressedDataType_Thumbnail : sdk::CrGetContentsCompressedDataType_Screennail, directory.data(), name.data()) :
      sdk::GetRemoteTransferContentsDataFile(handle_, static_cast<sdk::CrSlotNumber>(content.slot), content.id, file.id, 0x1000000, directory.data(), name.data());
  bool cancel_sent = false;
  auto cancel_deadline = std::chrono::steady_clock::time_point::max();
  std::unique_lock<std::mutex> lock(result_mutex_);
  if (err != sdk::CrError_None) { transferring_ = false; throw std::runtime_error(sdk_error(err)); }
  while (!terminal_) {
    if ((stopping_ || canceled_ || std::chrono::steady_clock::now() - transfer_activity_ > std::chrono::minutes(3)) && !cancel_sent) {
      cancel_sent = true; cancel_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5); lock.unlock();
      // The command is shared by original and compressed transfers.
      sdk::ControlGetRemoteTransferContentsDataFile(handle_, sdk::CrGetContentsDataControlType_Cancel);
      lock.lock();
    }
    // During shutdown the process terminates before any later request can reuse callbacks.
    if (stopping_) break;
    if (cancel_sent && std::chrono::steady_clock::now() > cancel_deadline) {
      stopping_ = true;
      event("fatal", args[2], "\"message\":" + json("Камера не подтвердила остановку передачи; требуется переподключение"));
      break;
    }
    result_cv_.wait_for(lock, std::chrono::milliseconds(100));
  }
  transferring_ = false;
  if (stopping_ || canceled_ || cancel_sent) throw std::runtime_error("Передача отменена");
  if (result_ != sdk::CrNotify_RemoteTransfer_Result_OK) throw std::runtime_error(sdk_error(result_));
  lock.unlock();
  std::error_code ec;
  auto size = std::filesystem::file_size(std::filesystem::path(directory) / name, ec);
  if (ec || (!preview && file.size && size != file.size)) throw std::runtime_error("Размер полученного файла не совпадает с оригиналом");
  if (!output.publish(message)) throw std::runtime_error(message);
  event(preview ? "preview" : "copied", args[2], "\"path\":" + json(output.target()) + ",\"size\":" + json(std::to_string(size)));
}
void CameraBrowser::delete_result(CrInt32u warning, CrInt32 result, CrInt32 content, CrInt32 slot) {
  if (warning != sdk::CrWarningExt_DeleteContent) return;
  std::lock_guard<std::mutex> lock(result_mutex_);
  if (!deleting_ || unsigned(content) != delete_content_ || unsigned(slot) != delete_slot_) return;
  result_ = unsigned(result); terminal_ = true; result_cv_.notify_all();
}
void CameraBrowser::erase(const std::vector<std::string>& args, const Content& content) {
  std::unique_lock<std::mutex> serial(transfers_);
  const auto snapshot = read_slot(content.slot);
  auto current = std::find_if(snapshot.begin(), snapshot.end(), [&](const Content& c) { return c.id == content.id && c.signature == content.signature; });
  if (current == snapshot.end()) throw std::runtime_error("Содержимое карты изменилось; обнови список перед удалением");
  if (!current->deletable || current->protected_file) throw std::runtime_error("Камера не разрешает удаление этого кадра");
  if (stopping_ || canceled_) throw std::runtime_error("Операция отменена");
  {
    std::lock_guard<std::mutex> lock(result_mutex_);
    deleting_ = true; terminal_ = false; delete_slot_ = content.slot; delete_content_ = content.id;
  }
  auto err = sdk::DeleteRemoteTransferContentsFile(handle_, static_cast<sdk::CrSlotNumber>(content.slot), content.id);
  std::unique_lock<std::mutex> lock(result_mutex_);
  bool notified = false;
  if (err == sdk::CrError_None) notified = result_cv_.wait_for(lock, std::chrono::seconds(30), [this] { return terminal_ || stopping_; });
  deleting_ = false;
  if (err != sdk::CrError_None) throw std::runtime_error(sdk_error(err));
  if (!notified || stopping_ || result_ != sdk::CrWarningExtParam_DeleteContentResult_OK) throw std::runtime_error("Удаление не подтверждено камерой; обнови список");
  lock.unlock();
  event("deleted", args[2]);
  catalog_.erase(key(content.slot, content.id));
}
