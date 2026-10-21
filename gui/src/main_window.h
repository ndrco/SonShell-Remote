#pragma once
#include "camera_session.h"
#include "camera_card.h"
#include "settings_store.h"
#include "photo_library.h"
#include "studio_review.h"
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
class QSpinBox;
class GalleryWidget;
class CameraCardWidget;
class CameraControlsWidget;
class HelpDialog;

class MainWindow final : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QString binary, QWidget* parent = nullptr);
  ~MainWindow() override;
 protected:
  void closeEvent(QCloseEvent* event) override;
 private:
  void create_ui();
  void create_menus();
  void show_help();
  void load_settings();
  void save_settings();
  void connect_camera();
  void open_photo();
  void update_frame();
  void set_photo_directory();
  void load_photo(const QString& path, bool open = false);
  void send(const QString& command);
  QPushButton* command_button(const QString& label, const QString& command, QWidget* parent);
  QWidget* setting_row(const QString& initial, const QString& command, QWidget* parent);
  AppSettings values() const;
  QString binary_, language_;
  QPointer<HelpDialog> help_;
  SettingsStore settings_;
  CameraSession session_;
  CameraCard card_;
  PhotoLibrary photos_;
  StudioReview review_;
  QLineEdit *host_, *model_, *user_, *password_, *folder_;
  QCheckBox *remember_, *auto_liveview_, *auto_reconnect_;
  QPushButton *connect_button_, *browse_;
  QCheckBox *studio_, *hold_;
  QSpinBox* review_seconds_;
  QTabWidget* tabs_;
  GalleryWidget* gallery_;
  CameraCardWidget* card_widget_;
  CameraControlsWidget* sdk_controls_;
  QLabel *state_;
  QPlainTextEdit* log_;
  ImageViewer *live_view_, *photo_view_;
  QPointer<ImageViewer> photo_window_;
  QList<QWidget*> camera_actions_;
  QTimer frames_timer_;
  QDateTime last_frame_;
  QImage photo_, last_live_image_;
  QString photo_path_, photo_caption_;
  bool decode_pending_ = false, open_after_load_ = false;
  bool loading_ = false;
  bool liveview_running_ = false;
};
