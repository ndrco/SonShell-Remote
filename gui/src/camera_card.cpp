#include <QCoreApplication>
#include "camera_card.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QRegularExpression>
#include <QTimer>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QUuid>
#include <algorithm>

namespace {
constexpr int raw_format = 0xb101;
constexpr int jpeg_format = 0x3801;
bool save_json(const QString& path, const QJsonObject& object) {
  QSaveFile file(path); const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
  if (bytes.size() > 32 * 1024 * 1024) return false;
  return file.open(QIODevice::WriteOnly) && file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) &&
      file.write(bytes) == bytes.size() && file.commit();
}
}
QString CardItem::name() const { return files.isEmpty() ? key() : QFileInfo(files.first().path).fileName(); }
bool CardItem::is_video() const {
  return std::any_of(files.begin(), files.end(), [](const CardFile& file) { return file.format == 0xb982; });
}
QString CardItem::format() const {
  QStringList result;
  for (const auto& file : files) {
    const QString format = file.format == raw_format ? "RAW" : file.format == jpeg_format ? "JPEG" : file.format == 0xb110 ? "HEIF" : file.format == 0xb982 ? "MP4" : file.format == 0xba82 ? "XML" : QCoreApplication::translate("App", "Доп. файл");
    if (!result.contains(format)) result.append(format);
  }
  return result.join("+");
}
CardFile CardItem::preview_file() const {
  for (const auto& file : files) if (file.format == 0xb982) return file;
  for (const auto& file : files) if (file.format == jpeg_format) return file;
  for (const auto& file : files) if (file.format == raw_format || file.format == 0xb110) return file;
  return files.value(0);
}
CameraCard::CameraCard(CameraSession* session, QObject* parent) : QObject(parent), session_(session) {
  // Full-size Sony JPEGs can exceed Qt's default 128 MiB decoded-image budget.
  // Respect an explicit per-process limit; our own JPEG bounds remain 120 MP.
  if (qEnvironmentVariableIsEmpty("QT_IMAGEIO_MAXALLOC")) QImageReader::setAllocationLimit(512);
  connect(session_, &CameraSession::card_event, this, &CameraCard::receive);
  studio_timer_.setSingleShot(true); studio_timer_.setInterval(500);
  connect(&studio_timer_, &QTimer::timeout, this, [this] {
    if (studio_pending_ && studio_ && connected_ && !busy()) { studio_pending_ = false; studio_list_ = true; refresh(); }
  });
  connect(&verification_, &QFutureWatcher<ImportVerification>::progressValueChanged, this, [this](int progress) {
    if (active_.kind == "verify") emit progress_changed(0, 0, progress / 10);
  });
  connect(&verification_, &QFutureWatcher<ImportVerification>::finished, this, [this] {
    if (active_.kind != "verify") return;
    if (verification_.isCanceled()) { finish(QCoreApplication::translate("App", "Проверка копий остановлена. Файлы не изменялись.")); return; }
    const auto result = verification_.result(); ledger_ = result.ledger;
    const bool saved = save_json(ledger_path(), ledger_);
    emit catalog_changed();
    finish(saved ? QString(QCoreApplication::translate("App", "SHA256: исправны %1; изменены %2; отсутствуют %3; без эталона %4.")).arg(result.good).arg(result.changed).arg(result.missing).arg(result.legacy) :
        QCoreApplication::translate("App", "Проверка завершена, но журнал импорта не сохранён."));
  });
}
CameraCard::~CameraCard() {
  verification_.cancel();
  if (original_cancel_) original_cancel_->store(true);
  clear_original();
}
void CameraCard::clear_original() {
  if (original_temporary_) QFile::remove(original_result_.path);
  original_result_ = {}; original_key_.clear(); original_temporary_ = false;
}
QString CameraCard::request_id() { return prefix_ + QString::number(++sequence_); }
void CameraCard::set_connected(bool connected) {
  if (connected == connected_) return;
  connected_ = connected; prefix_ = QUuid::createUuid().toString(QUuid::Id128) + '-';
  if (playing_video()) emit movie_viewing(false);
  if (active_.kind != "verify") active_ = {};
  queue_.clear(); list_request_.clear(); epoch_.clear(); items_.clear(); resume_after_list_ = false;
  dirty_ = true; studio_list_ = false; canceled_ = false;
  studio_pending_ = false; studio_timer_.stop(); last_studio_identity_.clear();
  emit catalog_changed(); emit busy_changed(busy());
  if (original_cancel_) original_cancel_->store(true);
  original_cancel_.reset();
  clear_original();
  emit status_changed(connected ? QCoreApplication::translate("App", "Открой вкладку и обнови список карты. Оригиналы при съёмке не копируются.") : QCoreApplication::translate("App", "Камера отключена"));
}
QString CameraCard::ledger_path() const { return QDir(directory_).filePath(".sonshell-imports.json"); }
QString CameraCard::pending_path() const { return QDir(directory_).filePath(".sonshell-copy-queue.json"); }
bool CameraCard::save_pending() {
  if (saved_queue_.isEmpty()) return !QFileInfo::exists(pending_path()) || QFile::remove(pending_path());
  return save_json(pending_path(), {{"version", 1}, {"files", saved_queue_}});
}
void CameraCard::set_directory(const QString& directory) {
  if (busy()) return;
  directory_ = directory.trimmed().isEmpty() ? QString() : QFileInfo(directory).canonicalFilePath();
  if (directory_.isEmpty() && !directory.trimmed().isEmpty()) directory_ = QDir(directory).absolutePath();
  ledger_ = {};
  QFile file(ledger_path());
  if (file.open(QIODevice::ReadOnly) && file.size() <= 32 * 1024 * 1024) ledger_ = QJsonDocument::fromJson(file.readAll()).object();
  saved_queue_ = {}; QFile pending(pending_path());
  if (pending.open(QIODevice::ReadOnly) && pending.size() <= 32 * 1024 * 1024) {
    const auto object = QJsonDocument::fromJson(pending.readAll()).object();
    if (object.value("version").toInt() == 1) saved_queue_ = object.value("files").toArray();
  }
  emit catalog_changed();
}
QString CameraCard::ledger_key(const CardItem& item, const CardFile& file) const {
  return QString::fromLatin1(QCryptographicHash::hash((item.identity + ':' + file.path + ':' + QString::number(file.size)).toUtf8(), QCryptographicHash::Sha256).toHex());
}
bool CameraCard::imported(const CardItem& item, const CardFile& file) const {
  const auto entry = ledger_.value(ledger_key(item, file)).toObject();
  const auto path = entry.value("path").toString();
  const auto canonical = QFileInfo(path).canonicalFilePath();
  const auto relative = QDir(directory_).relativeFilePath(canonical);
  const QFileInfo info(path);
  return !canonical.isEmpty() && relative != ".." && !relative.startsWith("../") && info.isFile() &&
      !info.isSymLink() && file.size > 0 && quint64(info.size()) == file.size && entry.value("size").toString() == QString::number(file.size) &&
      entry.value("valid").toBool(true) && (entry.value("mtime").isUndefined() || entry.value("mtime").toString() == QString::number(info.lastModified().toMSecsSinceEpoch()));
}
bool CameraCard::matches(const CardItem& item, const CardFile& file, const QString& format) {
  return format == "all" || (format == "video" && item.is_video()) || (format == "photo" && !item.is_video()) ||
      (format == "raw" && file.format == raw_format) || (format == "jpeg" && file.format == jpeg_format);
}
bool CameraCard::is_new(const CardItem& item, const QString& format) const {
  for (const auto& file : item.files) if (matches(item, file, format) && !imported(item, file)) return true;
  return false;
}
const CardItem* CameraCard::find(const QString& key) const {
  for (const auto& item : items_) if (item.key() == key) return &item;
  return nullptr;
}
void CameraCard::refresh() {
  if (!connected_ || busy()) return;
  active_ = {"list", request_id(), {}, {}, false, false, {}}; list_request_ = active_.request;
  canceled_ = false; total_ = completed_ = failed_ = 0; list_warnings_.clear(); items_.clear(); emit catalog_changed(); emit busy_changed(true);
  emit status_changed(QCoreApplication::translate("App", "Читаю список файлов карты без загрузки оригиналов…"));
  if (!session_->send_command("card list " + active_.request)) finish(QCoreApplication::translate("App", "Не удалось запросить список камеры"));
}
void CameraCard::preview(const QString& key, bool thumbnail, bool full_jpeg) {
  if (!connected_ || busy()) return;
  const auto* item = find(key); if (!item) return;
  auto file = item->preview_file(); bool original = false;
  if (full_jpeg) for (const auto& candidate : item->files) if (candidate.format == jpeg_format) { file = candidate; original = true; break; }
  queue_.enqueue({"preview", request_id(), key, file, thumbnail, false, {}, original, full_jpeg && !original});
  canceled_ = false; total_ = completed_ = failed_ = 0; dispatch();
}
void CameraCard::original(const QString& key) {
  const auto* item = find(key);
  if (!connected_ || busy() || !item) return;
  const auto file = item->preview_file();
  if (!file.size) {
    const auto message = QCoreApplication::translate("App", "Файл недоступен");
    emit original_failed(key, message); emit status_changed(message); return;
  }
  const auto identity = ledger_key(*item, file);
  active_ = {"original", request_id(), key, file};
  canceled_ = false; total_ = completed_ = failed_ = 0;
  if (original_key_ == identity && (original_temporary_ || imported(*item, file)) && QFileInfo(original_result_.path).isFile() &&
      quint64(QFileInfo(original_result_.path).size()) == file.size) {
    emit busy_changed(true);
    // Defer delivery so selecting a cached item has the same ordering as a transfer.
    const auto request = active_.request;
    QTimer::singleShot(0, this, [this, key, request] {
      if (active_.request != request) return;
      if (!canceled_) emit original_ready(key, original_result_);
      finish();
    });
  } else if (imported(*item, file)) {
    emit busy_changed(true); emit progress_changed(0, 0, 0);
    read_original(ledger_.value(identity).toObject().value("path").toString(), false);
  } else {
    queue_.enqueue(active_); active_ = {}; dispatch();
  }
}
void CameraCard::read_original(const QString& path, bool temporary) {
  const auto task = active_;
  original_cancel_ = std::make_shared<std::atomic<bool>>(canceled_);
  const auto token = original_cancel_;
  emit progress_changed(0, 0, 95);
  emit status_changed(QCoreApplication::translate("App", "Открываю оригинал в исходном размере…"));
  auto* watcher = new QFutureWatcher<MediaResult>(this);
  connect(watcher, &QFutureWatcher<MediaResult>::finished, this, [this, watcher, path, temporary, task] {
    const auto result = watcher->result(); watcher->deleteLater();
    if (!connected_ || active_.request != task.request || canceled_ || result.image.isNull()) {
      if (temporary) QFile::remove(path);
      if (active_.request == task.request) {
        const auto message = canceled_ ? QCoreApplication::translate("App", "Просмотр оригинала отменён") : result.error;
        emit original_failed(task.key, message); finish(message);
      }
      return;
    }
    const auto* item = find(task.key);
    const auto old_path = original_temporary_ ? original_result_.path : QString();
    original_result_ = result; original_temporary_ = temporary;
    original_key_ = item ? ledger_key(*item, task.file) : QString();
    emit original_ready(task.key, original_result_);
    if (!old_path.isEmpty() && old_path != path) QFile::remove(old_path);
    original_cancel_.reset(); finish(QString(QCoreApplication::translate("App", "Оригинал открыт · %1 × %2")).arg(result.image.width()).arg(result.image.height()));
  });
  watcher->setFuture(QtConcurrent::run([path, task, token] {
    if (task.file.format == jpeg_format || task.file.format == 0xb110) {
      QImageReader reader(path); const auto size = reader.size();
      if (!size.isValid() || qint64(size.width()) * size.height() > 120000000 || QFileInfo(path).size() > 128 * 1024 * 1024) {
        MediaResult result; result.error = QCoreApplication::translate("App", "Изображение недоступно или превышает лимит 120 Мп / 128 MiB"); return result;
      }
    }
    return MediaReader::read(path, false, token);
  }));
}
QString CameraCard::movie_socket_path() const {
  return QDir(session_->card_cache_path()).filePath("movie-" + QUuid::createUuid().toString(QUuid::Id128) + ".sock");
}
void CameraCard::play_movie(const QString& key, const QString& socket) {
  const auto* item = find(key);
  if (!connected_ || busy() || !item || !item->is_video()) return;
  Task task{"movie", request_id(), key, item->preview_file(), false, false, socket};
  queue_.enqueue(task); canceled_ = false; total_ = completed_ = failed_ = 0; dispatch();
}
void CameraCard::movie_control(const QString& operation, int seconds) {
  if (!playing_video() || (operation != "play" && operation != "pause" && operation != "seek") || seconds < 0 || seconds > 86400) return;
  session_->send_command("card movie-" + operation + ' ' + active_.request + (operation == "seek" ? ' ' + QString::number(seconds) : ""));
}
void CameraCard::copy(const QStringList& keys, const QString& format, CopyMode mode) {
  if (!connected_ || busy() || directory_.isEmpty()) return;
  canceled_ = false; completed_ = failed_ = 0;
  const auto previous = saved_queue_; saved_queue_ = {};
  for (const auto& item : items_) {
    if (mode == CopyMode::Selected && !keys.contains(item.key())) continue;
    for (const auto& file : item.files) {
      // All is an explicit full copy; New/Selected reuse confirmed local files.
      if (!matches(item, file, format) || (mode != CopyMode::All && imported(item, file))) continue;
      queue_.enqueue({"copy", request_id(), item.key(), file, false, false, {}});
      saved_queue_.append(QJsonObject{{"key", ledger_key(item, file)}, {"identity", item.identity}, {"slot", item.slot},
          {"path", file.path}, {"size", QString::number(file.size)}, {"format", file.format}});
    }
  }
  total_ = queue_.size();
  if (!total_) { saved_queue_ = previous; emit status_changed(QCoreApplication::translate("App", "Нет новых файлов для копирования в выбранном формате и каталоге")); return; }
  if (!QDir().mkpath(directory_) || !save_pending()) {
    saved_queue_ = previous; queue_.clear(); total_ = 0;
    emit status_changed(QCoreApplication::translate("App", "Не удалось сохранить очередь импорта. Копирование не начато.")); return;
  }
  dispatch();
}
void CameraCard::resume_copy() {
  if (!connected_ || busy() || saved_queue_.isEmpty()) return;
  if (items_.isEmpty() || dirty_) { resume_after_list_ = true; refresh(); return; }
  QHash<QString, Task> available;
  for (const auto& item : items_) for (const auto& file : item.files)
    available.insert(ledger_key(item, file), {"copy", request_id(), item.key(), file, false, false, {}});
  QJsonArray remaining; int missing = 0;
  for (const auto& value : saved_queue_) {
    const auto entry = value.toObject(); const auto found = available.constFind(entry.value("key").toString());
    if (found == available.constEnd()) { remaining.append(entry); ++missing; continue; }
    const auto* item = find(found->key);
    if (!item || item->identity != entry.value("identity").toString() || item->slot != entry.value("slot").toInt() ||
        found->file.path != entry.value("path").toString() || QString::number(found->file.size) != entry.value("size").toString() ||
        found->file.format != entry.value("format").toInt()) { remaining.append(entry); ++missing; continue; }
    if (!imported(*item, found->file)) { queue_.enqueue(*found); remaining.append(entry); }
  }
  saved_queue_ = remaining;
  if (!save_pending()) { queue_.clear(); emit status_changed(QCoreApplication::translate("App", "Не удалось сохранить очередь импорта. Копирование не начато.")); return; }
  canceled_ = false; completed_ = failed_ = 0; total_ = queue_.size(); emit catalog_changed();
  if (!total_) {
    emit status_changed(missing ? QCoreApplication::translate("App", "Кадры сохранённой очереди отсутствуют или изменились. Проверь камеру и карту.") :
        QCoreApplication::translate("App", "Все файлы сохранённой очереди уже скопированы.")); return;
  }
  if (missing) emit session_->log_received(QString(QCoreApplication::translate("App", "Не найдены файлы сохранённой очереди: %1")).arg(missing));
  dispatch();
}
void CameraCard::verify_copies() {
  if (busy() || ledger_.isEmpty() || directory_.isEmpty()) return;
  active_ = {"verify", request_id(), {}, {}, false, false, {}}; total_ = completed_ = failed_ = 0; canceled_ = false;
  emit busy_changed(true); emit progress_changed(0, 0, 0);
  emit status_changed(QCoreApplication::translate("App", "Проверяю SHA256 локальных копий. Файлы не изменяются…"));
  verification_.setFuture(QtConcurrent::run(verify_imports, directory_, ledger_));
}
void CameraCard::remove(const QStringList& keys) {
  if (!connected_ || busy()) return;
  canceled_ = false; completed_ = failed_ = 0;
  for (const auto& item : items_) if (keys.contains(item.key()) && item.deletable && !item.protected_file)
    queue_.enqueue({"delete", request_id(), item.key(), {}, false, false, {}});
  total_ = queue_.size(); dispatch();
}
void CameraCard::cancel() {
  if (!busy()) return;
  if (active_.kind == "verify") { verification_.cancel(); return; }
  canceled_ = true; queue_.clear();
  if (original_cancel_) original_cancel_->store(true);
  else session_->send_command("card cancel");
  emit status_changed(QCoreApplication::translate("App", "Останавливаю операцию; ожидаю ответ камеры…"));
}
void CameraCard::dispatch() {
  if (!connected_ || queue_.isEmpty()) { emit busy_changed(false); return; }
  active_ = queue_.dequeue();
  const auto* item = find(active_.key);
  if (!item) { finish(QCoreApplication::translate("App", "Кадр больше не доступен")); return; }
  QString command = QString("card %1 %2 %3 %4 %5").arg(active_.kind == "original" ? "preview" : active_.kind, active_.request, epoch_).arg(item->slot).arg(item->id);
  if (active_.kind != "delete") command += ' ' + QString::number(active_.file.id) + ' ' +
      (active_.kind == "movie" ? QString::fromLatin1(active_.socket.toUtf8().toHex()) : active_.kind == "copy" ? QString::fromLatin1(directory_.toUtf8().toHex()) : active_.kind == "original" ? "original" : active_.full_jpeg ? "jpeg" : active_.thumbnail ? "thumb" : "screen");
  if (playing_video()) emit movie_viewing(true);
  emit busy_changed(true);
  emit status_changed((active_.kind == "copy" ? QCoreApplication::translate("App", "Копирование: ") : active_.kind == "delete" ? QCoreApplication::translate("App", "Удаление: ") : QCoreApplication::translate("App", "Превью: ")) + item->name());
  emit progress_changed(completed_, total_, 0);
  if (!session_->send_command(command)) finish(QCoreApplication::translate("App", "Команда не отправлена"));
}
void CameraCard::finish(const QString& message) {
  original_cancel_.reset();
  const bool batch = active_.kind == "copy" || active_.kind == "delete";
  if (batch) { ++completed_; if (!message.isEmpty()) ++failed_; }
  if (playing_video()) emit movie_viewing(false);
  active_ = {}; list_request_.clear();
  if (!message.isEmpty()) emit status_changed(message);
  if (!queue_.isEmpty() && !canceled_) { QTimer::singleShot(0, this, &CameraCard::dispatch); return; }
  emit progress_changed(completed_, total_, total_ ? 0 : 100);
  if (batch) emit status_changed(canceled_ ? QCoreApplication::translate("App", "Операция остановлена. Завершённые копии сохранены.") :
      QString(QCoreApplication::translate("App", "Обработано файлов/кадров: %1; ошибок: %2%3")).arg(completed_).arg(failed_).arg(message.isEmpty() ? "" : " · " + message));
  emit busy_changed(false);
  if (studio_pending_ && studio_ && connected_ && !busy() && !studio_timer_.isActive()) studio_timer_.start();
}
QString CameraCard::record(const CardItem& item, const CardFile& file, const QString& path, const QString& digest) {
  const QFileInfo info(path);
  if (!info.isFile() || (file.size && quint64(info.size()) != file.size)) return QCoreApplication::translate("App", "Полученная копия не прошла проверку размера");
  static const QRegularExpression pattern("^[0-9a-f]{64}$");
  if (!pattern.match(digest).hasMatch()) return QCoreApplication::translate("App", "Копия получена без контрольной суммы SHA256. Обнови backend.");
  const auto key = ledger_key(item, file);
  const auto previous = ledger_;
  ledger_.insert(key, QJsonObject{{"path", info.absoluteFilePath()}, {"size", QString::number(info.size())}, {"sha256", digest},
      {"mtime", QString::number(info.lastModified().toMSecsSinceEpoch())}, {"valid", true}});
  if (!save_json(ledger_path(), ledger_)) {
    ledger_ = previous;
    return QCoreApplication::translate("App", "Файл скопирован, но журнал импорта не сохранён. При повторном импорте возможна дополнительная копия.");
  }
  QJsonArray remaining;
  for (const auto& value : saved_queue_) if (value.toObject().value("key").toString() != key) remaining.append(value);
  saved_queue_ = remaining;
  if (!save_pending()) return QCoreApplication::translate("App", "Копия сохранена, но очередь импорта не обновлена.");
  return {};
}
void CameraCard::receive(const QJsonObject& event) {
  if (!connected_) return;
  const auto type = event.value("type").toString();
  if (type == "changed") {
    dirty_ = true;
    emit status_changed(QCoreApplication::translate("App", "Содержимое карты изменилось. Обнови список после съёмки."));
    // Studio quality is chosen independently from card browsing quality.
    if (studio_) {
      studio_pending_ = true;
      if (!busy()) studio_timer_.start();
    }
    return;
  }
  if (active_.request.isEmpty() || event.value("request").toString() != active_.request) return;
  if (type == "begin" && active_.kind == "list") { epoch_ = event.value("epoch").toString(); return; }
  if (type == "item" && active_.kind == "list") {
    CardItem item; item.slot = event.value("slot").toInt(); item.id = quint32(event.value("id").toDouble());
    item.stamp = event.value("stamp").toString(); item.capture_date = event.value("capture_date").toString(); item.identity = event.value("identity").toString();
    item.protected_file = event.value("protected").toBool(); item.deletable = event.value("deletable").toBool();
    for (const auto& value : event.value("files").toArray()) {
      const auto f = value.toObject();
      CardFile file{f.value("id").toInt(), f.value("format").toInt(), f.value("size").toString().toULongLong(), f.value("path").toString()};
      if (!file.path.isEmpty()) item.files.append(file);
    }
    if ((item.slot == 1 || item.slot == 2) && !item.identity.isEmpty() && !item.files.isEmpty() && !find(item.key())) items_.append(item);
  } else if (type == "end" && active_.kind == "list") {
    dirty_ = event.value("stale").toBool();
    std::sort(items_.begin(), items_.end(), [](const CardItem& a, const CardItem& b) { return a.stamp > b.stamp; });
    emit catalog_changed(); finish(QString(QCoreApplication::translate("App", "Кадров на карте: %1%2")).arg(items_.size()).arg(dirty_ ? QCoreApplication::translate("App", " · список изменился, обнови его") : "") + (list_warnings_.isEmpty() ? "" : " · " + list_warnings_.join("; ")));
    if (resume_after_list_) {
      resume_after_list_ = false;
      if (!dirty_) resume_copy();
      else emit status_changed(QCoreApplication::translate("App", "Карта менялась во время чтения. Обнови список перед продолжением импорта."));
    }
    else if (studio_list_ && studio_) {
      studio_list_ = false;
      const auto image = std::find_if(items_.begin(), items_.end(), [](const CardItem& item) { return !item.is_video(); });
      if (dirty_) { studio_pending_ = true; studio_timer_.start(); }
      else if (image != items_.end() && image->identity != last_studio_identity_) { preview(image->key(), false, studio_full_jpeg_); active_.studio = true; }
    }
  } else if (type == "slot-error" && active_.kind == "list") {
    list_warnings_.append(QString(QCoreApplication::translate("App", "Слот %1: %2")).arg(event.value("slot").toInt()).arg(event.value("message").toString()));
    emit status_changed(list_warnings_.last());
    emit session_->log_received(QString(QCoreApplication::translate("App", "Карта: слот %1 · %2")).arg(event.value("slot").toInt()).arg(event.value("message").toString()));
  } else if (type == "movie-state" && playing_video()) {
    emit movie_state(event.value("state").toString());
  } else if (type == "movie-stopped" && playing_video()) {
    finish(QCoreApplication::translate("App", "Видеопросмотр остановлен"));
  } else if (type == "progress") {
    int percent = std::clamp(event.value("percent").toInt(), 0, 100);
    if (active_.kind == "copy") {
      const bool hashing = event.value("phase").toString() == "hash";
      percent = hashing ? 90 + percent * 9 / 100 : percent * 90 / 100;
      if (hashing) emit status_changed(QCoreApplication::translate("App", "Вычисляю SHA256 перед сохранением…"));
    } else if (active_.kind == "original") percent = percent * 90 / 100;
    emit progress_changed(completed_, total_, percent);
  }
  else if (type == "preview" && active_.kind == "original") {
    const auto path = event.value("path").toString();
    if (canceled_ || quint64(QFileInfo(path).size()) != active_.file.size) {
      QFile::remove(path); const auto message = QCoreApplication::translate("App", "Оригинал отменён или получен не полностью");
      emit original_failed(active_.key, message); finish(message); return;
    }
    read_original(path, true);
  } else if (type == "preview" && active_.kind == "preview") {
    const auto path = event.value("path").toString();
    const auto task = active_;
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, path, task] {
      const auto image = watcher->result(); watcher->deleteLater(); QFile::remove(path);
      if (!connected_ || active_.request != task.request) return;
      if (!image.isNull() && !canceled_) {
        if (!task.studio) emit preview_ready(task.key, image, task.thumbnail);
        if (task.studio) {
          const auto* item = find(task.key);
          if (item) last_studio_identity_ = item->identity;
          auto source = task.full_jpeg ? QCoreApplication::translate("App", "JPEG с карты · исходный размер") :
              task.fallback ? QCoreApplication::translate("App", "JPEG на карте нет · сжатое превью") : QCoreApplication::translate("App", "Сжатое превью камеры");
          emit studio_ready(image, task.file.path, source + QString(" · %1 × %2").arg(image.width()).arg(image.height()));
        }
      }
      finish(image.isNull() ? task.full_jpeg ? QCoreApplication::translate("App", "Не удалось прочитать JPEG с карты.") :
          QCoreApplication::translate("App", "Камера вернула нечитаемое превью. Оригинал не загружался.") : "");
    });
    watcher->setFuture(QtConcurrent::run([path, task] {
      if (QFileInfo(path).size() > (task.full_jpeg ? 128 : 32) * 1024 * 1024) return QImage();
      QImageReader reader(path); reader.setAutoTransform(true);
      const auto size = reader.size();
      return size.isValid() && qint64(size.width()) * size.height() <= (task.full_jpeg ? 120000000 : 24000000) ? reader.read() : QImage();
    }));
  } else if (type == "copied" && active_.kind == "copy") {
    const auto* item = find(active_.key); const auto path = event.value("path").toString();
    const auto canonical = QFileInfo(path).canonicalFilePath();
    const auto relative = QDir(directory_).relativeFilePath(canonical);
    if (canonical.isEmpty() || relative == ".." || relative.startsWith("../") || QFileInfo(path).isSymLink()) { finish(QCoreApplication::translate("App", "Неверный путь полученного файла")); return; }
    const auto message = item ? record(*item, active_.file, path, event.value("sha256").toString()) : QString(QCoreApplication::translate("App", "Копия получена, но каталог изменился"));
    emit copied(path); emit item_status_changed(active_.key); finish(message);
  } else if (type == "deleted" && active_.kind == "delete") {
    const auto key = active_.key;
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const CardItem& item) { return item.key() == key; }), items_.end());
    emit catalog_changed(); finish();
  } else if (type == "error") {
    const auto message = event.value("code") == "movie-no-data" ?
        QString(QCoreApplication::translate("App", "SDK не передал видеокадры. Открой оригинал для просмотра. Если поток не работает ни с одним роликом, проверь входящие TCP 55001, 55003, 55005 от %1 к %2. Справка: F1 → Видео.")).arg(event.value("camera_host").toString(), event.value("computer_ip").toString()) : event.value("message").toString();
    if (playing_video()) emit movie_failed(message);
    if (active_.kind == "original") emit original_failed(active_.key, message);
    if (active_.kind == "delete") queue_.clear(); // Reconfirm remaining deletions after any failure.
    studio_list_ = false;
    emit session_->log_received(QCoreApplication::translate("App", "Карта: ") + message);
    finish(message);
  }
}
