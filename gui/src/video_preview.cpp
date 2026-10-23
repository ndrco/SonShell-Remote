#include "video_preview.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>
#include <csignal>
#include <algorithm>

VideoPreview::VideoPreview(QObject* parent) : QObject(parent) {
  connect(&decoder_, &QProcess::readyReadStandardOutput, this, &VideoPreview::read_frames);
  connect(&decoder_, &QProcess::readyReadStandardError, this, [this] {
    diagnostic_ += decoder_.readAllStandardError(); diagnostic_ = diagnostic_.right(3000);
  });
  connect(&decoder_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
    read_frames();
    if (intentional_) return;
    if (code != 0 || !received_) emit failed(QCoreApplication::translate("App", "Не удалось декодировать видео") + ": " + QString::fromUtf8(diagnostic_).left(600));
    emit finished();
  });
  connect(&decoder_, &QProcess::errorOccurred, this, [this](auto error) {
    if (error == QProcess::FailedToStart) emit failed(QCoreApplication::translate("App", "FFmpeg недоступен. Переустанови приложение."));
  });
  connect(&server_, &QLocalServer::newConnection, this, [this] {
    auto* connection = server_.nextPendingConnection();
    if (socket_) { connection->abort(); connection->deleteLater(); return; }
    socket_ = connection;
    connection->setReadBufferSize(256 * 1024);
    auto pump = [this] {
      if (!socket_ || decoder_.state() != QProcess::Running) return;
      while (socket_->bytesAvailable() && decoder_.bytesToWrite() < 256 * 1024)
        decoder_.write(socket_->read(64 * 1024));
      if (socket_->state() == QLocalSocket::UnconnectedState && !socket_->bytesAvailable()) decoder_.closeWriteChannel();
    };
    connect(connection, &QLocalSocket::readyRead, this, pump);
    connect(&decoder_, &QProcess::bytesWritten, connection, [pump](qint64) { pump(); });
    connect(&decoder_, &QProcess::started, connection, pump);
    connect(connection, &QLocalSocket::disconnected, this, [pump] {
      pump();
      // Keep unread bytes until the decoder has drained them. The server owns the socket.
    });
    pump();
  });
}
VideoPreview::~VideoPreview() {
  QObject::disconnect(&decoder_, nullptr, this, nullptr); stop();
}
void VideoPreview::stop() {
  intentional_ = true;
  if (socket_) { QObject::disconnect(socket_, nullptr, this, nullptr); socket_->abort(); socket_->deleteLater(); socket_ = nullptr; }
  server_.close();
  if (decoder_.state() != QProcess::NotRunning) {
    if (paused_) ::kill(pid_t(decoder_.processId()), SIGCONT);
    decoder_.kill(); decoder_.waitForFinished(500);
  }
  paused_ = false; frames_.clear();
  if (decoder_.isOpen()) { decoder_.readAllStandardOutput(); decoder_.readAllStandardError(); }
}
void VideoPreview::launch(const QStringList& input) {
  intentional_ = false; received_ = false; diagnostic_.clear();
  decoder_.setProgram(QStandardPaths::findExecutable("ffmpeg"));
  decoder_.setArguments(QStringList{"-hide_banner", "-loglevel", "error", "-threads", "2", "-filter_threads", "1"} + input +
      QStringList{"-an", "-vf", "fps=12,scale=w='min(1280,iw)':h='min(720,ih)':force_original_aspect_ratio=decrease", "-c:v", "mjpeg", "-q:v", "5", "-f", "image2pipe", "pipe:1"});
  decoder_.start();
}
void VideoPreview::play_local(const QString& path, int seconds) {
  stop();
  if (!QFileInfo(path).isFile()) return;
  launch({"-re", "-ss", QString::number(std::max(0, seconds)), "-protocol_whitelist", "file,pipe", "-i", QFileInfo(path).absoluteFilePath()});
}
bool VideoPreview::listen(const QString& path) {
  stop(); server_.setSocketOptions(QLocalServer::UserAccessOption);
  if (!server_.listen(path)) { emit failed(server_.errorString()); return false; }
  launch({"-probesize", "65536", "-analyzeduration", "1000000", "-protocol_whitelist", "pipe", "-i", "pipe:0"});
  return true;
}
void VideoPreview::pause_local(bool paused) {
  if (decoder_.state() != QProcess::Running || paused_ == paused) return;
  if (::kill(pid_t(decoder_.processId()), paused ? SIGSTOP : SIGCONT) == 0) paused_ = paused;
}
void VideoPreview::read_frames() {
  const auto bytes = decoder_.isOpen() ? decoder_.readAllStandardOutput() : QByteArray();
  if (intentional_) return;
  frames_ += bytes;
  if (frames_.size() > 8 * 1024 * 1024) { stop(); emit failed(QCoreApplication::translate("App", "Превышен лимит буфера видеопревью")); return; }
  QByteArray latest;
  for (;;) {
    const auto start = frames_.indexOf(QByteArray::fromHex("ffd8"));
    if (start < 0) { if (frames_.size() > 1) frames_ = frames_.right(1); break; }
    if (start > 0) frames_.remove(0, start);
    const auto end = frames_.indexOf(QByteArray::fromHex("ffd9"), 2);
    if (end < 0) break;
    latest = frames_.left(end + 2); frames_.remove(0, end + 2);
  }
  if (!latest.isEmpty()) {
    const auto image = QImage::fromData(latest, "JPEG");
    if (!image.isNull()) { received_ = true; emit frame_ready(image); }
  }
}
