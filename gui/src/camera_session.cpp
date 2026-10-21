#include "camera_session.h"
#include <QDir>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <algorithm>

CameraSession::CameraSession(QObject* parent) : QObject(parent) {
  process_.setProcessChannelMode(QProcess::MergedChannels);
  for (auto* timer : {&retry_, &connection_timeout_, &terminate_timeout_, &kill_timeout_}) timer->setSingleShot(true);
  connect(&retry_, &QTimer::timeout, this, &CameraSession::launch);
  connect(&connection_timeout_, &QTimer::timeout, this, [this] {
    emit log_received("Камера не ответила за 45 секунд. Перезапуск соединения.");
    terminate_process();
  });
  connect(&terminate_timeout_, &QTimer::timeout, this, [this] {
    process_.terminate();
    kill_timeout_.start(1500);
  });
  connect(&kill_timeout_, &QTimer::timeout, &process_, &QProcess::kill);
  connect(&process_, &QProcess::readyReadStandardOutput, this, &CameraSession::consume_output);
  connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &CameraSession::process_finished);
  connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
      connection_timeout_.stop();
      desired_ = false;
      set_state(State::Error, "Не удалось запустить backend: " + process_.errorString());
    }
  });
}
CameraSession::~CameraSession() {
  // waitForFinished emits QProcess signals synchronously during destruction.
  QObject::disconnect(&process_, nullptr, this, nullptr);
  desired_ = false;
  retry_.stop();
  if (process_.state() != QProcess::NotRunning) {
    process_.write("quit\n");
    process_.closeWriteChannel();
    if (!process_.waitForFinished(1000)) {
      process_.terminate();
      if (!process_.waitForFinished(1000)) {
        process_.kill();
        process_.waitForFinished(1000);
      }
    }
  }
}
QString CameraSession::card_cache_path() const { return frames_.filePath("card"); }
QString CameraSession::liveview_path() const { return frames_.filePath("liveview.jpg"); }
void CameraSession::set_state(State state, const QString& message) {
  state_ = state;
  emit state_changed(state, message);
}
void CameraSession::start(const ConnectionOptions& options) {
  if (active()) return;
  options_ = options;
  if (!QFileInfo(options.binary).isFile() || !QFileInfo(options.binary).isExecutable() || !frames_.isValid()) {
    set_state(State::Error, "Backend не найден или временный каталог недоступен. Переустанови приложение либо укажи --sonshell.");
    return;
  }
  desired_ = true;
  attempts_ = 0;
  launch();
}
void CameraSession::launch() {
  if (!desired_ || process_.state() != QProcess::NotRunning) return;
  pending_.clear();
  QFile::remove(liveview_path());
  QDir(card_cache_path()).removeRecursively();
  QDir().mkpath(card_cache_path());
  process_.setProgram(options_.binary);
  process_.setWorkingDirectory(QFileInfo(options_.binary).absolutePath());
  process_.setArguments({"--gui", "--auth-env", "--host", options_.host,
      "--model", options_.model, "--sync-dir", options_.folder, "--liveview-file", liveview_path(), "--camera-cache", card_cache_path()});
  auto environment = QProcessEnvironment::systemEnvironment();
  // Sony's dlopen-loaded monitor library has no RUNPATH for its own sibling.
  // Restrict the loader adjustment to this child; never modify global ld config.
  const QString backend_directory = QFileInfo(options_.binary).absolutePath();
  QString loader_path = backend_directory + ':' + backend_directory + "/CrAdapter";
  if (!environment.value("LD_LIBRARY_PATH").isEmpty()) loader_path += ':' + environment.value("LD_LIBRARY_PATH");
  environment.insert("LD_LIBRARY_PATH", loader_path);
  environment.insert("SONSHELL_CAMERA_USER", options_.user);
  environment.insert("SONSHELL_CAMERA_PASSWORD", options_.password);
  process_.setProcessEnvironment(environment);
  set_state(State::Connecting, "Подключение к " + options_.host + "…");
  process_.start();
  connection_timeout_.start(45000);
}
void CameraSession::consume_output() {
  pending_ += process_.readAllStandardOutput();
  qsizetype newline = -1;
  while ((newline = pending_.indexOf('\n')) >= 0) {
    const QByteArray line = pending_.left(newline);
    pending_.remove(0, newline + 1);
    handle_line(line);
  }
  if (pending_.size() > 64 * 1024) pending_.clear();
}
void CameraSession::handle_line(const QByteArray& bytes) {
  QString line = QString::fromUtf8(bytes);
  static const QRegularExpression ansi("\\x1B\\[[0-9;?]*[ -/]*[@-~]");
  line.remove(ansi);
  line.remove('\r');
  QString sanitized = line;
  if (!options_.password.isEmpty()) sanitized.replace(options_.password, "[скрыто]");
  const bool file_event = line.startsWith("INFO  | [INFO] REMOTE/1 card ") || line.startsWith("INFO  | [INFO] REMOTE/1 download-complete ") ||
      line.startsWith("INFO  | [INFO] REMOTE/1 transfer-progress ");
  if (!file_event && !sanitized.trimmed().isEmpty()) emit log_received(sanitized);
  if (!desired_) return;
  const QString event_prefix = "INFO  | [INFO] REMOTE/1 ";
  if (line.startsWith(event_prefix + "card ")) {
    const auto encoded = line.mid(event_prefix.size() + 5);
    static const QRegularExpression hex_json("^(?:[0-9a-f]{2})+$");
    if (encoded.size() > 60000 || !hex_json.match(encoded).hasMatch() || state_ != State::Connected) return;
    const auto event = QJsonDocument::fromJson(QByteArray::fromHex(encoded.toLatin1())).object();
    const auto type = event.value("type").toString();
    if (type == "fatal") {
      emit log_received(event.value("message").toString());
      set_state(State::Connecting, "Перезапуск соединения после ошибки передачи…");
      terminate_process(); return;
    }
    if (type == "preview" || type == "copied") {
      const auto path = event.value("path").toString();
      const auto root = type == "preview" ? card_cache_path() : QString();
      // Import destinations are chosen by the GUI; CameraCard checks the selected directory.
      if (!QFileInfo(path).isAbsolute()) return;
      if (!root.isEmpty()) {
        const auto relative = QDir(root).relativeFilePath(path);
        if (relative == ".." || relative.startsWith("../") || QFileInfo(path).isSymLink()) return;
      }
    }
    if (!event.isEmpty()) emit card_event(event);
    return;
  }
  if (line.startsWith(event_prefix + "download-complete ") || line.startsWith(event_prefix + "transfer-progress ")) {
    const auto parts = line.mid(event_prefix.size()).split(' ');
    static const QRegularExpression hex("^(?:[0-9a-f]{2})+$");
    if (parts.size() == 3 && parts[2].size() <= 32768 && hex.match(parts[2]).hasMatch()) {
      const QString path = QString::fromUtf8(QByteArray::fromHex(parts[2].toLatin1()));
      const auto relative = QDir(options_.folder).relativeFilePath(path);
      if (QFileInfo(path).isAbsolute() && relative != ".." && !relative.startsWith("../")) {
        QString filename = QFileInfo(path).fileName();
        if (!options_.password.isEmpty()) filename.replace(options_.password, "[скрыто]");
        if (parts[0] == "download-complete" && (parts[1] == "new" || parts[1] == "sync")) {
          emit log_received("Получен файл: " + filename);
          emit download_completed(path, parts[1] == "new");
        }
        else if (parts[0] == "transfer-progress") {
          bool ok = false; const int percent = parts[1].toInt(&ok);
          if (ok && percent >= 0 && percent <= 100) {
            emit log_received(QString("Передача: %1 · %2%").arg(filename).arg(percent));
            emit transfer_progress(path, percent);
          }
        }
      }
    }
    return;
  }
  // Only complete, versioned event lines can change camera state.
  if (line == QString("INFO  | [INFO] REMOTE/1 connected")) {
    connection_timeout_.stop();
    attempts_ = 0;
    set_state(State::Connected, "Подключено · " + options_.host);
    emit connected();
  } else if (line == QString("INFO  | [INFO] REMOTE/1 disconnected")) {
    set_state(State::Connecting, "Соединение с камерой потеряно…");
    emit liveview_changed(false);
    terminate_process();
  } else if (line == QString("INFO  | [INFO] REMOTE/1 liveview-started")) emit liveview_changed(true);
  else if (line == QString("INFO  | [INFO] REMOTE/1 liveview-failed") ||
           line == QString("INFO  | [INFO] REMOTE/1 liveview-stopped")) emit liveview_changed(false);
}
void CameraSession::process_finished() {
  consume_output();
  if (!pending_.isEmpty()) { handle_line(pending_); pending_.clear(); }
  connection_timeout_.stop();
  terminate_timeout_.stop();
  kill_timeout_.stop();
  emit liveview_changed(false);
  finish_or_retry();
}
void CameraSession::finish_or_retry() {
  if (desired_ && options_.auto_reconnect) {
    const int delay = std::min(30000, retry_base_ms_ * (1 << std::min(attempts_++, 4)));
    set_state(State::RetryWaiting, QString("Нет связи · повтор через %1 с").arg(delay / 1000.0, 0, 'f', 1));
    retry_.start(delay);
  } else {
    desired_ = false;
    set_state(State::Disconnected, "Отключено");
  }
}
void CameraSession::terminate_process() {
  connection_timeout_.stop();
  if (process_.state() == QProcess::NotRunning) return;
  process_.write("quit\n");
  process_.closeWriteChannel();
  terminate_timeout_.start(1500);
}
void CameraSession::stop() {
  desired_ = false;
  retry_.stop();
  if (process_.state() == QProcess::NotRunning) {
    set_state(State::Disconnected, "Отключено");
    return;
  }
  set_state(State::Stopping, "Отключение…");
  terminate_process();
}
void CameraSession::set_auto_reconnect(bool enabled) {
  options_.auto_reconnect = enabled;
  if (!enabled && state_ == State::RetryWaiting) stop();
}
bool CameraSession::send_command(const QString& command) {
  if (state_ != State::Connected || command.contains('\n') || command.contains('\r')) return false;
  const auto bytes = (command + '\n').toUtf8();
  return process_.write(bytes) == bytes.size();
}
