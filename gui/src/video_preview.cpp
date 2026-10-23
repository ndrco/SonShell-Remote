#include "video_preview.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
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
      if (!socket_ || intentional_) return;
      if (!stream_started_ && socket_->bytesAvailable()) {
        stream_started_ = true;
        launch({"-f", "hevc", "-probesize", "8388608", "-analyzeduration", "500000", "-protocol_whitelist", "pipe", "-i", "pipe:0"});
      }
      if (decoder_.state() != QProcess::Running) return;
      while (socket_->bytesAvailable() && decoder_.bytesToWrite() < 256 * 1024)
        decoder_.write(socket_->read(64 * 1024));
      if (socket_->state() == QLocalSocket::UnconnectedState && !socket_->bytesAvailable() && !decoder_.bytesToWrite()) decoder_.closeWriteChannel();
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
  paused_ = false; stream_started_ = false; frames_.clear(); frame_size_ = {}; payload_size_ = 0;
  if (decoder_.isOpen()) { decoder_.readAllStandardOutput(); decoder_.readAllStandardError(); }
}
void VideoPreview::launch(const QStringList& input) {
  intentional_ = false; received_ = false; diagnostic_.clear();
  decoder_.setProgram(QStandardPaths::findExecutable("ffmpeg"));
  decoder_.setArguments(QStringList{"-hide_banner", "-loglevel", "error", "-threads", "2", "-filter_threads", "1"} + input +
      QStringList{"-an", "-fps_mode", "passthrough", "-pix_fmt", "rgb24", "-c:v", "ppm", "-threads", "1", "-f", "image2pipe", "pipe:1"});
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
  // No decoder until data arrives. A blocked camera must report the SDK/network
  // error, rather than an unrelated FFmpeg error about an empty input stream.
  intentional_ = false;
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
  // FFmpeg emits binary PPM: the original frame size/rate and uncompressed RGB.
  // Parse the bounded header separately; JPEG re-encoding used to lose detail.
  constexpr qint64 max_frame_bytes = 128 * 1024 * 1024;
  static const QRegularExpression header("\\AP6\\n([0-9]+) ([0-9]+)\\n255\\n");
  for (;;) {
    if (!payload_size_) {
      const auto match = header.match(QString::fromLatin1(frames_.left(80)));
      if (!match.hasMatch()) {
        if (frames_.size() < 80) break;
        stop(); emit failed(QCoreApplication::translate("App", "Не удалось декодировать видео")); return;
      }
      const auto width = match.captured(1).toInt(), height = match.captured(2).toInt();
      const qint64 pixels = qint64(width) * height;
      if (width <= 0 || height <= 0 || pixels > max_frame_bytes / 3) {
        stop(); emit failed(QCoreApplication::translate("App", "Превышен лимит буфера видеопревью")); return;
      }
      frame_size_ = {width, height}; payload_size_ = pixels * 3;
      frames_.remove(0, match.capturedLength());
    }
    if (frames_.size() < payload_size_) break;
    const auto image = QImage(reinterpret_cast<const uchar*>(frames_.constData()), frame_size_.width(), frame_size_.height(), frame_size_.width() * 3, QImage::Format_RGB888).copy();
    frames_.remove(0, payload_size_); payload_size_ = 0;
    if (!image.isNull()) { received_ = true; emit frame_ready(image); }
    if (intentional_) return;
  }
}
