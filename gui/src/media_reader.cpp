#include <QCoreApplication>
#include "media_reader.h"
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTransform>

namespace {
bool canceled(const MediaCancel& token) { return token && token->load(); }
QByteArray exif_data(const QString& path, bool raw, bool thumbnail, const MediaCancel& token, QString& error) {
  QStringList args{"-config", "", "-json", "-n", "-charset", "filename=UTF8",
      "-Model", "-LensModel", "-LensID", "-ExposureTime", "-FNumber", "-ISO",
      "-DateTimeOriginal", "-FocalLength", "-Orientation", "-ImageWidth", "-ImageHeight",
      "-ColorSpace", "-ProfileDescription", "-FileType"};
  if (raw) {
    args << "-b" << "-PreviewImage" << "-JpgFromRaw";
    if (thumbnail) args << "-ThumbnailImage";
  }
  args << "--" << QFileInfo(path).absoluteFilePath();
  QProcess process;
  process.start("exiftool", args, QIODevice::ReadOnly);
  if (!process.waitForStarted(2000)) { error = QCoreApplication::translate("App", "ExifTool not found. Install libimage-exiftool-perl."); return {}; }
  QByteArray output;
  QElapsedTimer elapsed; elapsed.start();
  do {
    process.waitForFinished(50);
    output += process.readAllStandardOutput();
    if (canceled(token) || elapsed.elapsed() > 10000 || output.size() > 96 * 1024 * 1024) {
      process.kill(); process.waitForFinished(1000);
      error = canceled(token) ? QCoreApplication::translate("App", "Reading canceled") : QCoreApplication::translate("App", "ExifTool: read limit exceeded");
      return {};
    }
  } while (process.state() != QProcess::NotRunning);
  output += process.readAllStandardOutput();
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    error = QCoreApplication::translate("App", "Could not read EXIF or RAW preview");
    return {};
  }
  return output;
}
QImage orient(QImage image, int orientation) {
  switch (orientation) {
    case 2: return image.mirrored(true, false);
    case 3: return image.transformed(QTransform().rotate(180));
    case 4: return image.mirrored(false, true);
    case 5: return image.mirrored(true, false).transformed(QTransform().rotate(270));
    case 6: return image.transformed(QTransform().rotate(90));
    case 7: return image.mirrored(true, false).transformed(QTransform().rotate(90));
    case 8: return image.transformed(QTransform().rotate(270));
    default: return image;
  }
}
}
bool MediaReader::is_video(const QString& path) {
  const auto suffix = QFileInfo(path).suffix().toLower();
  return suffix == "mp4" || suffix == "mov" || suffix == "m4v";
}
QString MediaReader::group_key(const QString& path) {
  const QFileInfo file(path);
  return file.absolutePath() + '/' + file.completeBaseName().toCaseFolded() + (is_video(path) ? ":video" : "");
}
bool MediaReader::is_raw(const QString& path) {
  const auto suffix = QFileInfo(path).suffix().toLower();
  return suffix == "arw" || suffix == "sr2" || suffix == "srf" || suffix == "dng";
}
bool MediaReader::is_supported(const QString& path) {
  const auto suffix = QFileInfo(path).suffix().toLower();
  return is_raw(path) || is_video(path) || suffix == "jpg" || suffix == "jpeg" || suffix == "png";
}
bool MediaReader::is_complete_jpeg(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) && file.size() >= 4 && file.seek(file.size() - 2)
      && file.read(2) == QByteArray::fromHex("ffd9");
}
MediaResult MediaReader::read(const QString& path, bool thumbnail, const MediaCancel& cancel) {
  MediaResult result; result.path = path;
  const QFileInfo before(path);
  if (canceled(cancel) || !before.isFile() || before.size() == 0) { result.error = QCoreApplication::translate("App", "File unavailable"); return result; }
  const auto suffix = before.suffix().toLower();
  const bool raw = is_raw(path);
  result.video = is_video(path);
  result.raw_preview = raw;
  if ((suffix == "jpg" || suffix == "jpeg") && !is_complete_jpeg(path)) {
    result.error = QCoreApplication::translate("App", "JPEG is incomplete or damaged"); return result;
  }
  QJsonObject metadata;
  QString metadata_error;
  if (raw || !thumbnail) {
    const auto json = QJsonDocument::fromJson(exif_data(path, raw, thumbnail, cancel, metadata_error));
    if (json.isArray() && !json.array().isEmpty()) metadata = json.array().first().toObject();
  }
  if (canceled(cancel)) { result.error = QCoreApplication::translate("App", "Reading canceled"); return result; }
  if (result.video) {
    QProcess decoder;
    QStringList args{"-hide_banner", "-loglevel", "error", "-threads", "1", "-filter_threads", "1", "-protocol_whitelist", "file,pipe", "-i", before.absoluteFilePath(), "-frames:v", "1"};
    if (thumbnail) args << "-vf" << "scale=w='min(192,iw)':h='min(128,ih)':force_original_aspect_ratio=decrease";
    args << "-f" << "image2pipe" << "-c:v" << "png" << "-threads" << "1" << "pipe:1";
    decoder.start("ffmpeg", args);
    QByteArray bytes; QElapsedTimer elapsed; elapsed.start();
    while (decoder.state() != QProcess::NotRunning) {
      decoder.waitForFinished(40); bytes += decoder.readAllStandardOutput();
      if (canceled(cancel) || elapsed.elapsed() > 12000 || bytes.size() > 128 * 1024 * 1024) { decoder.kill(); decoder.waitForFinished(500); break; }
    }
    bytes += decoder.readAllStandardOutput();
    result.image = QImage::fromData(bytes, "PNG");
    result.source = QCoreApplication::translate("App", "Video · first frame");
  } else if (raw) {
    QByteArray best;
    const auto tags = thumbnail ? QStringList{"ThumbnailImage", "PreviewImage", "JpgFromRaw"}
                                : QStringList{"JpgFromRaw", "PreviewImage"};
    qint64 best_area = 0;
    for (const auto& tag : tags) {
      const auto text = metadata.value(tag).toString();
      if (!text.startsWith("base64:")) continue;
      const auto bytes = QByteArray::fromBase64(text.mid(7).toLatin1());
      QBuffer buffer; buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
      QImageReader reader(&buffer); const auto size = reader.size();
      const qint64 area = qint64(size.width()) * size.height();
      if (size.isValid() && area > best_area) { best = bytes; best_area = area; }
      if (thumbnail && !best.isEmpty()) break;
    }
    if (best.isEmpty()) {
      result.error = metadata_error.isEmpty() ? QCoreApplication::translate("App", "No accessible embedded JPEG preview in this RAW file") : metadata_error;
      return result;
    }
    QBuffer buffer(&best); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    if (thumbnail) reader.setScaledSize(reader.size().scaled(192, 128, Qt::KeepAspectRatio));
    result.image = orient(reader.read(), metadata.value("Orientation").toInt(1));
    result.source = QString(QCoreApplication::translate("App", "RAW · embedded JPEG preview %1 × %2")).arg(result.image.width()).arg(result.image.height());
  } else {
    QImageReader reader(path); reader.setAutoTransform(true);
    if (thumbnail && reader.size().isValid()) reader.setScaledSize(reader.size().scaled(192, 128, Qt::KeepAspectRatio));
    result.image = reader.read();
    result.source = QString(QCoreApplication::translate("App", "%1 · original %2 × %3")).arg(suffix.toUpper()).arg(result.image.width()).arg(result.image.height());
  }
  const QFileInfo after(path);
  if (before.size() != after.size() || before.lastModified() != after.lastModified()) {
    result.image = {}; result.error = QCoreApplication::translate("App", "File is still changing"); return result;
  }
  if (result.image.isNull()) { result.error = QCoreApplication::translate("App", "Could not decode image"); return result; }
  if (thumbnail) return result;
  auto value = [&metadata](const QString& tag) {
    const auto v = metadata.value(tag);
    return v.isString() ? v.toString() : (v.isDouble() ? QString::number(v.toDouble(), 'g', 8) : QString());
  };
  auto add = [&result](const QString& name, const QString& text) {
    if (!text.isEmpty()) result.metadata << name + ": " + text.left(500);
  };
  add(QCoreApplication::translate("App", "File"), before.fileName()); add(QCoreApplication::translate("App", "Source"), result.source);
  add(QCoreApplication::translate("App", "Camera"), value("Model"));
  add(QCoreApplication::translate("App", "Lens"), value("LensModel").isEmpty() ? value("LensID") : value("LensModel"));
  const double exposure = metadata.value("ExposureTime").toDouble();
  if (exposure > 0) add(QCoreApplication::translate("App", "Shutter speed"), exposure < 1 ? QString(QCoreApplication::translate("App", "1/%1 s")).arg(qRound(1 / exposure)) : QString::number(exposure) + QCoreApplication::translate("App", " s"));
  if (!value("FNumber").isEmpty()) add(QCoreApplication::translate("App", "Aperture"), "f/" + value("FNumber"));
  add("ISO", value("ISO")); add(QCoreApplication::translate("App", "Focal length, mm"), value("FocalLength"));
  add(QCoreApplication::translate("App", "Capture date"), value("DateTimeOriginal"));
  add(QCoreApplication::translate("App", "EXIF orientation"), value("Orientation"));
  const auto color = metadata.value("ColorSpace").toInt(-1);
  add(QCoreApplication::translate("App", "Color profile"), value("ProfileDescription").isEmpty() ? (color == 1 ? "sRGB" : (color == 2 ? "Adobe RGB" : "")) : value("ProfileDescription"));
  if (raw && metadata.contains("ImageWidth"))
    add(QCoreApplication::translate("App", "RAW dimensions from EXIF"), value("ImageWidth") + " × " + value("ImageHeight"));
  add(QCoreApplication::translate("App", "File size"), QString::number(before.size() / 1024.0 / 1024, 'f', 1) + " MiB");
  if (!metadata_error.isEmpty()) add("EXIF", metadata_error);
  return result;
}
