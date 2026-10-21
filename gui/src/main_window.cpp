#include "main_window.h"
#include "image_viewer.h"
#include "gallery_widget.h"
#include "camera_card_widget.h"
#include <QSpinBox>
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
  load_settings();
  photos_.set_auto_preview(false);
  review_.setObjectName("studio_review");
  review_.configure(studio_->isChecked(), review_seconds_->value());
  card_.set_studio(studio_->isChecked());
  connect(&card_, &CameraCard::copied, this, [this](const QString& path) { photos_.completed_download(path, false); });
  connect(&card_, &CameraCard::busy_changed, this, [this](bool busy) { browse_->setEnabled(!busy); folder_->setEnabled(!busy); });
  connect(&card_, &CameraCard::studio_ready, this, [this](const QImage& image, const QString& name) {
    if (!liveview_running_) return;
    MediaResult result; result.image = image; result.path = name; result.source = "Сжатое превью камеры"; review_.present(result);
  });
  connect(&session_, &CameraSession::state_changed, this, [this](auto state, const QString& message) {
    state_->setText(message);
    const bool connected = state == CameraSession::State::Connected;
    for (auto* action : camera_actions_) action->setEnabled(connected);
    connect_button_->setText(session_.active() ? "Отключить" : "Подключить");
    connect_button_->setEnabled(state != CameraSession::State::Stopping);
    card_.set_connected(connected); card_widget_->setEnabled(connected);
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
    if (!running) review_.cancel();
    last_frame_ = {};
    live_view_->set_caption(running ? "Live View · ожидаем кадр…" : "Live View · остановлен");
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
    if (photo_window_ && !result.image.isNull()) photo_window_->set_image(photo_, photo_caption_);
    if (open_after_load_ && !photo_.isNull()) open_photo();
    open_after_load_ = false;
    Q_UNUSED(new_capture);
    statusBar()->showMessage(result.error.isEmpty() ? "Готово: " + QFileInfo(result.path).fileName() : result.error, 5000);
  });
  connect(&review_, &StudioReview::photo_presented, this, [this](const MediaResult& result) {
    live_view_->set_image(result.image, "Снимок · " + QFileInfo(result.path).fileName() + " · " + result.source);
    tabs_->setCurrentIndex(0);
    hold_->setEnabled(true);
  });
  connect(&review_, &StudioReview::returned_to_liveview, this, [this] {
    { QSignalBlocker guard(hold_); hold_->setChecked(false); }
    hold_->setEnabled(false);
    live_view_->set_image(last_live_image_, last_live_image_.isNull() ? "Live View · ожидаем кадр…" : "Live View");
  });
  set_photo_directory();
  frames_timer_.setInterval(70);
  connect(&frames_timer_, &QTimer::timeout, this, &MainWindow::update_frame);
  frames_timer_.start();
}

MainWindow::~MainWindow() {
  // QObject children outlive C++ value members. No child may call back into
  // PhotoLibrary/StudioReview after those members begin destruction.
  for (auto* child : findChildren<QObject*>()) QObject::disconnect(child, nullptr, this, nullptr);
  live_view_->leave_fullscreen(); photo_view_->leave_fullscreen(); card_widget_->leave_fullscreen();
  frames_timer_.stop(); review_.cancel(); session_.stop();
}

void MainWindow::create_ui() {
  auto* root = new QWidget(this);
  auto* layout = new QVBoxLayout(root);
  auto* connection_box = new QGroupBox("Камера Sony", root);
  auto* connection = new QGridLayout(connection_box);
  host_ = new QLineEdit(connection_box); host_->setObjectName("host");
  model_ = new QLineEdit(connection_box);
  model_->setToolTip("Модель в формате SonShell, например a6700. Возможности зависят от SDK.");
  user_ = new QLineEdit(connection_box); user_->setObjectName("username");
  password_ = new QLineEdit(connection_box); password_->setObjectName("password");
  password_->setEchoMode(QLineEdit::Password);
  folder_ = new QLineEdit(connection_box);
  remember_ = new QCheckBox("Сохранить имя и пароль", connection_box); remember_->setObjectName("remember");
  remember_->setToolTip("Хранить на этом компьютере в файле с правами 0600, без шифрования. Снятие флажка удаляет сохранённые поля.");
  auto_liveview_ = new QCheckBox("Live View при подключении", connection_box);
  auto_reconnect_ = new QCheckBox("Переподключаться автоматически", connection_box);
  connect_button_ = button("Подключить", connection_box, [this] {
    if (session_.active()) session_.stop(); else connect_camera();
  });
  connect_button_->setObjectName("connect");
  state_ = new QLabel("Отключено", connection_box); state_->setObjectName("connection_state");
  connection->addWidget(new QLabel("IP камеры"), 0, 0); connection->addWidget(host_, 0, 1);
  connection->addWidget(new QLabel("Модель"), 0, 2); connection->addWidget(model_, 0, 3);
  connection->addWidget(new QLabel("Имя"), 0, 4); connection->addWidget(user_, 0, 5);
  connection->addWidget(new QLabel("Пароль"), 0, 6); connection->addWidget(password_, 0, 7);
  connection->addWidget(new QLabel("Каталог фото"), 1, 0); connection->addWidget(folder_, 1, 1, 1, 6);
  browse_ = button("Обзор…", connection_box, [this] {
    if (card_.busy()) return;
    const auto path = QFileDialog::getExistingDirectory(this, "Каталог снимков", folder_->text());
    if (!path.isEmpty()) { folder_->setText(path); set_photo_directory(); save_settings(); }
  });
  connection->addWidget(browse_, 1, 7);
  connection->addWidget(remember_, 2, 0, 1, 2);
  connection->addWidget(auto_liveview_, 2, 3, 1, 2);
  connection->addWidget(auto_reconnect_, 2, 5, 1, 3);
  connection->addWidget(state_, 3, 0, 1, 6); connection->addWidget(connect_button_, 3, 6, 1, 2);
  connection->setColumnStretch(1, 1); connection->setColumnStretch(5, 1);
  layout->addWidget(connection_box);
  auto* split = new QSplitter(Qt::Horizontal, root);
  auto* scroll = new QScrollArea(split); scroll->setWidgetResizable(true); scroll->setMinimumWidth(290);
  auto* controls = new QWidget(scroll); auto* controls_layout = new QVBoxLayout(controls);
  auto* capture = new QGroupBox("Съёмка", controls); auto* capture_layout = new QGridLayout(capture);
  capture_layout->addWidget(command_button("СФОТОГРАФИРОВАТЬ", "shoot", capture), 0, 0, 1, 2);
  capture_layout->addWidget(command_button("Автофокус", "focus", capture), 1, 0);
  capture_layout->addWidget(command_button("Статус камеры", "status", capture), 1, 1);
  capture_layout->addWidget(command_button("Начать видео", "record start", capture), 2, 0);
  capture_layout->addWidget(command_button("Остановить видео", "record stop", capture), 2, 1);
  controls_layout->addWidget(capture);
  auto* exposure = new QGroupBox("Экспозиция", controls); auto* exposure_layout = new QFormLayout(exposure);
  exposure_layout->addRow("ISO", setting_row("auto", "exposure iso", exposure));
  exposure_layout->addRow("Выдержка", setting_row("1/125", "exposure shutter", exposure));
  exposure_layout->addRow("Диафрагма", setting_row("2.8", "exposure aperture", exposure));
  exposure_layout->addRow("Коррекция EV", setting_row("0", "exposure comp", exposure));
  controls_layout->addWidget(exposure);
  auto* transfer = new QGroupBox("Live View", controls); auto* transfer_layout = new QVBoxLayout(transfer);
  transfer_layout->addWidget(command_button("Live View · включить", "monitor start", transfer));
  transfer_layout->addWidget(command_button("Live View · выключить", "monitor stop", transfer));
  transfer_layout->addWidget(button("Открыть каталог фото", transfer, [this] {
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder_->text()));
  }));
  controls_layout->addWidget(transfer);
  auto* studio_box = new QGroupBox("Студийная съёмка", controls);
  auto* studio_layout = new QVBoxLayout(studio_box);
  studio_ = new QCheckBox("Превью после спуска (без RAW)", studio_box);
  studio_->setToolTip("Запрашивает сжатое превью последнего кадра с карты. Оригинал не загружается; разрешение определяет камера."); studio_->setObjectName("studio_enabled");
  review_seconds_ = new QSpinBox(studio_box); review_seconds_->setObjectName("review_seconds");
  review_seconds_->setRange(2, 10); review_seconds_->setSuffix(" с");
  hold_ = new QCheckBox("Удерживать кадр", studio_box); hold_->setObjectName("review_hold"); hold_->setEnabled(false);
  studio_layout->addWidget(studio_); studio_layout->addWidget(review_seconds_); studio_layout->addWidget(hold_);
  controls_layout->addWidget(studio_box);
  controls_layout->addStretch(); scroll->setWidget(controls);
  auto* right = new QWidget(split); auto* right_layout = new QVBoxLayout(right);
  right_layout->setContentsMargins(0, 0, 0, 0);
  tabs_ = new QTabWidget(right); tabs_->setObjectName("main_tabs");
  auto* tabs = tabs_;
  auto* live_host = new QWidget(tabs); auto* live_layout = new QVBoxLayout(live_host);
  live_view_ = new ImageViewer(live_host); live_view_->setObjectName("live_view");
  live_view_->set_caption("Live View · подключи камеру"); live_layout->addWidget(live_view_, 1);
  gallery_ = new GalleryWidget(tabs); photo_view_ = gallery_->viewer();
  card_widget_ = new CameraCardWidget(&card_, tabs); card_widget_->setEnabled(false);
  tabs->addTab(live_host, "Live View"); tabs->addTab(card_widget_, "В камере"); tabs->addTab(gallery_, "На компьютере · EXIF");
  connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) { if (index == 1) card_widget_->activated(); });
  right_layout->addWidget(tabs, 4);
  log_ = new QPlainTextEdit(right); log_->setReadOnly(true); log_->setMaximumBlockCount(1500);
  log_->setPlaceholderText("Журнал соединения и передачи файлов"); log_->setMaximumHeight(180);
  right_layout->addWidget(log_, 1);
  split->addWidget(scroll); split->addWidget(right); split->setStretchFactor(1, 1); split->setSizes({320, 900});
  layout->addWidget(split, 1); setCentralWidget(root);
  for (auto* action : camera_actions_) action->setEnabled(false);
  for (auto* editor : {host_, model_, user_, password_, folder_}) connect(editor, &QLineEdit::editingFinished, this, [this, editor] {
    save_settings(); if (editor == folder_) set_photo_directory();
  });
  connect(remember_, &QCheckBox::toggled, this, [this](bool remember) {
    if (loading_) return;
    if (!remember && !settings_.forget_credentials()) statusBar()->showMessage("Не удалось удалить сохранённые данные");
    save_settings();
  });
  connect(auto_liveview_, &QCheckBox::toggled, this, [this] { if (!loading_) save_settings(); });
  auto configure_review = [this] {
    if (loading_) return;
    review_.configure(studio_->isChecked(), review_seconds_->value()); card_.set_studio(studio_->isChecked()); save_settings();
  };
  connect(studio_, &QCheckBox::toggled, this, configure_review);
  connect(review_seconds_, qOverload<int>(&QSpinBox::valueChanged), this, configure_review);
  connect(hold_, &QCheckBox::toggled, &review_, &StudioReview::hold);
  connect(auto_reconnect_, &QCheckBox::toggled, this, [this](bool on) {
    if (!loading_) { session_.set_auto_reconnect(on); save_settings(); }
  });
}
QPushButton* MainWindow::command_button(const QString& label, const QString& command, QWidget* parent) {
  auto* action = button(label, parent, [this, command] { send(command); });
  action->setObjectName("command_" + QString(command).replace(' ', '_'));
  camera_actions_.append(action); return action;
}
QWidget* MainWindow::setting_row(const QString& initial, const QString& command, QWidget* parent) {
  auto* row = new QWidget(parent); auto* layout = new QHBoxLayout(row); layout->setContentsMargins(0, 0, 0, 0);
  auto* editor = new QLineEdit(initial, row); layout->addWidget(editor);
  auto* apply = button("Задать", row, [this, editor, command] { send(command + ' ' + editor->text().trimmed()); });
  layout->addWidget(apply); camera_actions_.append(apply); return row;
}
AppSettings MainWindow::values() const {
  AppSettings v;
  v.host = host_->text().trimmed(); v.model = model_->text().trimmed();
  v.user = user_->text(); v.password = password_->text(); v.folder = folder_->text().trimmed();
  v.remember_credentials = remember_->isChecked(); v.auto_sync = false;
  v.auto_liveview = auto_liveview_->isChecked(); v.auto_reconnect = auto_reconnect_->isChecked();
  v.studio_review = studio_->isChecked(); v.review_seconds = review_seconds_->value();
  v.geometry = saveGeometry(); v.sdk_terms_accepted = settings_.load().sdk_terms_accepted; return v;
}
void MainWindow::load_settings() {
  loading_ = true;
  const auto v = settings_.load();
  host_->setText(v.host); model_->setText(v.model); user_->setText(v.user); password_->setText(v.password);
  folder_->setText(v.folder); remember_->setChecked(v.remember_credentials);
  auto_liveview_->setChecked(v.auto_liveview); auto_reconnect_->setChecked(v.auto_reconnect);
  studio_->setChecked(v.studio_review); review_seconds_->setValue(v.review_seconds);
  if (!v.geometry.isEmpty()) restoreGeometry(v.geometry);
  loading_ = false;
}
void MainWindow::save_settings() {
  if (!loading_ && !settings_.save(values())) statusBar()->showMessage("Не удалось сохранить настройки: " + settings_.path());
}
void MainWindow::connect_camera() {
  const auto v = values();
  QHostAddress address;
  if (!address.setAddress(v.host) || address.protocol() != QAbstractSocket::IPv4Protocol || v.model.isEmpty()) {
    QMessageBox::warning(this, "Подключение", "Введи IPv4 камеры и модель (например a6700)."); return;
  }
  if (v.folder.isEmpty() || !QDir().mkpath(v.folder)) {
    QMessageBox::warning(this, "Каталог", "Не удалось создать каталог снимков."); return;
  }
  if (!settings_.load().sdk_terms_accepted) {
    QMessageBox terms(this);
    terms.setWindowTitle("Условия использования Sony SDK");
    terms.setTextFormat(Qt::RichText);
    terms.setText("SonShell Remote — независимое приложение. Поддержку приложения предоставляет проект, а не Sony.<br><br>"
        "Sony требует согласия с условиями SDK, включая положения об исключении гарантии камеры при управлении через SDK "
        "(с оговорками для отдельных стран) и запреты военного использования и использования с вооружением.<br><br>"
        "Прочитай полные <a href=\"https://support.d-imaging.sony.co.jp/app/sdk/licenseagreement/en.html\">условия Sony</a> перед подключением.");
    auto* accept = terms.addButton("Принимаю условия Sony", QMessageBox::AcceptRole);
    terms.addButton("Отмена", QMessageBox::RejectRole);
    const auto local_license = QDir(QCoreApplication::applicationDirPath()).filePath("../share/doc/sonshell-remote/sony-sdk-license.html");
    if (QFileInfo::exists(local_license)) {
      auto* read = terms.addButton("Открыть полный текст", QMessageBox::ActionRole);
      connect(read, &QAbstractButton::clicked, &terms, [local_license] { QDesktopServices::openUrl(QUrl::fromLocalFile(local_license)); });
    }
    terms.exec();
    if (terms.clickedButton() != accept) return;
    auto accepted = values(); accepted.sdk_terms_accepted = true;
    if (!settings_.save(accepted)) { statusBar()->showMessage("Не удалось сохранить принятие условий"); return; }
  }
  save_settings(); set_photo_directory();
  session_.start({QFileInfo(binary_).absoluteFilePath(), v.host, v.model, v.user, v.password, QDir(v.folder).absolutePath(), v.auto_reconnect});
}
void MainWindow::send(const QString& command) {
  if (!session_.send_command(command)) statusBar()->showMessage("Команда не отправлена: камера не подключена", 3000);
}
void MainWindow::open_photo() {
  if (photo_.isNull()) { statusBar()->showMessage("Выбери готовый снимок в галерее", 3000); return; }
  if (!photo_window_) {
    photo_window_ = new ImageViewer(this);
    photo_window_->setWindowFlags(Qt::Window);
    photo_window_->setAttribute(Qt::WA_DeleteOnClose);
    photo_window_->setWindowTitle("Фотография · SonShell Remote");
    photo_window_->setObjectName("photo_window");
    photo_window_->set_navigation_enabled(true);
    connect(photo_window_, &ImageViewer::navigate_requested, gallery_, &GalleryWidget::navigate);
    photo_window_->resize(1100, 800);
  }
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
    MediaResult empty; empty.source = "Выбери снимок в галерее"; gallery_->show_photo(empty);
    if (photo_window_) photo_window_->close();
  }
  photos_.set_directory(path); card_.set_directory(path);
}
void MainWindow::load_photo(const QString& path, bool open) {
  decode_pending_ = true; open_after_load_ = open; photos_.load(path);
}
void MainWindow::closeEvent(QCloseEvent* event) {
  review_.cancel();
  live_view_->leave_fullscreen(); photo_view_->leave_fullscreen(); card_widget_->leave_fullscreen();
  if (photo_window_) photo_window_->close();
  save_settings(); session_.stop(); QMainWindow::closeEvent(event);
}
