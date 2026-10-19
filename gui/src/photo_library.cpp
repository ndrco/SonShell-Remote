#include "photo_library.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QImageReader>
#include <QSet>

PhotoLibrary::PhotoLibrary(QObject* parent) : QObject(parent) {
  timer_.setInterval(1000);
  connect(&timer_, &QTimer::timeout, this, &PhotoLibrary::scan);
  timer_.start();
}
void PhotoLibrary::set_directory(const QString& path) {
  directory_ = path;
  entries_.clear();
  current_signature_.clear();
  scan();
}
void PhotoLibrary::scan() {
  if (directory_.isEmpty() || !QDir(directory_).exists()) return;
  QDirIterator files(directory_, QDir::Files, QDirIterator::Subdirectories);
  QFileInfo latest;
  QSet<QString> present;
  while (files.hasNext()) {
    files.next();
    const auto info = files.fileInfo();
    const auto suffix = info.suffix().toLower();
    if (suffix != "jpg" && suffix != "jpeg" && suffix != "png") continue;
    if (info.size() == 0) continue;
    const QString path = info.absoluteFilePath();
    present.insert(path);
    auto it = entries_.find(path);
    if (it == entries_.end() || it->size != info.size() || it->modified != info.lastModified()) {
      entries_.insert(path, {info.size(), info.lastModified(), 0});
      continue;
    }
    ++it->stable;
    if (!latest.exists() || info.lastModified() > latest.lastModified() ||
        (info.lastModified() == latest.lastModified() && path > latest.absoluteFilePath())) latest = info;
  }
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (!present.contains(it.key())) it = entries_.erase(it); else ++it;
  }
  if (!latest.exists()) return;
  const QString signature = latest.absoluteFilePath() + ':' + QString::number(latest.size())
      + ':' + QString::number(latest.lastModified().toMSecsSinceEpoch());
  if (signature == current_signature_) return;
  QImageReader reader(latest.absoluteFilePath());
  reader.setAutoTransform(true);
  // Reject incomplete JPEGs; image decoders can otherwise accept truncated files.
  if (latest.suffix().compare("png", Qt::CaseInsensitive) != 0) {
    QFile file(latest.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly) || !file.seek(file.size() - 2) || file.read(2) != QByteArray::fromHex("ffd9")) return;
  }
  const QImage image = reader.read();
  if (image.isNull()) return;
  current_signature_ = signature;
  emit image_ready(latest.absoluteFilePath(), image);
}
