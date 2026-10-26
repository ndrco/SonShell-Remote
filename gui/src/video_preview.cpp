#include "video_preview.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSignalBlocker>
#include <csignal>
#include <algorithm>
#include <cmath>

VideoPreview::VideoPreview(QObject* parent) : QObject(parent) {
  connect(&decoder_, &QProcess::readyReadStandardOutput, this, &VideoPreview::read_frames);
  connect(&decoder_, &QProcess::readyReadStandardError, this, &VideoPreview::read_progress);
  connect(&decoder_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
    read_frames(); read_progress();
    if (intentional_) return;
    if (code != 0 || !received_) emit failed(QCoreApplication::translate("App", "Не удалось декодировать видео") + ": " + QString::fromUtf8(diagnostic_).left(600));
    emit finished();
  });
  connect(&decoder_, &QProcess::errorOccurred, this, [this](auto error) {
    if (error == QProcess::FailedToStart) emit failed(QCoreApplication::translate("App", "FFmpeg недоступен. Переустанови приложение."));
  });
  probe_timeout_.setSingleShot(true); probe_timeout_.setInterval(5000);
  connect(&probe_timeout_, &QTimer::timeout, &probe_, &QProcess::kill);
  connect(&probe_, &QProcess::readyReadStandardOutput, this, [this] {
    probe_output_ += probe_.readAllStandardOutput();
    if (probe_output_.size() > 64 * 1024) { probe_output_.clear(); probe_.kill(); }
  });
  connect(&probe_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
    probe_timeout_.stop();
    if (code || status != QProcess::NormalExit) return;
    probe_output_ += probe_.readAllStandardOutput();
    if (probe_output_.size() > 64 * 1024) return;
    const auto data = QJsonDocument::fromJson(probe_output_).object();
    const double duration = data["format"].toObject()["duration"].toString().toDouble();
    if (!std::isfinite(duration) || duration <= 0 || duration > 86400) return;
    duration_ms_ = qRound64(duration * 1000);
    const auto streams = data["streams"].toArray();
    if (!streams.isEmpty()) {
      const auto ratio = streams.first().toObject()["avg_frame_rate"].toString().split('/');
      const double rate = ratio.size() == 2 && ratio[1].toDouble() > 0 ? ratio[0].toDouble() / ratio[1].toDouble() : 0;
      if (std::isfinite(rate) && rate > 0 && rate <= 1000) frame_interval_ms_ = qint64(std::ceil(1000 / rate));
    }
    emit duration_changed(duration_ms_);
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
  QObject::disconnect(this, nullptr, nullptr, nullptr);
  QObject::disconnect(&decoder_, nullptr, this, nullptr); stop();
  QObject::disconnect(&probe_, nullptr, this, nullptr);
  probe_.kill(); probe_.waitForFinished(500);
}
void VideoPreview::stop() {
  intentional_ = true;
  if (socket_) { QObject::disconnect(socket_, nullptr, this, nullptr); socket_->abort(); socket_->deleteLater(); socket_ = nullptr; }
  server_.close();
  if (decoder_.state() != QProcess::NotRunning) {
    if (paused_) ::kill(pid_t(decoder_.processId()), SIGCONT);
    decoder_.kill(); decoder_.waitForFinished(500);
  }
  paused_ = false; pause_on_frame_ = false; stream_started_ = false; frames_.clear(); frame_size_ = {}; payload_size_ = 0;
  progress_.clear(); decoded_ms_ = 0; position_offset_ms_ = 0; position_ms_ = 0;
  emit position_changed(position_ms_);
  if (decoder_.isOpen()) { decoder_.readAllStandardOutput(); decoder_.readAllStandardError(); }
}
void VideoPreview::launch(const QStringList& input) {
  intentional_ = false; received_ = false; diagnostic_.clear();
  decoder_.setProgram(QStandardPaths::findExecutable("ffmpeg"));
  decoder_.setArguments(QStringList{"-hide_banner", "-loglevel", "error", "-nostats", "-stats_period", "0.1", "-progress", "pipe:2", "-threads", "2", "-filter_threads", "1"} + input +
      QStringList{"-an", "-fps_mode", "passthrough", "-pix_fmt", "rgb24", "-c:v", "ppm", "-threads", "1", "-f", "image2pipe", "pipe:1"});
  decoder_.start();
}
void VideoPreview::play_local(const QString& path, int seconds) {
  start_local(path, qint64(std::max(0, seconds)) * 1000, false);
}
void VideoPreview::set_local_file(const QString& path) {
  if (local_path_ == path) return;
  // Duration probing stays asynchronous; changing files invalidates old output.
  {
    QSignalBlocker blocked(&probe_);
    probe_.kill(); probe_.waitForFinished(500);
    if (probe_.isOpen()) { probe_.readAllStandardOutput(); probe_.readAllStandardError(); }
  }
  probe_timeout_.stop();
  local_path_ = path; duration_ms_ = 0; frame_interval_ms_ = 1000; probe_output_.clear();
  emit duration_changed(0);
  const auto program = QStandardPaths::findExecutable("ffprobe");
  if (path.isEmpty() || program.isEmpty()) return;
  probe_.setProgram(program);
  probe_.setArguments({"-v", "error", "-protocol_whitelist", "file,pipe", "-select_streams", "v:0", "-show_entries", "format=duration:stream=avg_frame_rate", "-of", "json", QFileInfo(path).absoluteFilePath()});
  probe_.start(); probe_timeout_.start();
}
void VideoPreview::start_local(const QString& path, qint64 milliseconds, bool paused) {
  stop();
  if (!QFileInfo(path).isFile()) return;
  set_local_file(path);
  milliseconds = std::clamp(milliseconds, qint64(0), duration_ms_ > 0 ? std::max(qint64(0), duration_ms_ - frame_interval_ms_) : qint64(86400000));
  position_offset_ms_ = milliseconds; position_ms_ = milliseconds; pause_on_frame_ = paused;
  emit position_changed(position_ms_);
  // Pace decoded presentation timestamps. Input -re follows DTS and can burst
  // the opening frames of a short clip with a long decoder preroll.
  launch({"-ss", QString::number(milliseconds / 1000.0, 'f', 3), "-protocol_whitelist", "file,pipe", "-i", QFileInfo(path).absoluteFilePath(), "-vf", "realtime=limit=0.2"});
}
void VideoPreview::seek_local(qint64 milliseconds) {
  start_local(local_path_, milliseconds, paused());
}
void VideoPreview::set_stream_position(qint64 milliseconds) {
  position_ms_ = std::clamp(milliseconds, qint64(0), qint64(86400000));
  position_offset_ms_ = position_ms_ - decoded_ms_;
  emit position_changed(position_ms_);
}
void VideoPreview::read_progress() {
  progress_ += decoder_.isOpen() ? decoder_.readAllStandardError() : QByteArray();
  if (intentional_) { progress_.clear(); return; }
  for (int newline; (newline = progress_.indexOf('\n')) >= 0;) {
    const auto line = progress_.left(newline).trimmed(); progress_.remove(0, newline + 1);
    if (line.startsWith("out_time_us=")) {
      bool ok = false; const auto microseconds = line.mid(12).toLongLong(&ok);
      if (ok && !paused_) {
        decoded_ms_ = std::max(qint64(0), microseconds / 1000);
        position_ms_ = std::max(qint64(0), position_offset_ms_ + decoded_ms_);
        emit position_changed(position_ms_);
      }
    } else {
      const auto key = line.left(line.indexOf('='));
      static const QList<QByteArray> progress_keys{"frame", "fps", "stream_0_0_q", "bitrate", "total_size", "out_time_ms", "out_time", "dup_frames", "drop_frames", "speed", "progress"};
      if (!progress_keys.contains(key)) diagnostic_ = (diagnostic_ + line + '\n').right(3000);
    }
  }
  if (progress_.size() > 4096) { diagnostic_ = progress_.right(3000); progress_.clear(); }
}
bool VideoPreview::listen(const QString& path) {
  stop(); set_local_file({}); server_.setSocketOptions(QLocalServer::UserAccessOption);
  if (!server_.listen(path)) { emit failed(server_.errorString()); return false; }
  // No decoder until data arrives. A blocked camera must report the SDK/network
  // error, rather than an unrelated FFmpeg error about an empty input stream.
  intentional_ = false;
  return true;
}
void VideoPreview::pause_local(bool paused) {
  if (paused && decoder_.state() == QProcess::Starting) { pause_on_frame_ = true; return; }
  if (!paused) pause_on_frame_ = false;
  if (decoder_.state() != QProcess::Running || paused_ == paused) return;
  if (::kill(pid_t(decoder_.processId()), paused ? SIGSTOP : SIGCONT) == 0) {
    paused_ = paused;
    if (!paused_) read_frames();
  }
}
void VideoPreview::read_frames() {
  const auto bytes = decoder_.isOpen() ? decoder_.readAllStandardOutput() : QByteArray();
  if (intentional_) return;
  frames_ += bytes;
  if (paused_) return;
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
    if (!image.isNull()) {
      received_ = true;
      if (pause_on_frame_) { pause_on_frame_ = false; pause_local(true); }
      emit frame_ready(image);
    }
    if (intentional_) return;
    if (paused_) return;
  }
}
