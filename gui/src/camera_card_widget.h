#pragma once
#include "camera_card.h"
#include <QCache>
#include <QHash>
#include <QWidget>
class ImageViewer;
class QTreeWidget;
class QTreeWidgetItem;
class QComboBox;
class QLineEdit;
class QLabel;
class QProgressBar;
class QPushButton;

class CameraCardWidget final : public QWidget {
  Q_OBJECT
 public:
  explicit CameraCardWidget(CameraCard* card, QWidget* parent = nullptr);
  void activated();
  void deactivated();
  void leave_fullscreen();
  QString selected_path() const;
 signals:
  void selected_path_changed();
 private:
  void rebuild();
  void filter();
  QStringList checked(bool rejected = false) const;
  void select(const QString& mode);
  void copy(CameraCard::CopyMode mode);
  void remove_rejected();
  void request_preview();
  void request_original();
  void navigate(int step);
  void toggle_mark(bool rejected);
  void update_marks();
  void play_movie();
  void update_actions();
  QString item_caption(const CardItem& item) const;
  CameraCard* card_;
  QTreeWidget* list_;
  QComboBox* format_;
  QComboBox* quality_;
  QLineEdit* search_;
  QLabel *status_, *count_;
  ImageViewer* viewer_;
  QProgressBar* progress_;
  QList<QWidget*> actions_;
  QPushButton *cancel_, *resume_, *verify_;
  QPushButton *original_, *preview_cancel_;
  QLabel* preview_status_;
  QCache<QString, QImage> previews_{64 * 1024};
  QHash<QString, QTreeWidgetItem*> rows_;
  QString displayed_, pending_preview_, original_displayed_;
  bool pending_original_ = false;
};
