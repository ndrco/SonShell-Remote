#pragma once
#include "media_reader.h"
#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QThreadPool>
#include <QTimer>

class PhotoLibrary final : public QObject {
  Q_OBJECT
 public:
  explicit PhotoLibrary(QObject* parent = nullptr);
  ~PhotoLibrary() override;
  void set_directory(const QString& path);
  void scan();
  QString directory() const { return directory_; }
  void load(const QString& path, bool new_capture = false);
  bool completed_download(const QString& path, bool new_capture);
  void set_auto_preview(bool enabled) { auto_preview_ = enabled; }
 signals:
  void image_ready(const QString& path, const QImage& image);
  void photo_loaded(const MediaResult& result, bool new_capture);
  void items_changed(const MediaItems& items);
 private:
  struct Snapshot { QHash<QString, QString> stamps; MediaItems items; };
  static Snapshot read_directory(const QString& directory, const QHash<QString, QString>& previous, const MediaCancel& cancel);
  QString directory_, current_signature_;
  QHash<QString, QString> stamps_;
  QFutureWatcher<Snapshot> scanner_;
  QThreadPool pool_;
  QTimer timer_;
  MediaCancel cancel_ = std::make_shared<std::atomic<bool>>(false);
  MediaCancel decode_cancel_;
  quint64 generation_ = 0, scan_generation_ = 0, request_ = 0;
  bool auto_preview_ = true, scan_active_ = false;
};
