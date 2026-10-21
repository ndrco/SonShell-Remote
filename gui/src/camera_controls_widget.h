#pragma once
#include <QGroupBox>
#include <QTimer>
class CameraSession;
class CameraCard;
class QComboBox;
class QCheckBox;
class QLabel;

class CameraControlsWidget final : public QGroupBox {
  Q_OBJECT
 public:
  CameraControlsWidget(CameraSession* session, CameraCard* card, QWidget* parent = nullptr);
  void set_live(bool enabled);
  void select_point(const QPointF& normalized);
 signals:
  void point_selection_changed(bool enabled);
 private:
  void refresh();
  void request(const QString& operation, const QString& arguments = {});
  void update_enabled();
  CameraSession* session_;
  CameraCard* card_;
  QList<QComboBox*> editors_;
  QCheckBox* mouse_af_;
  QLabel* message_;
  QTimer refresh_timer_, deadline_;
  QString pending_, operation_, last_error_;
  bool available_af_ = false, live_ = false, connected_ = false, dirty_ = false;
};
