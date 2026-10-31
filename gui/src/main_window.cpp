#include "main_window.h"
#include "image_viewer.h"
#include "gallery_widget.h"
#include "camera_card_widget.h"
#include "camera_controls_widget.h"
#include "help_dialog.h"
#include "about_dialog.h"
#include "language_support.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QMenuBar>
#include <QActionGroup>
#include <QSpinBox>
#include <QComboBox>
#include <QSignalBlocker>
#include <functional>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QCoreApplication>
#include <QAbstractButton>
#include <QHostAddress>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QPushButton* button(const QString& text, QWidget* parent, const std::function<void()>& action) {
  auto* b = new QPushButton(text, parent);
  QObject::connect(b, &QPushButton::clicked, parent, action);
  return b;
}
}
MainWindow::MainWindow(QString binary, QWidget* parent)
    : QMainWindow(parent), binary_(std::move(binary)), session_(this), card_(&session_, this), photos_(this), review_(this) {
  setWindowTitle("SonShell Remote");
  resize(1280, 870);
  create_ui();
  file_path_ = new QLineEdit(this); file_path_->setObjectName("file_status_path");
  file_path_->setReadOnly(true); file_path_->setFrame(false); file_path_->setMinimumWidth(0);
  statusBar()->addWidget(file_path_, 1);
  connect(card_widget_, &CameraCardWidget::selected_path_changed, this, &MainWindow::update_file_path);
  connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::update_file_path);
  load_settings();
  create_menus();
  photos_.set_auto_preview(false);
  review_.setObjectName("studio_review");
  review_.configure(studio_->isChecked(), review_seconds_->value());
  card_.set_studio(studio_->isChecked(), review_quality_->currentIndex() == 1);
  connect(&card_, &CameraCard::movie_viewing, this, [this](bool active) {
    for (auto* action : camera_actions_) action->setEnabled(!active && session_.state() == CameraSession::State::Connected);
    if (active) { resume_liveview_ = liveview_running_; review_.cancel(); }
    else if (resume_liveview_ && session_.state() == CameraSession::State::Connected) { resume_liveview_ = false; send("monitor start"); }
  });
  connect(&card_, &CameraCard::copied, this, [this](const QString& path) { photos_.completed_download(path, false); });
  connect(&card_, &CameraCard::busy_changed, this, [this](bool busy) { browse_->setEnabled(!busy); folder_->setEnabled(!busy); });
  connect(&card_, &CameraCard::studio_ready, this, [this](const QImage& image, const QString& name, const QString& source) {
    if (!liveview_running_) return;
    MediaResult result; result.image = image; result.path = name; result.source = source; review_.present(result);
  });
  connect(&session_, &CameraSession::state_changed, this, [this](auto state, const QString& message) {
    state_->setText(message);
    const bool connected = state == CameraSession::State::Connected;
    for (auto* action : camera_actions_) action->setEnabled(connected);
    connect_button_->setText(session_.active() ? QCoreApplication::translate("App", "Disconnect") : QCoreApplication::translate("App", "Connect"));
    connect_button_->setEnabled(state != CameraSession::State::Stopping);
    card_.set_connected(connected);
    browse_->setEnabled(!card_.busy()); folder_->setEnabled(!card_.busy());
    for (auto* input : {host_, model_, user_, password_}) input->setEnabled(!session_.active());
    statusBar()->showMessage(message);
    if (!connected) {
      review_.cancel();
      liveview_running_ = false; last_live_image_ = {};
      live_view_->set_caption("Live View · " + message);
    }
  });
  connect(&session_, &CameraSession::log_received, log_, &QPlainTextEdit::appendPlainText);
  connect(&session_, &CameraSession::connected, this, [this] {
    send("sync off");
    if (auto_liveview_->isChecked()) send("monitor start");
    send("status");
  });
  connect(&session_, &CameraSession::liveview_changed, this, [this](bool running) {
    liveview_running_ = running;
    sdk_controls_->set_live(running && !review_.active());
    if (!running) review_.cancel();
    last_frame_ = {};
    live_view_->set_caption(running ? QCoreApplication::translate("App", "Live View · waiting for a frame…") : QCoreApplication::translate("App", "Live View · stopped"));
  });
  connect(&photos_, &PhotoLibrary::items_changed, this, [this](const MediaItems& items) {
    gallery_->set_items(items);
    if (photo_path_.isEmpty() && !decode_pending_ && !items.isEmpty()) load_photo(items.first().path());
  });
  connect(gallery_, &GalleryWidget::photo_selected, this, [this](const QString& path) { load_photo(path); });
  connect(gallery_, &GalleryWidget::open_requested, this, [this](const QString& path) { load_photo(path, true); });
  connect(&photos_, &PhotoLibrary::photo_loaded, this, [this](const MediaResult& result, bool new_capture) {
    decode_pending_ = false;
    photo_ = result.image; photo_path_ = result.path;
    gallery_->select_path(result.path);
    photo_caption_ = QFileInfo(result.path).fileName() + " · " + result.source;
    gallery_->show_photo(result);
    if (photo_window_ && !result.image.isNull()) {
      photo_window_->set_local_video(result.video ? result.path : QString());
      photo_window_->set_image(photo_, photo_caption_);
    }
    if (open_after_load_ && !photo_.isNull()) open_photo();
    open_after_load_ = false;
    Q_UNUSED(new_capture);
    update_file_path();
    if (!result.error.isEmpty()) statusBar()->showMessage(result.error, 5000);
  });
  connect(&review_, &StudioReview::photo_presented, this, [this](const MediaResult& result) {
    studio_path_ = result.path;
    live_view_->set_image(result.image, QCoreApplication::translate("App", "Photo · ") + QFileInfo(result.path).fileName() + " · " + result.source);
    tabs_->setCurrentIndex(0);
    sdk_controls_->set_live(false);
    hold_->setEnabled(true); hold_->show();
    update_file_path();
  });
  connect(&review_, &StudioReview::returned_to_liveview, this, [this] {
    studio_path_.clear(); update_file_path();
    { QSignalBlocker guard(hold_); hold_->setChecked(false); }
    hold_->setEnabled(false); hold_->hide();
    sdk_controls_->set_live(liveview_running_);
    live_view_->set_image(last_live_image_, last_live_image_.isNull() ? QCoreApplication::translate("App", "Live View · waiting for a frame…") : "Live View");
  });
  set_photo_directory();
  frames_timer_.setInterval(70);
  connect(&frames_timer_, &QTimer::timeout, this, &MainWindow::update_frame);
  frames_timer_.start();
  startup_connect_.setSingleShot(true); startup_connect_.setInterval(250);
  connect(&startup_connect_, &QTimer::timeout, this, &MainWindow::connect_on_startup);
  if (auto_connect_->isChecked()) startup_connect_.start();
}

MainWindow::~MainWindow() {
  startup_connect_.stop();
  // QObject children outlive C++ value members. No child may call back into
  // PhotoLibrary/StudioReview after those members begin destruction.
  for (auto* child : findChildren<QObject*>()) QObject::disconnect(child, nullptr, this, nullptr);
  live_view_->leave_fullscreen(); photo_view_->leave_fullscreen(); card_widget_->leave_fullscreen();
  frames_timer_.stop(); review_.cancel(); session_.stop();
}

void MainWindow::create_ui() {
  auto* root = new QWidget(this);
  auto* layout = new QVBoxLayout(root);
  create_settings();
  auto* connection_box = new QWidget(root);
  auto* connection = new QHBoxLayout(connection_box); connection->setContentsMargins(0, 0, 0, 0);
  connect_button_ = button(QCoreApplication::translate("App", "Connect"), connection_box, [this] {
    startup_connect_.stop();
    if (session_.active()) session_.stop(); else connect_camera();
  });
  connect_button_->setObjectName("connect");
  connect_button_->setMinimumSize(160, 38);
  auto connection_font = connect_button_->font(); connection_font.setBold(true); connect_button_->setFont(connection_font);
  connect_button_->setStyleSheet(
      "QPushButton#connect { background: #0969da; color: white; border: 2px solid #0969da; border-radius: 6px; padding: 6px 18px; }"
      "QPushButton#connect:hover { background: #0759ba; border-color: #0759ba; }"
      "QPushButton#connect:pressed { background: #064c9e; }"
      "QPushButton#connect:focus { border-color: #fbbf24; }"
      "QPushButton#connect:disabled { background: #576579; border-color: #576579; color: #e5e7eb; }");
  state_ = new QLabel(QCoreApplication::translate("App", "Disconnected"), connection_box); state_->setObjectName("connection_state");
  state_->setWordWrap(true);
  connection->addWidget(state_, 1);
  connection->addWidget(button(QCoreApplication::translate("App", "Settings…"), connection_box, [this] { show_settings(0); }));
  connection->addWidget(connect_button_);
  layout->addWidget(connection_box);
  auto* split = new QSplitter(Qt::Horizontal, root);
  auto* scroll = new QScrollArea(split); scroll->setWidgetResizable(true); scroll->setMinimumWidth(360);
  auto* controls = new QWidget(scroll); auto* controls_layout = new QVBoxLayout(controls);
  auto* capture = new QGroupBox(QCoreApplication::translate("App", "Capture"), controls); auto* capture_layout = new QGridLayout(capture);
  capture_layout->addWidget(command_button(QCoreApplication::translate("App", "TAKE PHOTO"), "shoot", capture), 0, 0, 1, 2);
  capture_layout->addWidget(command_button(QCoreApplication::translate("App", "Autofocus"), "focus", capture), 1, 0);
  capture_layout->addWidget(command_button(QCoreApplication::translate("App", "Camera status"), "status", capture), 1, 1);
  capture_layout->addWidget(command_button(QCoreApplication::translate("App", "Start video"), "record start", capture), 2, 0);
  capture_layout->addWidget(command_button(QCoreApplication::translate("App", "Stop video"), "record stop", capture), 2, 1);
  controls_layout->addWidget(capture);
  sdk_controls_ = new CameraControlsWidget(&session_, &card_, controls);
  controls_layout->addWidget(sdk_controls_);
  auto* transfer = new QGroupBox("Live View", controls); auto* transfer_layout = new QVBoxLayout(transfer);
  transfer_layout->addWidget(command_button(QCoreApplication::translate("App", "Live View · start"), "monitor start", transfer));
  transfer_layout->addWidget(command_button(QCoreApplication::translate("App", "Live View · stop"), "monitor stop", transfer));
  controls_layout->addWidget(transfer);
  controls_layout->addStretch(); scroll->setWidget(controls);
  auto* right = new QWidget(split); auto* right_layout = new QVBoxLayout(right);
  right_layout->setContentsMargins(0, 0, 0, 0);
  tabs_ = new QTabWidget(right); tabs_->setObjectName("main_tabs");
  auto* tabs = tabs_;
  auto* live_host = new QWidget(tabs); auto* live_layout = new QVBoxLayout(live_host);
  live_view_ = new ImageViewer(live_host); live_view_->setObjectName("live_view");
  live_view_->set_caption(QCoreApplication::translate("App", "Live View · connect a camera")); live_layout->addWidget(hold_); live_layout->addWidget(live_view_, 1);
  connect(sdk_controls_, &CameraControlsWidget::point_selection_changed, live_view_->view(), &ImageView::set_point_selection);
  connect(live_view_->view(), &ImageView::point_selected, sdk_controls_, &CameraControlsWidget::select_point);
  gallery_ = new GalleryWidget(tabs); photo_view_ = gallery_->viewer();
  card_widget_ = new CameraCardWidget(&card_, tabs);
  auto* preview_form = settings_dialog_->findChild<QFormLayout*>("preview_settings_form");
  connect(card_widget_->preview_quality_selector(), &QComboBox::currentIndexChanged, this, [this] { if (!loading_) save_settings(); });
  preview_form->addRow(QCoreApplication::translate("App", "Photos on camera"), card_widget_->preview_quality_selector());
  preview_form->addRow(QCoreApplication::translate("App", "Video on camera"), card_widget_->video_quality_selector());
  connect(card_widget_->video_quality_selector(), &QComboBox::currentIndexChanged, this, [this] { if (!loading_) save_settings(); });
  tabs->addTab(live_host, "Live View"); tabs->addTab(card_widget_, QCoreApplication::translate("App", "On camera")); tabs->addTab(gallery_, QCoreApplication::translate("App", "On computer · EXIF"));
  connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) { if (index == 1) card_widget_->activated(); else card_widget_->deactivated(); });
  right_layout->addWidget(tabs, 4);
  log_ = new QPlainTextEdit(right); log_->setReadOnly(true); log_->setMaximumBlockCount(1500);
  log_->setPlaceholderText(QCoreApplication::translate("App", "Connection and file transfer log")); log_->setMaximumHeight(180);
  right_layout->addWidget(log_, 1); log_->hide();
  split->addWidget(scroll); split->addWidget(right); split->setStretchFactor(1, 1); split->setSizes({360, 900});
  layout->addWidget(split, 1); setCentralWidget(root);
  for (auto* action : camera_actions_) action->setEnabled(false);
  for (auto* editor : {host_, model_, user_, password_, folder_}) connect(editor, &QLineEdit::editingFinished, this, [this, editor] {
    save_settings(); if (editor == folder_) set_photo_directory();
  });
  connect(remember_, &QCheckBox::toggled, this, [this](bool remember) {
    if (loading_) return;
    if (!remember && !settings_.forget_credentials()) statusBar()->showMessage(QCoreApplication::translate("App", "Could not remove saved credentials"));
    save_settings();
  });
  connect(auto_liveview_, &QCheckBox::toggled, this, [this] { if (!loading_) save_settings(); });
  connect(auto_connect_, &QCheckBox::toggled, this, [this](bool enabled) {
    if (loading_) return;
    if (!enabled) startup_connect_.stop();
    save_settings();
  });
  auto configure_review = [this] {
    if (loading_) return;
    review_.configure(studio_->isChecked(), review_seconds_->value()); card_.set_studio(studio_->isChecked(), review_quality_->currentIndex() == 1); save_settings();
  };
  connect(studio_, &QCheckBox::toggled, this, configure_review);
  connect(review_seconds_, qOverload<int>(&QSpinBox::valueChanged), this, configure_review);
  connect(review_quality_, qOverload<int>(&QComboBox::currentIndexChanged), this, configure_review);
  connect(hold_, &QCheckBox::toggled, &review_, &StudioReview::hold);
  connect(auto_reconnect_, &QCheckBox::toggled, this, [this](bool on) {
    if (!loading_) { session_.set_auto_reconnect(on); save_settings(); }
  });
}
void MainWindow::create_settings() {
  settings_dialog_ = new QDialog(this); settings_dialog_->setObjectName("settings_dialog");
  settings_dialog_->setWindowTitle(QCoreApplication::translate("App", "Settings")); settings_dialog_->resize(700, 430);
  auto* layout = new QVBoxLayout(settings_dialog_);
  auto* pages = new QTabWidget(settings_dialog_); pages->setObjectName("settings_tabs"); layout->addWidget(pages);
  auto* connection = new QWidget(pages); auto* form = new QFormLayout(connection);
  host_ = new QLineEdit(connection); host_->setObjectName("host");
  model_ = new QLineEdit(connection); model_->setObjectName("model");
  model_->setToolTip(QCoreApplication::translate("App", "SonShell model name, e.g. a6700. Features depend on the SDK."));
  user_ = new QLineEdit(connection); user_->setObjectName("username");
  password_ = new QLineEdit(connection); password_->setObjectName("password"); password_->setEchoMode(QLineEdit::Password);
  form->addRow(QCoreApplication::translate("App", "Camera IP"), host_);
  form->addRow(QCoreApplication::translate("App", "Model"), model_);
  form->addRow(QCoreApplication::translate("App", "Username"), user_);
  form->addRow(QCoreApplication::translate("App", "Password"), password_);
  remember_ = new QCheckBox(QCoreApplication::translate("App", "Remember username and password"), connection); remember_->setObjectName("remember");
  remember_->setToolTip(QCoreApplication::translate("App", "Store in an unencrypted file on this computer with 0600 permissions. Clearing the checkbox removes the saved fields."));
  auto_liveview_ = new QCheckBox(QCoreApplication::translate("App", "Live View on connection"), connection);
  auto_connect_ = new QCheckBox(QCoreApplication::translate("App", "Connect on startup"), connection); auto_connect_->setObjectName("auto_connect");
  auto_connect_->setToolTip(QCoreApplication::translate("App", "Connect on startup using the saved username and password. First connect manually and accept the SDK terms. Disconnect cancels retries until the next launch."));
  auto_reconnect_ = new QCheckBox(QCoreApplication::translate("App", "Reconnect automatically"), connection);
  form->addRow(remember_); form->addRow(auto_connect_); form->addRow(auto_liveview_); form->addRow(auto_reconnect_);
  pages->addTab(connection, QCoreApplication::translate("App", "Connection"));
  auto* files = new QWidget(pages); auto* files_layout = new QFormLayout(files);
  folder_ = new QLineEdit(files); folder_->setObjectName("photo_directory");
  auto* folder_row = new QHBoxLayout; folder_row->addWidget(folder_, 1);
  browse_ = button(QCoreApplication::translate("App", "Browse…"), files, [this] {
    if (card_.busy()) return;
    const auto path = QFileDialog::getExistingDirectory(settings_dialog_, QCoreApplication::translate("App", "Photo directory"), folder_->text());
    if (!path.isEmpty()) { folder_->setText(path); set_photo_directory(); save_settings(); }
  });
  folder_row->addWidget(browse_); files_layout->addRow(QCoreApplication::translate("App", "Photo folder"), folder_row);
  files_layout->addRow(button(QCoreApplication::translate("App", "Open photo directory"), files, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(folder_->text())); }));
  auto* dates = new QLabel(QCoreApplication::translate("App", "Imported files are organized by capture date within the selected directory."), files); dates->setWordWrap(true); files_layout->addRow(dates);
  pages->addTab(files, QCoreApplication::translate("App", "Directory and files"));
  auto* preview = new QWidget(pages); auto* preview_layout = new QFormLayout(preview); preview_layout->setObjectName("preview_settings_form");
  studio_ = new QCheckBox(QCoreApplication::translate("App", "Preview after capture (no RAW)"), preview); studio_->setObjectName("studio_enabled");
  studio_->setToolTip(QCoreApplication::translate("App", "Requests a compressed preview of the latest photo on the card. The original is not downloaded; resolution depends on the camera."));
  review_seconds_ = new QSpinBox(preview); review_seconds_->setObjectName("review_seconds"); review_seconds_->setRange(2, 10); review_seconds_->setSuffix(QCoreApplication::translate("App", " s"));
  review_quality_ = new QComboBox(preview); review_quality_->setObjectName("review_quality");
  review_quality_->addItem(QCoreApplication::translate("App", "Compressed camera preview"));
  review_quality_->addItem(QCoreApplication::translate("App", "JPEG from card · original size"));
  review_quality_->setToolTip(QCoreApplication::translate("App", "For original size, enable JPEG or RAW+JPEG on the camera. Only JPEG is transferred; speed depends on the network."));
  preview_layout->addRow(studio_);
  preview_layout->addRow(QCoreApplication::translate("App", "Studio review quality"), review_quality_);
  preview_layout->addRow(QCoreApplication::translate("App", "Review duration"), review_seconds_);
  pages->addTab(preview, QCoreApplication::translate("App", "Preview"));
  hold_ = new QCheckBox(QCoreApplication::translate("App", "Hold photo"), this); hold_->setObjectName("review_hold"); hold_->setEnabled(false); hold_->hide();
  auto* saved = new QLabel(QCoreApplication::translate("App", "Changes are saved automatically."), settings_dialog_); layout->addWidget(saved);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, settings_dialog_); layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, settings_dialog_, &QDialog::reject);
  connect(settings_dialog_, &QDialog::finished, this, [this] {
    save_settings();
    if (!card_.busy() && QDir(folder_->text().trimmed()).absolutePath() != photos_.directory()) set_photo_directory();
  });
}
void MainWindow::show_settings(int page) {
  settings_dialog_->findChild<QTabWidget*>("settings_tabs")->setCurrentIndex(page);
  settings_dialog_->show(); settings_dialog_->raise(); settings_dialog_->activateWindow();
}
QPushButton* MainWindow::command_button(const QString& label, const QString& command, QWidget* parent) {
  auto* action = button(label, parent, [this, command] { send(command); });
  action->setObjectName("command_" + QString(command).replace(' ', '_'));
  camera_actions_.append(action); return action;
}
AppSettings MainWindow::values() const {
  AppSettings v;
  v.language = language_;
  v.host = host_->text().trimmed(); v.model = model_->text().trimmed();
  v.user = user_->text(); v.password = password_->text(); v.folder = folder_->text().trimmed();
  v.remember_credentials = remember_->isChecked(); v.auto_sync = false;
  v.auto_liveview = auto_liveview_->isChecked(); v.auto_reconnect = auto_reconnect_->isChecked();
  v.auto_connect = auto_connect_->isChecked();
  v.card_original_video = card_widget_->video_quality_selector()->currentIndex() == 0;
  v.card_full_jpeg = card_widget_->preview_quality_selector()->currentIndex() == 0;
  v.studio_review = studio_->isChecked(); v.studio_full_jpeg = review_quality_->currentIndex() == 1; v.review_seconds = review_seconds_->value();
  v.geometry = saveGeometry(); v.sdk_terms_accepted = settings_.load().sdk_terms_accepted; return v;
}
void MainWindow::load_settings() {
  loading_ = true;
  const auto v = settings_.load();
  language_ = v.language;
  host_->setText(v.host); model_->setText(v.model); user_->setText(v.user); password_->setText(v.password);
  folder_->setText(v.folder); remember_->setChecked(v.remember_credentials);
  auto_liveview_->setChecked(v.auto_liveview); auto_reconnect_->setChecked(v.auto_reconnect);
  auto_connect_->setChecked(v.auto_connect);
  card_widget_->video_quality_selector()->setCurrentIndex(v.card_original_video ? 0 : 1);
  card_widget_->preview_quality_selector()->setCurrentIndex(v.card_full_jpeg ? 0 : 1);
  studio_->setChecked(v.studio_review); review_quality_->setCurrentIndex(v.studio_full_jpeg ? 1 : 0); review_seconds_->setValue(v.review_seconds);
  if (!v.geometry.isEmpty()) restoreGeometry(v.geometry);
  loading_ = false;
}
void MainWindow::save_settings() {
  if (!loading_ && !settings_.save(values())) statusBar()->showMessage(QCoreApplication::translate("App", "Could not save settings: ") + settings_.path());
}
void MainWindow::connect_on_startup() {
  if (!auto_connect_->isChecked() || session_.active()) return;
  const auto v = values();
  if (!v.remember_credentials || v.user.isEmpty() || v.password.isEmpty() || !v.sdk_terms_accepted) {
    const auto message = QCoreApplication::translate("App", "To connect at startup, save your username and password and connect manually once.");
    state_->setText(message); statusBar()->showMessage(message); return;
  }
  connect_camera(true);
}
void MainWindow::connect_camera(bool automatic) {
  const auto v = values();
  auto failed = [this, automatic](const QString& title, const QString& message) {
    if (automatic) { state_->setText(message); statusBar()->showMessage(message); }
    else QMessageBox::warning(this, title, message);
  };
  QHostAddress address;
  if (!address.setAddress(v.host) || address.protocol() != QAbstractSocket::IPv4Protocol || v.model.isEmpty()) {
    failed(QCoreApplication::translate("App", "Connection"), QCoreApplication::translate("App", "Enter the camera IPv4 address and model (e.g. a6700).")); return;
  }
  if (v.folder.isEmpty() || !QDir().mkpath(v.folder)) {
    failed(QCoreApplication::translate("App", "Directory"), QCoreApplication::translate("App", "Could not create the photo directory.")); return;
  }
  if (!settings_.load().sdk_terms_accepted) {
    if (automatic) {
      const auto message = QCoreApplication::translate("App", "To connect at startup, save your username and password and connect manually once.");
      state_->setText(message); statusBar()->showMessage(message); return;
    }
    QMessageBox terms(this);
    terms.setWindowTitle(QCoreApplication::translate("App", "Sony SDK terms of use"));
    terms.setTextFormat(Qt::RichText);
    terms.setText(QCoreApplication::translate("App", "SonShell Remote is an independent application. Application support is provided by this project, not Sony.<br><br>Sony requires agreement to the SDK terms, including camera warranty exclusions when controlled through the SDK (with exceptions for some countries) and restrictions on military and weapons use.<br><br>Read the full <a href=\"https://support.d-imaging.sony.co.jp/app/sdk/licenseagreement/en.html\">Sony terms</a> before connecting."));
    auto* accept = terms.addButton(QCoreApplication::translate("App", "Accept Sony terms"), QMessageBox::AcceptRole);
    terms.addButton(QCoreApplication::translate("App", "Cancel"), QMessageBox::RejectRole);
    const auto local_license = QDir(QCoreApplication::applicationDirPath()).filePath("../share/doc/sonshell-remote/sony-sdk-license.html");
    if (QFileInfo::exists(local_license)) {
      auto* read = terms.addButton(QCoreApplication::translate("App", "Open full text"), QMessageBox::ActionRole);
      connect(read, &QAbstractButton::clicked, &terms, [local_license] { QDesktopServices::openUrl(QUrl::fromLocalFile(local_license)); });
    }
    terms.exec();
    if (terms.clickedButton() != accept) return;
    auto accepted = values(); accepted.sdk_terms_accepted = true;
    if (!settings_.save(accepted)) { statusBar()->showMessage(QCoreApplication::translate("App", "Could not save acceptance of terms")); return; }
  }
  save_settings(); set_photo_directory();
  session_.start({QFileInfo(binary_).absoluteFilePath(), v.host, v.model, v.user, v.password, QDir(v.folder).absolutePath(), v.auto_reconnect});
}
void MainWindow::send(const QString& command) {
  if (!session_.send_command(command)) statusBar()->showMessage(QCoreApplication::translate("App", "Command not sent: camera is not connected"), 3000);
}
void MainWindow::open_photo() {
  if (photo_.isNull()) { statusBar()->showMessage(QCoreApplication::translate("App", "Select an available photo in the gallery"), 3000); return; }
  if (!photo_window_) {
    photo_window_ = new ImageViewer(this);
    photo_window_->setWindowFlags(Qt::Window);
    photo_window_->setAttribute(Qt::WA_DeleteOnClose);
    photo_window_->setWindowTitle(QCoreApplication::translate("App", "Photo · SonShell Remote"));
    photo_window_->setObjectName("photo_window");
    photo_window_->set_navigation_enabled(true);
    connect(photo_window_, &ImageViewer::navigate_requested, gallery_, &GalleryWidget::navigate);
    photo_window_->resize(1100, 800);
  }
  photo_window_->set_local_video(MediaReader::is_video(photo_path_) ? photo_path_ : QString());
  photo_window_->set_image(photo_, photo_caption_);
  photo_window_->show(); photo_window_->view()->actual_size(); photo_window_->raise(); photo_window_->activateWindow();
}
void MainWindow::update_frame() {
  if (!liveview_running_) return;
  const QFileInfo info(session_.liveview_path());
  if (!info.exists() || info.lastModified() == last_frame_) return;
  QImageReader reader(info.absoluteFilePath()); const auto image = reader.read();
  if (image.isNull()) return;
  last_frame_ = info.lastModified(); last_live_image_ = image;
  if (!review_.active()) live_view_->set_image(image, QString("Live View · %1 × %2").arg(image.width()).arg(image.height()));
}
void MainWindow::set_photo_directory() {
  const auto path = folder_->text().trimmed();
  if (QDir(path).absolutePath() != photos_.directory()) {
    review_.cancel(); photo_ = {}; photo_path_.clear(); decode_pending_ = false;
    MediaResult empty; empty.source = QCoreApplication::translate("App", "Select a photo in the gallery"); gallery_->show_photo(empty);
    if (photo_window_) photo_window_->close();
  }
  photos_.set_directory(path); card_.set_directory(path);
  update_file_path();
}
void MainWindow::load_photo(const QString& path, bool open) {
  photo_path_ = path; update_file_path();
  decode_pending_ = true; open_after_load_ = open; photos_.load(path);
}
void MainWindow::update_file_path() {
  const auto path = tabs_->currentIndex() == 0 && review_.active() ? studio_path_ : tabs_->currentIndex() == 1 ? card_widget_->selected_path() :
      tabs_->currentIndex() == 2 && !photo_path_.isEmpty() ? QFileInfo(photo_path_).absoluteFilePath() : QString();
  file_path_->setText(path); file_path_->setToolTip(path); file_path_->setCursorPosition(0);
}
void MainWindow::closeEvent(QCloseEvent* event) {
  startup_connect_.stop();
  review_.cancel();
  live_view_->leave_fullscreen(); photo_view_->leave_fullscreen(); card_widget_->leave_fullscreen();
  if (photo_window_) photo_window_->close();
  if (about_) about_->close();
  save_settings(); session_.stop(); QMainWindow::closeEvent(event);
}

void MainWindow::create_menus() {
  auto* settings = menuBar()->addMenu(QCoreApplication::translate("App", "Settings"));
  auto add_settings = [this, settings](const QString& label, const QString& name, int page) {
    auto* action = settings->addAction(label); action->setObjectName(name);
    connect(action, &QAction::triggered, this, [this, page] { show_settings(page); });
    return action;
  };
  add_settings(QCoreApplication::translate("App", "Connection…"), "settings_connection", 0)->setShortcut(QKeySequence("Ctrl+,"));
  add_settings(QCoreApplication::translate("App", "Directory and files…"), "settings_files", 1);
  add_settings(QCoreApplication::translate("App", "Preview…"), "settings_preview", 2);
  settings->addSeparator();
  auto* journal = settings->addAction(QCoreApplication::translate("App", "Connection log")); journal->setObjectName("settings_log");
  journal->setCheckable(true); connect(journal, &QAction::toggled, log_, &QWidget::setVisible);
  auto* languages = settings->addMenu(QCoreApplication::translate("App", "Language"));
  auto* group = new QActionGroup(this);
  for (const auto& code : {QString("en"), QString("ru")}) {
    auto* action = languages->addAction(code == "ru" ? QCoreApplication::translate("App", "Russian") : QString("English"));
    action->setObjectName("language_" + code); action->setCheckable(true); group->addAction(action);
    action->setChecked(language_ == code);
    connect(action, &QAction::triggered, this, [this, code] {
      const auto old = language_; language_ = code;
      if (!settings_.save(values())) {
        language_ = old;
        if (auto* previous = findChild<QAction*>("language_" + old)) previous->setChecked(true);
        statusBar()->showMessage(QCoreApplication::translate("App", "Could not save settings: ") + settings_.path());
        return;
      }
      statusBar()->showMessage(QCoreApplication::translate("App", "Language changes on next launch"));
    });
  }
  auto* help = menuBar()->addMenu(QCoreApplication::translate("App", "Help"));
  auto* guide = help->addAction(QCoreApplication::translate("App", "User guide and keys · F1")); guide->setObjectName("help_action");
  guide->setShortcut(Qt::Key_F1); guide->setShortcutContext(Qt::ApplicationShortcut);
  connect(guide, &QAction::triggered, this, &MainWindow::show_help);
  auto* about = help->addAction(QCoreApplication::translate("App", "About")); about->setObjectName("about_action");
  connect(about, &QAction::triggered, this, [this] {
    if (!about_) { about_ = new AboutDialog(this); about_->setAttribute(Qt::WA_DeleteOnClose); }
    about_->show(); about_->raise(); about_->activateWindow();
  });
}
void MainWindow::show_help() {
  if (!help_) {
    // Use the currently displayed interface language, not a pending restart choice.
    const auto current = LanguageSupport::current_language();
    help_ = new HelpDialog(current, this); help_->setAttribute(Qt::WA_DeleteOnClose);
  }
  help_->show(); help_->raise(); help_->activateWindow();
}
