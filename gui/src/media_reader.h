#pragma once
#include <QDateTime>
#include <QImage>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <memory>

using MediaCancel = std::shared_ptr<std::atomic<bool>>;
struct MediaItem {
  QString key, name, jpeg_path, raw_path, signature;
  QDateTime modified;
  QString video_path;
  QString path() const { return !video_path.isEmpty() ? video_path : jpeg_path.isEmpty() ? raw_path : jpeg_path; }
  QString format() const {
    if (!video_path.isEmpty()) return video_path.section('.', -1).toUpper();
    if (jpeg_path.isEmpty()) return "RAW";
    const QString rendered = jpeg_path.endsWith(".png", Qt::CaseInsensitive) ? "PNG" : "JPEG";
    return raw_path.isEmpty() ? rendered : "RAW+" + rendered;
  }
};
Q_DECLARE_METATYPE(MediaItem)
using MediaItems = QVector<MediaItem>;
Q_DECLARE_METATYPE(MediaItems)
struct MediaResult {
  QString path, error, source;
  QImage image;
  QStringList metadata;
  bool raw_preview = false, video = false;
};
Q_DECLARE_METATYPE(MediaResult)

class MediaReader final {
 public:
  static bool is_video(const QString& path);
  static QString group_key(const QString& path);
  static bool is_raw(const QString& path);
  static bool is_supported(const QString& path);
  static bool is_complete_jpeg(const QString& path);
  static MediaResult read(const QString& path, bool thumbnail = false, const MediaCancel& cancel = {});
};
