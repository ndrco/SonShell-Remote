#pragma once
#include <QImage>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>

// Native-size silent video. The decoder never receives a camera credential or URL.
class VideoPreview final : public QObject {
  Q_OBJECT
 public:
  explicit VideoPreview(QObject* parent = nullptr);
  ~VideoPreview() override;
  void play_local(const QString& path, int seconds = 0);
  bool listen(const QString& socket_path);
  void pause_local(bool paused);
  void stop();
  bool running() const { return decoder_.state() == QProcess::Running; }
 signals:
  void frame_ready(const QImage& image);
  void failed(const QString& message);
  void finished();
 private:
  void launch(const QStringList& input);
  void read_frames();
  QProcess decoder_;
  QLocalServer server_;
  QPointer<QLocalSocket> socket_;
  QByteArray frames_, diagnostic_;
  QSize frame_size_;
  qsizetype payload_size_ = 0;
  bool intentional_ = false, paused_ = false, received_ = false;
  bool stream_started_ = false;
};
