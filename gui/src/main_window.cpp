#include "main_window.h"
#include "image_viewer.h"
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
    : QMainWindow(parent), binary_(std::move(binary)), session_(this), photos_(this) {
  setWindowTitle("SonShell Remote");
  resize(1280, 870);
  create_ui();
  load_settings();
  connect(&session_, &CameraSession::state_changed, this, [this](auto state, const QString& message) {
    state_->setText(message);
    const bool connected = state == CameraSession::State::Connected;
    for (auto* action : camera_actions_) action->setEnabled(connected);
    connect_button_->setText(session_.active() ? "Отключить" : "Подключить");
    connect_button_->setEnabled(state != CameraSession::State::Stopping);
    for (auto* input : {host_, model_, user_, password_, folder_}) input->setEnabled(!session_.active());
    statusBar()->showMessage(message);
    if (!connected) {
      liveview_running_ = false;
      live_view_->set_caption("Live View · " + message);
    }
  });
  connect(&session_, &CameraSession::log_received, log_, &QPlainTextEdit::appendPlainText);
  connect(&session_, &CameraSession::connected, this, [this] {
    send(auto_sync_->isChecked() ? "sync on" : "sync off");
    if (auto_liveview_->isChecked()) send("monitor start");
    send("status");
  });
  connect(&session_, &CameraSession::liveview_changed, this, [this](bool running) {
    liveview_running_ = running;
    last_frame_ = {};
    live_view_->set_caption(running ? "Live View · ожидаем кадр…" : "Live View · остановлен");
  });
  connect(&photos_, &PhotoLibrary::image_ready, this, [this](const QString& path, const QImage& image) {
    photo_ = image;
    photo_path_ = path;
    const auto caption = QFileInfo(path).fileName() + QString(" · %1 × %2").arg(image.width()).arg(image.height());
    photo_view_->set_image(image, caption);
    if (photo_window_) photo_window_->set_image(image, caption);
    statusBar()->showMessage("Получено фото: " + QFileInfo(path).fileName(), 5000);
  });
  photos_.set_directory(folder_->text());
  frames_timer_.setInterval(70);
  connect(&frames_timer_, &QTimer::timeout, this, &MainWindow::update_frame);
  frames_timer_.start();
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
  auto_sync_ = new QCheckBox("Auto Sync", connection_box);
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
  connection->addWidget(button("Обзор…", connection_box, [this] {
    if (session_.active()) return;
    const auto path = QFileDialog::getExistingDirectory(this, "Каталог снимков", folder_->text());
    if (!path.isEmpty()) { folder_->setText(path); photos_.set_directory(path); save_settings(); }
  }), 1, 7);
  connection->addWidget(remember_, 2, 0, 1, 2);
  connection->addWidget(auto_sync_, 2, 2);
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
  auto* transfer = new QGroupBox("Передача и Live View", controls); auto* transfer_layout = new QVBoxLayout(transfer);
  transfer_layout->addWidget(command_button("Sync · новые файлы", "sync", transfer));
  transfer_layout->addWidget(command_button("Sync All · скачать всё", "sync all", transfer));
  transfer_layout->addWidget(command_button("Live View · включить", "monitor start", transfer));
  transfer_layout->addWidget(command_button("Live View · выключить", "monitor stop", transfer));
  transfer_layout->addWidget(button("Открыть каталог фото", transfer, [this] {
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder_->text()));
  }));
  controls_layout->addWidget(transfer);
  auto* info = new QLabel("JPEG: оригинал и 100%.\nRAW передаётся SonShell; просмотр RAW,\nгалерея и EXIF — следующие этапы.", controls);
  info->setWordWrap(true); controls_layout->addWidget(info);
  controls_layout->addStretch(); scroll->setWidget(controls);
  auto* right = new QWidget(split); auto* right_layout = new QVBoxLayout(right);
  right_layout->setContentsMargins(0, 0, 0, 0);
  auto* tabs = new QTabWidget(right);
  auto* live_host = new QWidget(tabs); auto* live_layout = new QVBoxLayout(live_host);
  live_view_ = new ImageViewer(live_host); live_view_->setObjectName("live_view");
  live_view_->set_caption("Live View · подключи камеру"); live_layout->addWidget(live_view_, 1);
  auto* photo_host = new QWidget(tabs); auto* photo_layout = new QVBoxLayout(photo_host);
  photo_layout->addWidget(button("Открыть фотографию в отдельном окне · 100%", photo_host, [this] { open_photo(); }));
  photo_view_ = new ImageViewer(photo_host); photo_view_->set_caption("Ожидаем JPEG в выбранном каталоге…");
  photo_layout->addWidget(photo_view_, 1);
  tabs->addTab(live_host, "Live View"); tabs->addTab(photo_host, "Последняя фотография");
  right_layout->addWidget(tabs, 4);
  log_ = new QPlainTextEdit(right); log_->setReadOnly(true); log_->setMaximumBlockCount(1500);
  log_->setPlaceholderText("Журнал соединения и передачи файлов"); log_->setMaximumHeight(180);
  right_layout->addWidget(log_, 1);
  split->addWidget(scroll); split->addWidget(right); split->setStretchFactor(1, 1); split->setSizes({320, 900});
  layout->addWidget(split, 1); setCentralWidget(root);
  for (auto* action : camera_actions_) action->setEnabled(false);
  for (auto* editor : {host_, model_, user_, password_, folder_}) connect(editor, &QLineEdit::editingFinished, this, [this, editor] {
    save_settings(); if (editor == folder_) photos_.set_directory(folder_->text());
  });
  connect(remember_, &QCheckBox::toggled, this, [this](bool remember) {
    if (loading_) return;
    if (!remember && !settings_.forget_credentials()) statusBar()->showMessage("Не удалось удалить сохранённые данные");
    save_settings();
  });
  connect(auto_sync_, &QCheckBox::toggled, this, [this](bool on) {
    if (!loading_) { send(on ? "sync on" : "sync off"); save_settings(); }
  });
  connect(auto_liveview_, &QCheckBox::toggled, this, [this] { if (!loading_) save_settings(); });
  connect(auto_reconnect_, &QCheckBox::toggled, this, [this](bool on) {
    if (!loading_) { session_.set_auto_reconnect(on); save_settings(); }
  });
}
QPushButton* MainWindow::command_button(const QString& label, const QString& command, QWidget* parent) {
  auto* action = button(label, parent, [this, command] { send(command); });
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
  v.remember_credentials = remember_->isChecked(); v.auto_sync = auto_sync_->isChecked();
  v.auto_liveview = auto_liveview_->isChecked(); v.auto_reconnect = auto_reconnect_->isChecked();
  v.geometry = saveGeometry(); v.sdk_terms_accepted = settings_.load().sdk_terms_accepted; return v;
}
void MainWindow::load_settings() {
  loading_ = true;
  const auto v = settings_.load();
  host_->setText(v.host); model_->setText(v.model); user_->setText(v.user); password_->setText(v.password);
  folder_->setText(v.folder); remember_->setChecked(v.remember_credentials); auto_sync_->setChecked(v.auto_sync);
  auto_liveview_->setChecked(v.auto_liveview); auto_reconnect_->setChecked(v.auto_reconnect);
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
  save_settings(); photos_.set_directory(v.folder);
  session_.start({QFileInfo(binary_).absoluteFilePath(), v.host, v.model, v.user, v.password, QDir(v.folder).absolutePath(), v.auto_reconnect});
}
void MainWindow::send(const QString& command) {
  if (!session_.send_command(command)) statusBar()->showMessage("Команда не отправлена: камера не подключена", 3000);
}
void MainWindow::open_photo() {
  if (photo_.isNull()) { statusBar()->showMessage("В каталоге пока нет готового JPEG", 3000); return; }
  if (!photo_window_) {
    photo_window_ = new ImageViewer(this);
    photo_window_->setWindowFlags(Qt::Window);
    photo_window_->setAttribute(Qt::WA_DeleteOnClose);
    photo_window_->setWindowTitle("Фотография · SonShell Remote");
    photo_window_->resize(1100, 800);
  }
  photo_window_->set_image(photo_, QFileInfo(photo_path_).fileName());
  photo_window_->show(); photo_window_->view()->actual_size(); photo_window_->raise(); photo_window_->activateWindow();
}
void MainWindow::update_frame() {
  if (!liveview_running_) return;
  const QFileInfo info(session_.liveview_path());
  if (!info.exists() || info.lastModified() == last_frame_) return;
  QImageReader reader(info.absoluteFilePath()); const auto image = reader.read();
  if (image.isNull()) return;
  last_frame_ = info.lastModified();
  live_view_->set_image(image, QString("Live View · %1 × %2").arg(image.width()).arg(image.height()));
}
void MainWindow::closeEvent(QCloseEvent* event) {
  live_view_->leave_fullscreen(); photo_view_->leave_fullscreen();
  if (photo_window_) photo_window_->close();
  save_settings(); session_.stop(); QMainWindow::closeEvent(event);
}
