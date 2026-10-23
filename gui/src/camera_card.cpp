#include <QCoreApplication>
#include "camera_card.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QUuid>
#include <algorithm>

namespace {
constexpr int raw_format = 0xb101;
constexpr int jpeg_format = 0x3801;
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
  connect(session_, &CameraSession::card_event, this, &CameraCard::receive);
}
QString CameraCard::request_id() { return prefix_ + QString::number(++sequence_); }
void CameraCard::set_connected(bool connected) {
  if (connected == connected_) return;
  connected_ = connected; prefix_ = QUuid::createUuid().toString(QUuid::Id128) + '-';
  if (playing_video()) emit movie_viewing(false);
  active_ = {}; queue_.clear(); list_request_.clear(); epoch_.clear(); items_.clear();
  dirty_ = true; studio_list_ = false; canceled_ = false;
  emit catalog_changed(); emit busy_changed(false);
  emit status_changed(connected ? QCoreApplication::translate("App", "Открой вкладку и обнови список карты. Оригиналы при съёмке не копируются.") : QCoreApplication::translate("App", "Камера отключена"));
}
QString CameraCard::ledger_path() const { return QDir(directory_).filePath(".sonshell-imports.json"); }
void CameraCard::set_directory(const QString& directory) {
  if (busy()) return;
  directory_ = directory.trimmed().isEmpty() ? QString() : QFileInfo(directory).canonicalFilePath();
  if (directory_.isEmpty() && !directory.trimmed().isEmpty()) directory_ = QDir(directory).absolutePath();
  ledger_ = {};
  QFile file(ledger_path());
  if (file.open(QIODevice::ReadOnly) && file.size() <= 32 * 1024 * 1024) ledger_ = QJsonDocument::fromJson(file.readAll()).object();
  emit catalog_changed();
}
QString CameraCard::ledger_key(const CardItem& item, const CardFile& file) const {
  return QString::fromLatin1(QCryptographicHash::hash((item.identity + ':' + file.path + ':' + QString::number(file.size)).toUtf8(), QCryptographicHash::Sha256).toHex());
}
bool CameraCard::imported(const CardItem& item, const CardFile& file) const {
  const auto entry = ledger_.value(ledger_key(item, file)).toObject();
  const auto path = entry.value("path").toString();
  const auto relative = QDir(directory_).relativeFilePath(path);
  const QFileInfo info(path);
  return !path.isEmpty() && relative != ".." && !relative.startsWith("../") && info.isFile() &&
      !info.isSymLink() && file.size > 0 && quint64(info.size()) == file.size && entry.value("size").toString() == QString::number(file.size);
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
void CameraCard::preview(const QString& key, bool thumbnail) {
  if (!connected_ || busy()) return;
  const auto* item = find(key); if (!item) return;
  queue_.enqueue({"preview", request_id(), key, item->preview_file(), thumbnail, false, {}});
  canceled_ = false; total_ = completed_ = failed_ = 0; dispatch();
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
  for (const auto& item : items_) {
    if (mode == CopyMode::Selected && !keys.contains(item.key())) continue;
    for (const auto& file : item.files) {
      // All is an explicit full copy; New/Selected reuse confirmed local files.
      if (!matches(item, file, format) || (mode != CopyMode::All && imported(item, file))) continue;
      queue_.enqueue({"copy", request_id(), item.key(), file, false, false, {}});
    }
  }
  total_ = queue_.size();
  if (!total_) { emit status_changed(QCoreApplication::translate("App", "Нет новых файлов для копирования в выбранном формате и каталоге")); return; }
  dispatch();
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
  canceled_ = true; queue_.clear(); session_->send_command("card cancel");
  emit status_changed(QCoreApplication::translate("App", "Останавливаю операцию; ожидаю ответ камеры…"));
}
void CameraCard::dispatch() {
  if (!connected_ || queue_.isEmpty()) { emit busy_changed(false); return; }
  active_ = queue_.dequeue();
  const auto* item = find(active_.key);
  if (!item) { finish(QCoreApplication::translate("App", "Кадр больше не доступен")); return; }
  QString command = QString("card %1 %2 %3 %4 %5").arg(active_.kind, active_.request, epoch_).arg(item->slot).arg(item->id);
  if (active_.kind != "delete") command += ' ' + QString::number(active_.file.id) + ' ' +
      (active_.kind == "movie" ? QString::fromLatin1(active_.socket.toUtf8().toHex()) : active_.kind == "copy" ? QString::fromLatin1(directory_.toUtf8().toHex()) : active_.thumbnail ? "thumb" : "screen");
  if (playing_video()) emit movie_viewing(true);
  emit busy_changed(true);
  emit status_changed((active_.kind == "copy" ? QCoreApplication::translate("App", "Копирование: ") : active_.kind == "delete" ? QCoreApplication::translate("App", "Удаление: ") : QCoreApplication::translate("App", "Превью: ")) + item->name());
  emit progress_changed(completed_, total_, 0);
  if (!session_->send_command(command)) finish(QCoreApplication::translate("App", "Команда не отправлена"));
}
void CameraCard::finish(const QString& message) {
  const bool batch = active_.kind == "copy" || active_.kind == "delete";
  if (batch) { ++completed_; if (!message.isEmpty()) ++failed_; }
  if (playing_video()) emit movie_viewing(false);
  active_ = {}; list_request_.clear();
  if (!message.isEmpty()) emit status_changed(message);
  if (!queue_.isEmpty() && !canceled_) { QTimer::singleShot(0, this, &CameraCard::dispatch); return; }
  emit busy_changed(false); emit progress_changed(completed_, total_, total_ ? 0 : 100);
  if (batch) emit status_changed(canceled_ ? QCoreApplication::translate("App", "Операция остановлена. Завершённые копии сохранены.") :
      QString(QCoreApplication::translate("App", "Обработано файлов/кадров: %1; ошибок: %2%3")).arg(completed_).arg(failed_).arg(message.isEmpty() ? "" : " · " + message));
}
QString CameraCard::record(const CardItem& item, const CardFile& file, const QString& path) {
  const QFileInfo info(path);
  if (!info.isFile() || (file.size && quint64(info.size()) != file.size)) return QCoreApplication::translate("App", "Полученная копия не прошла проверку размера");
  ledger_.insert(ledger_key(item, file), QJsonObject{{"path", info.absoluteFilePath()}, {"size", QString::number(info.size())}});
  QSaveFile output(ledger_path());
  if (!output.open(QIODevice::WriteOnly) || !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      output.write(QJsonDocument(ledger_).toJson(QJsonDocument::Compact)) < 0 || !output.commit())
    return QCoreApplication::translate("App", "Файл скопирован, но журнал импорта не сохранён. При повторном импорте возможна дополнительная копия.");
  return {};
}
void CameraCard::receive(const QJsonObject& event) {
  if (!connected_) return;
  const auto type = event.value("type").toString();
  if (type == "changed") {
    dirty_ = true;
    emit status_changed(QCoreApplication::translate("App", "Содержимое карты изменилось. Обнови список после съёмки."));
    // Optional studio review requests only compressed data, never an original.
    if (studio_ && !busy()) {
      studio_list_ = true;
      QTimer::singleShot(500, this, [this] { if (studio_list_ && studio_ && !busy()) refresh(); });
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
    if (studio_list_ && studio_) {
      studio_list_ = false;
      const auto image = std::find_if(items_.begin(), items_.end(), [](const CardItem& item) { return !item.is_video(); });
      if (image != items_.end()) { preview(image->key()); active_.studio = true; }
    }
  } else if (type == "slot-error" && active_.kind == "list") {
    list_warnings_.append(QString(QCoreApplication::translate("App", "Слот %1: %2")).arg(event.value("slot").toInt()).arg(event.value("message").toString()));
    emit status_changed(list_warnings_.last());
    emit session_->log_received(QString(QCoreApplication::translate("App", "Карта: слот %1 · %2")).arg(event.value("slot").toInt()).arg(event.value("message").toString()));
  } else if (type == "movie-state" && playing_video()) {
    emit movie_state(event.value("state").toString());
  } else if (type == "movie-stopped" && playing_video()) {
    finish(QCoreApplication::translate("App", "Видеопросмотр остановлен"));
  } else if (type == "progress") emit progress_changed(completed_, total_, event.value("percent").toInt());
  else if (type == "preview" && active_.kind == "preview") {
    const auto path = event.value("path").toString();
    const auto task = active_;
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, path, task] {
      const auto image = watcher->result(); watcher->deleteLater(); QFile::remove(path);
      if (!connected_ || active_.request != task.request) return;
      if (!image.isNull() && !canceled_) {
        emit preview_ready(task.key, image, task.thumbnail);
        if (task.studio) { const auto* item = find(task.key); emit studio_ready(image, item ? item->name() : ""); }
      }
      finish(image.isNull() ? QCoreApplication::translate("App", "Камера вернула нечитаемое превью. Оригинал не загружался.") : "");
    });
    watcher->setFuture(QtConcurrent::run([path] {
      if (QFileInfo(path).size() > 32 * 1024 * 1024) return QImage();
      QImageReader reader(path); reader.setAutoTransform(true);
      const auto size = reader.size();
      return size.isValid() && qint64(size.width()) * size.height() <= 24000000 ? reader.read() : QImage();
    }));
  } else if (type == "copied" && active_.kind == "copy") {
    const auto* item = find(active_.key); const auto path = event.value("path").toString();
    const auto canonical = QFileInfo(path).canonicalFilePath();
    const auto relative = QDir(directory_).relativeFilePath(canonical);
    if (canonical.isEmpty() || relative == ".." || relative.startsWith("../") || QFileInfo(path).isSymLink()) { finish(QCoreApplication::translate("App", "Неверный путь полученного файла")); return; }
    const auto message = item ? record(*item, active_.file, path) : QString(QCoreApplication::translate("App", "Копия получена, но каталог изменился"));
    emit copied(path); emit catalog_changed(); finish(message);
  } else if (type == "deleted" && active_.kind == "delete") {
    const auto key = active_.key;
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const CardItem& item) { return item.key() == key; }), items_.end());
    emit catalog_changed(); finish();
  } else if (type == "error") {
    const auto message = event.value("code") == "movie-no-data" ?
        QString(QCoreApplication::translate("App", "Нет видеоданных от камеры. Проверь входящие TCP 55001, 55003, 55005 от %1 к %2 в брандмауэре и сети. Справка: F1 → Видео.")).arg(event.value("camera_host").toString(), event.value("computer_ip").toString()) : event.value("message").toString();
    if (playing_video()) emit movie_failed(message);
    if (active_.kind == "delete") queue_.clear(); // Reconfirm remaining deletions after any failure.
    studio_list_ = false;
    emit session_->log_received(QCoreApplication::translate("App", "Карта: ") + message);
    finish(message);
  }
}
