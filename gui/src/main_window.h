#pragma once
#include "camera_session.h"
#include "settings_store.h"
#include "photo_library.h"
#include <QMainWindow>
#include <QImage>
#include <QPointer>
#include <QTimer>
class ImageViewer;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class QTabWidget;

class MainWindow final : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QString binary, QWidget* parent = nullptr);
 protected:
  void closeEvent(QCloseEvent* event) override;
 private:
  void create_ui();
  void load_settings();
  void save_settings();
  void connect_camera();
  void open_photo();
  void update_frame();
  void send(const QString& command);
  QPushButton* command_button(const QString& label, const QString& command, QWidget* parent);
  QWidget* setting_row(const QString& initial, const QString& command, QWidget* parent);
  AppSettings values() const;
  QString binary_;
  SettingsStore settings_;
  CameraSession session_;
  PhotoLibrary photos_;
  QLineEdit *host_, *model_, *user_, *password_, *folder_;
  QCheckBox *remember_, *auto_sync_, *auto_liveview_, *auto_reconnect_;
  QPushButton *connect_button_;
  QLabel *state_;
  QPlainTextEdit* log_;
  ImageViewer *live_view_, *photo_view_;
  QPointer<ImageViewer> photo_window_;
  QList<QWidget*> camera_actions_;
  QTimer frames_timer_;
  QDateTime last_frame_;
  QImage photo_;
  QString photo_path_;
  bool loading_ = false;
  bool liveview_running_ = false;
};
