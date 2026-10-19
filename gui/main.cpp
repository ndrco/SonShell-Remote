// SonShell Remote GUI — Qt 6 front-end for the existing SonShell REPL.
// No Sony SDK headers or proprietary libraries are bundled.
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSocketNotifier>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace {
QString default_binary() {
  const QDir executable_dir(QCoreApplication::applicationDirPath());
  return QDir::cleanPath(executable_dir.absoluteFilePath("../build/sonshell"));
}

QString strip_terminal_codes(QString s) {
  static const QRegularExpression ansi(QStringLiteral("\\x1B\\[[0-9;?]*[ -/]*[@-~]"));
  s.remove(ansi);
  s.replace('\r', '\n');
  return s;
}

QPushButton* make_button(const QString& label, QWidget* parent, const std::function<void()>& action) {
  auto* button = new QPushButton(label, parent);
  QObject::connect(button, &QPushButton::clicked, parent, [action](bool) { action(); });
  return button;
}
}  // namespace

class MainWindow final : public QMainWindow {
 public:
  explicit MainWindow(QString binary, QWidget* parent = nullptr)
      : QMainWindow(parent), binary_(std::move(binary)) {
    setWindowTitle(QStringLiteral("Sony α6700 — SonShell Remote (MVP)"));
    resize(1170, 790);
    create_ui();
    load_settings();
    files_timer_.setInterval(1800);
    connect(&files_timer_, &QTimer::timeout, this, [this] { check_new_image(); });
    files_timer_.start();
    child_timer_.setInterval(400);
    connect(&child_timer_, &QTimer::timeout, this, [this] { check_child(); });
  }

  ~MainWindow() override { stop_child(); }

 protected:
  void closeEvent(QCloseEvent* event) override {
    save_settings();
    stop_child();
    QMainWindow::closeEvent(event);
  }

  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == preview_ && event->type() == QEvent::Resize) {
      QTimer::singleShot(0, this, [this] { refresh_preview_pixmap(); });
    }
    return QMainWindow::eventFilter(watched, event);
  }

 private:
  void create_ui() {
    auto* root = new QWidget(this);
    auto* root_layout = new QVBoxLayout(root);
    auto* connection_box = new QGroupBox(QStringLiteral("Подключение Sony α6700"), root);
    auto* connection = new QGridLayout(connection_box);

    ip_ = new QLineEdit(connection_box);
    ip_->setPlaceholderText("10.0.1.5");
    user_ = new QLineEdit(connection_box);
    user_->setPlaceholderText(QStringLiteral("Имя пользователя камеры"));
    password_ = new QLineEdit(connection_box);
    password_->setEchoMode(QLineEdit::Password);
    password_->setPlaceholderText(QStringLiteral("Пароль Access Authentication"));
    folder_ = new QLineEdit(connection_box);
    folder_->setPlaceholderText(QDir::homePath() + "/Pictures/Sony");
    auto* browse = make_button(QStringLiteral("Обзор…"), connection_box, [this] {
      const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка снимков"), folder_->text());
      if (!path.isEmpty()) folder_->setText(path);
    });
    auto_sync_ = new QCheckBox(QStringLiteral("Автоматически скачивать новые снимки"), connection_box);
    auto_monitor_ = new QCheckBox(QStringLiteral("Открывать Live View после соединения"), connection_box);
    connect_button_ = make_button(QStringLiteral("Подключить"), connection_box, [this] {
      if (pid_ > 0) stop_child(); else start_child();
    });
    state_ = new QLabel(QStringLiteral("Не подключено"), connection_box);

    connection->addWidget(new QLabel("IP камеры:"), 0, 0);
    connection->addWidget(ip_, 0, 1);
    connection->addWidget(new QLabel("Имя:"), 0, 2);
    connection->addWidget(user_, 0, 3);
    connection->addWidget(new QLabel("Пароль:"), 0, 4);
    connection->addWidget(password_, 0, 5);
    connection->addWidget(new QLabel(QStringLiteral("Каталог фото:")), 1, 0);
    connection->addWidget(folder_, 1, 1, 1, 4);
    connection->addWidget(browse, 1, 5);
    connection->addWidget(auto_sync_, 2, 0, 1, 2);
    connection->addWidget(auto_monitor_, 2, 2, 1, 2);
    connection->addWidget(connect_button_, 2, 4);
    connection->addWidget(state_, 2, 5);
    connection->setColumnStretch(1, 2);
    connection->setColumnStretch(3, 1);
    root_layout->addWidget(connection_box);

    auto* split = new QSplitter(Qt::Horizontal, root);
    auto* controls = new QWidget(split);
    auto* controls_layout = new QVBoxLayout(controls);
    controls_layout->setContentsMargins(2, 2, 8, 2);

    auto* capture_box = new QGroupBox(QStringLiteral("Съёмка"), controls);
    auto* capture_layout = new QGridLayout(capture_box);
    capture_layout->addWidget(command_button(QStringLiteral("СФОТОГРАФИРОВАТЬ"), "shoot", capture_box), 0, 0, 1, 2);
    capture_layout->addWidget(command_button("AF", "focus", capture_box), 1, 0);
    capture_layout->addWidget(command_button(QStringLiteral("Статус"), "status", capture_box), 1, 1);
    capture_layout->addWidget(command_button(QStringLiteral("Начать видео"), "record start", capture_box), 2, 0);
    capture_layout->addWidget(command_button(QStringLiteral("Стоп видео"), "record stop", capture_box), 2, 1);
    controls_layout->addWidget(capture_box);

    auto* settings_box = new QGroupBox(QStringLiteral("Экспозиция (применяется по команде)"), controls);
    auto* settings_layout = new QFormLayout(settings_box);
    iso_ = new QLineEdit("auto", settings_box);
    iso_->setPlaceholderText("auto / 100 / 800 / 6400");
    shutter_ = new QLineEdit("1/125", settings_box);
    shutter_->setPlaceholderText("1/125");
    aperture_ = new QLineEdit("2.8", settings_box);
    aperture_->setPlaceholderText("2.8");
    exposure_comp_ = new QLineEdit("0", settings_box);
    exposure_comp_->setPlaceholderText("+0.7 / -1.0");
    settings_layout->addRow("ISO:", setting_row(iso_, "iso"));
    settings_layout->addRow(QStringLiteral("Выдержка:"), setting_row(shutter_, "shutter"));
    settings_layout->addRow(QStringLiteral("Диафрагма:"), setting_row(aperture_, "aperture"));
    settings_layout->addRow(QStringLiteral("Экспокоррекция:"), setting_row(exposure_comp_, "comp"));
    controls_layout->addWidget(settings_box);

    auto* transfer_box = new QGroupBox(QStringLiteral("Фото и Live View"), controls);
    auto* transfer_layout = new QGridLayout(transfer_box);
    transfer_layout->addWidget(command_button("Sync ON", "sync on", transfer_box), 0, 0);
    transfer_layout->addWidget(command_button("Sync OFF", "sync off", transfer_box), 0, 1);
    transfer_layout->addWidget(command_button(QStringLiteral("Забрать последние"), "sync", transfer_box), 1, 0);
    transfer_layout->addWidget(make_button(QStringLiteral("Скачать всё…"), transfer_box, [this] {
      if (QMessageBox::question(this, QStringLiteral("Синхронизировать всё"),
            QStringLiteral("Это может передать тысячи файлов и занять много времени. Продолжить?")) == QMessageBox::Yes) {
        send_command("sync all");
      }
    }), 1, 1);
    transfer_layout->addWidget(command_button(QStringLiteral("Live View: открыть"), "monitor start", transfer_box), 2, 0);
    transfer_layout->addWidget(command_button(QStringLiteral("Live View: закрыть"), "monitor stop", transfer_box), 2, 1);
    transfer_layout->addWidget(make_button(QStringLiteral("Открыть папку снимков"), transfer_box, [this] {
      QProcess::startDetached("xdg-open", {folder_->text().trimmed()});
    }), 3, 0, 1, 2);
    controls_layout->addWidget(transfer_box);
    controls_layout->addStretch();

    auto* right = new QWidget(split);
    auto* right_layout = new QVBoxLayout(right);
    right_layout->setContentsMargins(8, 2, 2, 2);
    auto* photo_label = new QLabel(QStringLiteral("Последняя загруженная фотография (JPEG)"), right);
    right_layout->addWidget(photo_label);
    preview_ = new QLabel(QStringLiteral("Ожидаем первый JPEG…\nLive View открывается в отдельном окне SonShell."), right);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setStyleSheet("QLabel { background: #20242b; color: #c9d0d9; border-radius: 8px; padding: 12px; }");
    preview_->setMinimumSize(420, 300);
    preview_->installEventFilter(this);
    right_layout->addWidget(preview_, 3);
    log_ = new QPlainTextEdit(right);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(1200);
    log_->setPlaceholderText(QStringLiteral("Диагностика SonShell появится здесь…"));
    right_layout->addWidget(new QLabel(QStringLiteral("Журнал соединения"), right));
    right_layout->addWidget(log_, 2);
    split->addWidget(controls);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({340, 780});
    root_layout->addWidget(split, 1);
    setCentralWidget(root);
    statusBar()->showMessage(QStringLiteral("MVP: SonShell управляется через PTY; Live View — отдельное окно."));
    set_connected(false);
  }

  QWidget* setting_row(QLineEdit* editor, const QString& field) {
    auto* widget = new QWidget(this);
    auto* row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(editor, 1);
    auto* apply = make_button(QStringLiteral("Задать"), widget, [this, editor, field] {
      const QString value = editor->text().trimmed();
      if (!value.isEmpty() && !value.contains('\n') && !value.contains('\r')) {
        send_command("exposure " + field + " " + value);
      }
    });
    row->addWidget(apply);
    actions_.append(apply);
    return widget;
  }

  QPushButton* command_button(const QString& label, const QString& command, QWidget* parent) {
    auto* button = make_button(label, parent, [this, command] { send_command(command); });
    actions_.append(button);
    return button;
  }

  void set_connected(bool connected) {
    connected_ = connected;
    state_->setText(connected ? QStringLiteral("Подключено ✓") : QStringLiteral("Не подключено"));
    for (auto* button : actions_) button->setEnabled(connected);
  }

  void load_settings() {
    QSettings config("SonShell", "RemoteGui");
    ip_->setText(config.value("ip", "10.0.1.5").toString());
    user_->setText(config.value("user", "").toString());
    folder_->setText(config.value("folder", QDir::homePath() + "/Pictures/Sony").toString());
    auto_sync_->setChecked(config.value("auto_sync", true).toBool());
    auto_monitor_->setChecked(config.value("auto_monitor", false).toBool());
  }

  void save_settings() {
    QSettings config("SonShell", "RemoteGui");
    config.setValue("ip", ip_->text());
    config.setValue("user", user_->text());
    config.setValue("folder", folder_->text());
    config.setValue("auto_sync", auto_sync_->isChecked());
    config.setValue("auto_monitor", auto_monitor_->isChecked());
    // Password intentionally never stored.
  }

  void start_child() {
    const QString binary = QFileInfo(binary_).absoluteFilePath();
    if (!QFileInfo(binary).isExecutable()) {
      QMessageBox::warning(this, QStringLiteral("SonShell не найден"),
          QStringLiteral("Не найден исполняемый файл:\n%1\n\nУкажи его через --sonshell /полный/путь/sonshell").arg(binary));
      return;
    }
    const QString ip = ip_->text().trimmed();
    if (ip.isEmpty() || ip.contains(' ') || ip.contains('\n')) {
      QMessageBox::warning(this, QStringLiteral("IP камеры"), QStringLiteral("Введи IP-адрес камеры."));
      return;
    }
    const QString dir = QDir::cleanPath(folder_->text().trimmed());
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
      QMessageBox::warning(this, QStringLiteral("Каталог"), QStringLiteral("Не удалось создать папку для снимков."));
      return;
    }
    folder_->setText(dir);
    save_settings();
    check_new_image();

    std::vector<QByteArray> args;
    args.push_back(QFile::encodeName(binary));
    args.push_back("--host"); args.push_back(ip.toUtf8());
    args.push_back("--model"); args.push_back("a6700");
    args.push_back("--sync-dir"); args.push_back(QFile::encodeName(dir));
    args.push_back("--keepalive"); args.push_back("3000");
    const QString username = user_->text().trimmed();
    if (!username.isEmpty()) {
      args.push_back("--user"); args.push_back(username.toUtf8());
      if (!password_->text().isEmpty()) {
        args.push_back("--pass"); args.push_back(password_->text().toUtf8());
      }
    }

    // forkpty gives libedit a proper terminal (QProcess pipes alone are unreliable).
    int master = -1;
    pid_t child = ::forkpty(&master, nullptr, nullptr, nullptr);
    if (child < 0) {
      QMessageBox::critical(this, QStringLiteral("Ошибка PTY"), QString::fromLocal8Bit(std::strerror(errno)));
      return;
    }
    if (child == 0) {
      ::chdir(QFile::encodeName(QFileInfo(binary).absolutePath()).constData());
      std::vector<char*> argv;
      argv.reserve(args.size() + 1);
      for (auto& argument : args) argv.push_back(argument.data());
      argv.push_back(nullptr);
      ::execv(argv[0], argv.data());
      ::_exit(127);
    }
    pid_ = child;
    master_fd_ = master;
    const int flags = ::fcntl(master_fd_, F_GETFL, 0);
    if (flags >= 0) ::fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);
    notifier_ = std::make_unique<QSocketNotifier>(master_fd_, QSocketNotifier::Read, this);
    connect(notifier_.get(), &QSocketNotifier::activated, this,
            [this](QSocketDescriptor, QSocketNotifier::Type) { read_from_child(); });
    child_timer_.start();
    connect_button_->setText(QStringLiteral("Отключить"));
    state_->setText(QStringLiteral("Подключение…"));
    log_->appendPlainText(QStringLiteral("Запуск SonShell → ") + binary + " @ " + ip);
    // Erase widget password immediately. CLI args are still visible to local process viewers.
    password_->clear();
  }

  void on_connected() {
    if (connected_) return;
    set_connected(true);
    statusBar()->showMessage(QStringLiteral("Sony α6700 подключена по Wi-Fi"));
    if (auto_sync_->isChecked()) send_command("sync on");
    if (auto_monitor_->isChecked()) send_command("monitor start");
    send_command("status");
  }

  void read_from_child() {
    if (master_fd_ < 0) return;
    char buffer[8192];
    while (true) {
      const ssize_t n = ::read(master_fd_, buffer, sizeof(buffer));
      if (n > 0) {
        const QString part = strip_terminal_codes(QString::fromLocal8Bit(buffer, static_cast<int>(n)));
        if (part.contains("Connected. Ctrl+D")) on_connected();
        if (part.contains("Disconnected") || part.contains("Connection lost")) set_connected(false);
        log_->moveCursor(QTextCursor::End);
        log_->insertPlainText(part);
        log_->ensureCursorVisible();
      } else if (n == -1 && errno == EINTR) {
        continue;
      } else {
        break;
      }
    }
  }

  void send_command(const QString& command) {
    if (master_fd_ < 0 || !connected_) return;
    const QByteArray payload = (command + "\n").toUtf8();
    ssize_t sent = ::write(master_fd_, payload.constData(), static_cast<size_t>(payload.size()));
    if (sent < 0) {
      statusBar()->showMessage(QStringLiteral("Не удалось отправить команду: ") + command, 4000);
    }
  }

  void check_child() {
    if (pid_ <= 0) return;
    int status = 0;
    const pid_t result = ::waitpid(pid_, &status, WNOHANG);
    if (result == pid_ || (result < 0 && errno == ECHILD)) {
      read_from_child();
      reset_child();
      log_->appendPlainText(QStringLiteral("\nSonShell завершил работу."));
    }
  }

  void reset_child() {
    child_timer_.stop();
    notifier_.reset();
    if (master_fd_ >= 0) ::close(master_fd_);
    master_fd_ = -1;
    pid_ = -1;
    set_connected(false);
    connect_button_->setText(QStringLiteral("Подключить"));
    statusBar()->showMessage(QStringLiteral("Отключено"));
  }

  void stop_child() {
    if (pid_ <= 0) return;
    const pid_t child = pid_;
    if (master_fd_ >= 0) {
      const char* bye = "quit\n";
      ::write(master_fd_, bye, std::strlen(bye));
    }
    // Give SDK a chance to release the camera before signalling termination.
    for (int i = 0; i < 15; ++i) {
      int status = 0;
      const pid_t r = ::waitpid(child, &status, WNOHANG);
      if (r == child || (r < 0 && errno == ECHILD)) {
        reset_child();
        return;
      }
      ::usleep(100000);
    }
    ::kill(child, SIGTERM);
    ::waitpid(child, nullptr, 0);
    reset_child();
  }

  void check_new_image() {
    const QDir base(folder_ ? folder_->text().trimmed() : QString());
    if (!base.exists()) return;
    QString latest_file;
    QDateTime latest_time;
    QDirIterator it(base.absolutePath(), {"*.jpg", "*.JPG", "*.jpeg", "*.JPEG", "*.png", "*.PNG"},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      it.next();
      const QFileInfo file = it.fileInfo();
      if (file.size() == 0) continue;
      if (latest_file.isEmpty() || file.lastModified() > latest_time) {
        latest_time = file.lastModified();
        latest_file = file.absoluteFilePath();
      }
    }
    if (latest_file.isEmpty() || latest_file == image_path_) return;
    QImageReader reader(latest_file);
    reader.setAutoTransform(true);
    const QImage loaded = reader.read();
    if (loaded.isNull()) return;  // Retry if the transfer is still in progress.
    image_path_ = latest_file;
    image_ = QPixmap::fromImage(loaded);
    refresh_preview_pixmap();
    statusBar()->showMessage(QStringLiteral("Получено фото: ") + QFileInfo(latest_file).fileName(), 5000);
  }

  void refresh_preview_pixmap() {
    if (!image_.isNull() && preview_ && preview_->width() > 10 && preview_->height() > 10) {
      preview_->setPixmap(image_.scaled(preview_->size() - QSize(24, 24), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
  }

  QString binary_;
  pid_t pid_ = -1;
  int master_fd_ = -1;
  bool connected_ = false;
  std::unique_ptr<QSocketNotifier> notifier_;
  QTimer child_timer_;
  QTimer files_timer_;
  QLineEdit *ip_ = nullptr, *user_ = nullptr, *password_ = nullptr, *folder_ = nullptr;
  QLineEdit *iso_ = nullptr, *shutter_ = nullptr, *aperture_ = nullptr, *exposure_comp_ = nullptr;
  QCheckBox *auto_sync_ = nullptr, *auto_monitor_ = nullptr;
  QPushButton* connect_button_ = nullptr;
  QLabel *state_ = nullptr, *preview_ = nullptr;
  QPlainTextEdit* log_ = nullptr;
  QList<QPushButton*> actions_;
  QString image_path_;
  QPixmap image_;
};

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName("SonShell Remote GUI");
  app.setOrganizationName("SonShell");
  QString binary = default_binary();
  const QStringList arguments = QCoreApplication::arguments();
  for (int i = 1; i + 1 < arguments.size(); ++i) {
    if (arguments[i] == "--sonshell") binary = arguments[++i];
  }
  MainWindow window(binary);
  window.show();
  return app.exec();
}
