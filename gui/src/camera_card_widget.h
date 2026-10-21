#pragma once
#include "camera_card.h"
#include <QCache>
#include <QWidget>
class ImageViewer;
class QTreeWidget;
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
  void leave_fullscreen();
 private:
  void rebuild();
  void filter();
  QStringList checked(bool rejected = false) const;
  void select(const QString& mode);
  void copy(CameraCard::CopyMode mode);
  void remove_rejected();
  void request_preview();
  void navigate(int step);
  void toggle_mark(bool rejected);
  CameraCard* card_;
  QTreeWidget* list_;
  QComboBox* format_;
  QLineEdit* search_;
  QLabel *status_, *count_;
  ImageViewer* viewer_;
  QProgressBar* progress_;
  QList<QWidget*> actions_;
  QPushButton* cancel_;
  QCache<QString, QImage> previews_{64 * 1024};
  QString displayed_, pending_preview_;
};
