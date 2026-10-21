#pragma once
#include "media_reader.h"
#include <QAbstractListModel>
#include <QCache>
#include <QSet>
#include <QThreadPool>
#include <QWidget>
class ImageViewer;
class QListView;
class QLineEdit;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QSortFilterProxyModel;

class GalleryModel final : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Role { KeyRole = Qt::UserRole + 1, PathRole, SearchRole, FormatRole, DateRole };
  explicit GalleryModel(QObject* parent = nullptr);
  ~GalleryModel() override;
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  bool set_items(const MediaItems& items);
 signals:
  void thumbnail_ready();
 private:
  void request_thumbnail(int row);
  MediaItems items_;
  QCache<QString, QImage> cache_{128};
  QSet<QString> pending_;
  QThreadPool pool_;
  MediaCancel cancel_ = std::make_shared<std::atomic<bool>>(false);
  quint64 generation_ = 0;
};

class GalleryWidget final : public QWidget {
  Q_OBJECT
 public:
  explicit GalleryWidget(QWidget* parent = nullptr);
  void set_items(const MediaItems& items);
  void show_photo(const MediaResult& result);
  void select_path(const QString& path);
  ImageViewer* viewer() const { return viewer_; }
 signals:
  void photo_selected(const QString& path);
  void open_requested(const QString& path);
 private:
  GalleryModel model_;
  QSortFilterProxyModel* proxy_;
  QListView* list_;
  QLineEdit* search_;
  QComboBox *format_, *sort_;
  QLabel* count_;
  QPlainTextEdit* metadata_;
  ImageViewer* viewer_;
};
