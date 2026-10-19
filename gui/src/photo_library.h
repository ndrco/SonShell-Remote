#pragma once
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QTimer>

class PhotoLibrary final : public QObject {
  Q_OBJECT
 public:
  explicit PhotoLibrary(QObject* parent = nullptr);
  void set_directory(const QString& path);
  void scan();
 signals:
  void image_ready(const QString& path, const QImage& image);
 private:
  struct Entry { qint64 size; QDateTime modified; int stable = 0; };
  QString directory_;
  QString current_signature_;
  QHash<QString, Entry> entries_;
  QTimer timer_;
};
