#pragma once
#include <QImage>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QTimer>

// Native-size silent video. The decoder never receives a camera credential or URL.
class VideoPreview final : public QObject {
  Q_OBJECT
 public:
  explicit VideoPreview(QObject* parent = nullptr);
  ~VideoPreview() override;
  void play_local(const QString& path, int seconds = 0);
  void set_local_file(const QString& path);
  void seek_local(qint64 milliseconds);
  void set_stream_position(qint64 milliseconds);
  qint64 position_ms() const { return position_ms_; }
  qint64 duration_ms() const { return duration_ms_; }
  bool paused() const { return paused_ || pause_on_frame_; }
  bool listen(const QString& socket_path);
  void pause_local(bool paused);
  void stop();
  bool running() const { return decoder_.state() == QProcess::Running; }
 signals:
  void frame_ready(const QImage& image);
  void failed(const QString& message);
  void finished();
  void position_changed(qint64 milliseconds);
  void duration_changed(qint64 milliseconds);
 private:
  void launch(const QStringList& input);
  void read_frames();
  void read_progress();
  void start_local(const QString& path, qint64 milliseconds, bool paused);
  QProcess decoder_, probe_;
  QTimer probe_timeout_;
  QLocalServer server_;
  QPointer<QLocalSocket> socket_;
  QByteArray frames_, diagnostic_, progress_, probe_output_;
  QString local_path_;
  qint64 position_ms_ = 0, duration_ms_ = 0, frame_interval_ms_ = 1000;
  qint64 position_offset_ms_ = 0, decoded_ms_ = 0;
  QSize frame_size_;
  qsizetype payload_size_ = 0;
  bool intentional_ = false, paused_ = false, received_ = false;
  bool stream_started_ = false;
  bool pause_on_frame_ = false;
};
