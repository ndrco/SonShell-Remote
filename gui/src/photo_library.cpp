#include "photo_library.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QtConcurrentRun>
#include <algorithm>

PhotoLibrary::PhotoLibrary(QObject* parent) : QObject(parent) {
  pool_.setMaxThreadCount(2);
  timer_.setInterval(1000);
  connect(&timer_, &QTimer::timeout, this, &PhotoLibrary::scan);
  connect(&scanner_, &QFutureWatcher<Snapshot>::finished, this, [this] {
    scan_active_ = false;
    if (scan_generation_ != generation_) { scan(); return; }
    const auto snapshot = scanner_.result();
    stamps_ = snapshot.stamps;
    emit items_changed(snapshot.items);
    if (auto_preview_ && !snapshot.items.isEmpty()) {
      const auto& latest = snapshot.items.first();
      if (latest.signature != current_signature_) {
        current_signature_ = latest.signature;
        load(latest.path());
      }
    }
  });
  timer_.start();
}
PhotoLibrary::~PhotoLibrary() {
  cancel_->store(true);
  if (decode_cancel_) decode_cancel_->store(true);
  pool_.clear();
  pool_.waitForDone();
}
void PhotoLibrary::set_directory(const QString& path) {
  if (QDir(path).absolutePath() == directory_) return;
  directory_ = path.isEmpty() ? QString() : QDir(path).absolutePath();
  ++generation_; ++request_;
  cancel_->store(true); cancel_ = std::make_shared<std::atomic<bool>>(false);
  if (decode_cancel_) decode_cancel_->store(true);
  stamps_.clear(); current_signature_.clear();
  emit items_changed({});
  scan();
}
PhotoLibrary::Snapshot PhotoLibrary::read_directory(const QString& directory, const QHash<QString, QString>& previous, const MediaCancel& cancel) {
  Snapshot result;
  if (directory.isEmpty() || !QDir(directory).exists()) return result;
  QMap<QString, MediaItem> groups;
  QDirIterator files(directory, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
  while (files.hasNext() && !cancel->load()) {
    files.next(); const auto info = files.fileInfo(); const auto path = info.absoluteFilePath();
    if (!MediaReader::is_supported(path) || info.size() == 0) continue;
    const QString stamp = QString::number(info.size()) + ':' + QString::number(info.lastModified().toMSecsSinceEpoch());
    result.stamps.insert(path, stamp);
    if (previous.value(path) != stamp) continue;
    if ((info.suffix().compare("jpg", Qt::CaseInsensitive) == 0 || info.suffix().compare("jpeg", Qt::CaseInsensitive) == 0)
        && !MediaReader::is_complete_jpeg(path)) continue;
    const QString key = MediaReader::group_key(path);
    auto& item = groups[key];
    item.key = key; item.name = info.completeBaseName();
    if (MediaReader::is_video(path)) item.video_path = path;
    else if (MediaReader::is_raw(path)) item.raw_path = path;
    else if (item.jpeg_path.isEmpty() || path < item.jpeg_path) item.jpeg_path = path;
    item.modified = std::max(item.modified, info.lastModified());
    item.signature += path + ':' + stamp + ';';
  }
  for (auto& item : groups) {
    auto parts = item.signature.split(';', Qt::SkipEmptyParts); parts.sort(); item.signature = parts.join(';');
    result.items.append(item);
  }
  std::sort(result.items.begin(), result.items.end(), [](const auto& a, const auto& b) {
    return a.modified == b.modified ? a.key > b.key : a.modified > b.modified;
  });
  return result;
}
void PhotoLibrary::scan() {
  if (scan_active_) return;
  scan_active_ = true;
  scan_generation_ = generation_;
  const auto path = directory_; const auto previous = stamps_; const auto cancel = cancel_;
  scanner_.setFuture(QtConcurrent::run(&pool_, [path, previous, cancel] { return read_directory(path, previous, cancel); }));
}
void PhotoLibrary::load(const QString& path, bool new_capture) {
  if (decode_cancel_) decode_cancel_->store(true);
  decode_cancel_ = std::make_shared<std::atomic<bool>>(false);
  auto* watcher = new QFutureWatcher<MediaResult>(this);
  const auto request = ++request_; const auto generation = generation_;
  const auto cancel = decode_cancel_;
  connect(watcher, &QFutureWatcher<MediaResult>::finished, this, [this, watcher, request, generation, new_capture] {
    const auto result = watcher->result(); watcher->deleteLater();
    if (generation != generation_ || request != request_) return;
    emit photo_loaded(result, new_capture);
    if (!result.image.isNull()) emit image_ready(result.path, result.image);
  });
  watcher->setFuture(QtConcurrent::run(&pool_, [path, cancel] { return MediaReader::read(path, false, cancel); }));
}
bool PhotoLibrary::completed_download(const QString& path, bool new_capture) {
  const QFileInfo info(path);
  const auto relative = QDir(directory_).relativeFilePath(info.absoluteFilePath());
  if (directory_.isEmpty() || relative.startsWith("../") || relative == ".." || !info.isFile() || !MediaReader::is_supported(path)) return false;
  // Prefer the paired JPEG when RAW arrives after it; do not replace it with a smaller preview.
  QString best = info.absoluteFilePath();
  if (MediaReader::is_raw(best)) {
    const auto siblings = QDir(info.absolutePath()).entryInfoList(QDir::Files, QDir::Name);
    for (const auto& sibling : siblings) {
      if (sibling.completeBaseName().compare(info.completeBaseName(), Qt::CaseInsensitive) == 0 &&
          (sibling.suffix().compare("jpg", Qt::CaseInsensitive) == 0 || sibling.suffix().compare("jpeg", Qt::CaseInsensitive) == 0)
          && MediaReader::is_complete_jpeg(sibling.absoluteFilePath())) { best = sibling.absoluteFilePath(); break; }
    }
  }
  load(best, new_capture);
  scan();
  return true;
}
