#pragma once
#include "camera_session.h"
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QVector>

struct CardFile {
  int id = 0, format = 0;
  quint64 size = 0;
  QString path;
};
struct CardItem {
  int slot = 0;
  quint32 id = 0;
  QString stamp, identity, capture_date;
  bool protected_file = false, deletable = false;
  QVector<CardFile> files;
  QString key() const { return QString::number(slot) + ':' + QString::number(id); }
  QString name() const;
  QString format() const;
  bool is_video() const;
  CardFile preview_file() const;
};

// Session-scoped remote catalog and one explicit operation at a time.
class CameraCard final : public QObject {
  Q_OBJECT
 public:
  enum class CopyMode { Selected, All, New };
  explicit CameraCard(CameraSession* session, QObject* parent = nullptr);
  void set_connected(bool connected);
  void set_directory(const QString& directory);
  void refresh();
  void preview(const QString& key, bool thumbnail = false);
  void copy(const QStringList& keys, const QString& format, CopyMode mode);
  void remove(const QStringList& keys);
  void cancel();
  void play_movie(const QString& key, const QString& socket_path);
  void movie_control(const QString& operation, int seconds = 0);
  QString movie_socket_path() const;
  bool playing_video() const { return active_.kind == "movie"; }
  void set_studio(bool enabled) { studio_ = enabled; }
  bool busy() const { return !active_.request.isEmpty() || !queue_.isEmpty(); }
  bool previewing() const { return active_.kind == "preview" || playing_video(); }
  bool dirty() const { return dirty_; }
  const QVector<CardItem>& items() const { return items_; }
  bool imported(const CardItem& item, const CardFile& file) const;
  bool is_new(const CardItem& item, const QString& format) const;
 signals:
  void catalog_changed();
  void busy_changed(bool busy);
  void status_changed(const QString& status);
  void progress_changed(int completed, int total, int percent);
  void preview_ready(const QString& key, const QImage& image, bool thumbnail);
  void studio_ready(const QImage& image, const QString& name);
  void copied(const QString& path);
  void movie_viewing(bool active);
  void movie_state(const QString& state);
  void movie_failed(const QString& message);
 private:
  struct Task { QString kind, request, key; CardFile file; bool thumbnail = false, studio = false; QString socket; };
  void receive(const QJsonObject& event);
  void dispatch();
  void finish(const QString& message = {});
  QString request_id();
  QString ledger_path() const;
  QString ledger_key(const CardItem& item, const CardFile& file) const;
  const CardItem* find(const QString& key) const;
  QString record(const CardItem& item, const CardFile& file, const QString& path);
  static bool matches(const CardItem& item, const CardFile& file, const QString& format);
  CameraSession* session_;
  QVector<CardItem> items_;
  QString directory_, epoch_, prefix_, list_request_;
  quint64 sequence_ = 0;
  Task active_;
  QQueue<Task> queue_;
  bool connected_ = false, dirty_ = true, canceled_ = false, studio_ = false, studio_list_ = false;
  int total_ = 0, completed_ = 0, failed_ = 0;
  QJsonObject ledger_;
  QStringList list_warnings_;
};
