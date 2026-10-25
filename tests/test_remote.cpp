#include "camera_session.h"
#include "image_viewer.h"
#include "main_window.h"
#include "photo_library.h"
#include "settings_store.h"
#include <QtTest>
#include <QBuffer>
#include <QGraphicsScene>
#include <QCheckBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <sys/stat.h>

class RemoteTests : public QObject {
  Q_OBJECT
 private:
  QTemporaryDir temporary_;
  ConnectionOptions options() {
    return {FAKE_BACKEND_PATH, "127.0.0.1", "a6700", "test-user", "test-secret", temporary_.path(), true};
  }
 private slots:
  void initTestCase() {
    QVERIFY(temporary_.isValid());
    qputenv("XDG_CONFIG_HOME", temporary_.filePath("config").toUtf8());
    QCoreApplication::setOrganizationName("SonShellRemoteTests");
  }
  void cleanup() {
    qunsetenv("REMOTE_TEST_MODE");
    qunsetenv("REMOTE_TEST_RECORD");
    qunsetenv("REMOTE_TEST_JPEG");
    QVERIFY(SettingsStore().save(AppSettings{}));
  }
  void credentials_opt_in_and_forget() {
    const QString path = temporary_.filePath("settings/settings.ini");
    SettingsStore store(path);
    AppSettings values;
    values.user = "test-user"; values.password = "test-secret";
    values.studio_review = true; values.studio_full_jpeg = true; values.review_seconds = 7;
    QVERIFY(store.save(values));
    QVERIFY(store.load().studio_review); QVERIFY(store.load().studio_full_jpeg); QCOMPARE(store.load().review_seconds, 7);
    QVERIFY(store.load().user.isEmpty()); QVERIFY(store.load().password.isEmpty());
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(!file.readAll().contains("test-secret")); file.close();
    values.remember_credentials = true;
    QVERIFY(store.save(values));
    QCOMPARE(store.load().user, values.user); QCOMPARE(store.load().password, values.password);
    struct stat info{};
    QVERIFY(::stat(QFile::encodeName(path).constData(), &info) == 0);
    QCOMPARE(info.st_mode & 0777, 0600u);
    QVERIFY(store.forget_credentials());
    QVERIFY(!store.load().remember_credentials); QVERIFY(store.load().password.isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto data = file.readAll();
    QVERIFY(!data.contains("test-secret")); QVERIFY(!data.contains("test-user"));
  }
  void viewer_zoom_and_restore() {
    QWidget host; auto* layout = new QVBoxLayout(&host);
    auto* viewer = new ImageViewer(&host); layout->addWidget(viewer, 1);
    QImage image(2400, 1600, QImage::Format_RGB32); image.fill(Qt::blue);
    viewer->set_image(image); host.resize(700, 500); host.show();
    QTest::qWait(30);
    QVERIFY(viewer->view()->zoom_factor() < 1.0);
    viewer->view()->actual_size(); QCOMPARE(viewer->view()->zoom_factor(), 1.0);
    viewer->view()->zoom(1.25); QCOMPARE(viewer->view()->zoom_factor(), 1.25);
    viewer->toggle_fullscreen(); QVERIFY(viewer->isFullScreen());
    viewer->leave_fullscreen();
    QCOMPARE(viewer->parentWidget(), &host); QVERIFY(!viewer->isWindow());
    QCOMPARE(layout->indexOf(viewer), 0);
    viewer->toggle_fullscreen(); viewer->close();
    QVERIFY(!viewer->isFullScreen()); QCOMPARE(layout->indexOf(viewer), 0);
    viewer->view()->fit_image(); QVERIFY(viewer->view()->is_fit());
  }
  void partial_download_and_replacement() {
    QTemporaryDir directory;
    const auto path = directory.filePath("test.jpg");
    QImage image(600, 400, QImage::Format_RGB32); image.fill(Qt::red);
    QByteArray jpeg; QBuffer buffer(&jpeg); QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "JPEG"));
    QFile output(path); QVERIFY(output.open(QIODevice::WriteOnly));
    output.write(jpeg.left(jpeg.size() - 2)); output.close();
    PhotoLibrary library; QSignalSpy ready(&library, &PhotoLibrary::image_ready);
    library.set_directory(directory.path()); library.scan(); QCOMPARE(ready.size(), 0);
    QVERIFY(output.open(QIODevice::Append)); output.write(jpeg.right(2)); output.close();
    library.scan(); library.scan(); QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 4000);
    library.scan(); QCOMPARE(ready.size(), 1);
    image = QImage(700, 500, QImage::Format_RGB32); image.fill(Qt::green); QVERIFY(image.save(path));
    library.scan(); library.scan(); QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 4000);
  }
  void fragmented_events_commands_and_private_arguments() {
    const auto record = temporary_.filePath("record.jsonl");
    qputenv("REMOTE_TEST_RECORD", record.toUtf8());
    CameraSession session;
    QSignalSpy connected(&session, &CameraSession::connected);
    auto connection = options(); connection.password = "connected";
    session.start(connection);
    QVERIFY(!session.send_command("shoot"));
    QTRY_COMPARE(connected.size(), 1);
    QCOMPARE(session.state(), CameraSession::State::Connected);
    QVERIFY(session.send_command("shoot"));
    QVERIFY(!session.send_command("shoot\nquit"));
    session.stop();
    QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
    QFile file(record); QVERIFY(file.open(QIODevice::ReadOnly)); const auto data = file.readAll();
    QVERIFY(data.contains("shoot")); QVERIFY(data.contains("--auth-env"));
    QVERIFY(data.contains("\"has_password\": true"));
    QVERIFY(data.contains("\"private_loader_path\": true"));
    QVERIFY(!data.contains("test-secret")); QVERIFY(!data.contains("--pass"));
  }
  void reconnect_after_disconnect_and_manual_cancel() {
    qputenv("REMOTE_TEST_MODE", "disconnect");
    CameraSession session; session.set_retry_interval(40);
    QSignalSpy connected(&session, &CameraSession::connected);
    session.start(options());
    QTRY_VERIFY(connected.size() >= 2);
    session.stop();
    QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
    const auto count = connected.size(); QTest::qWait(180); QCOMPARE(connected.size(), count);
  }
  void failed_connection_retry_toggle() {
    qputenv("REMOTE_TEST_MODE", "fail");
    CameraSession session; session.set_retry_interval(500);
    session.start(options());
    QTRY_COMPARE(session.state(), CameraSession::State::RetryWaiting);
    session.set_auto_reconnect(false);
    QCOMPARE(session.state(), CameraSession::State::Disconnected);
    QTest::qWait(550); QVERIFY(!session.active());
  }
  void stopped_process_is_killed_without_freezing_ui() {
    qputenv("REMOTE_TEST_MODE", "hang");
    CameraSession session; session.start(options());
    QTest::qWait(80);
    QElapsedTimer elapsed; elapsed.start(); session.stop();
    QVERIFY(elapsed.elapsed() < 100);
    QTRY_COMPARE_WITH_TIMEOUT(session.state(), CameraSession::State::Disconnected, 5000);
  }
  void main_window_connects_and_embeds_liveview() {
    const auto path = temporary_.filePath("frame.jpg");
    QImage image(640, 480, QImage::Format_RGB32); image.fill(Qt::yellow); QVERIFY(image.save(path));
    qputenv("REMOTE_TEST_JPEG", path.toUtf8());
    const auto record = temporary_.filePath("ui-record.jsonl"); qputenv("REMOTE_TEST_RECORD", record.toUtf8());
    SettingsStore store; auto settings = store.load(); settings.sdk_terms_accepted = true;
    settings.folder = temporary_.filePath("ui-photos"); QVERIFY(store.save(settings));
    MainWindow window(FAKE_BACKEND_PATH); window.show();
    auto* connect_button = window.findChild<QPushButton*>("connect"); QVERIFY(connect_button);
    QTest::mouseClick(connect_button, Qt::LeftButton);
    auto* state = window.findChild<QLabel*>("connection_state"); QVERIFY(state);
    QTRY_VERIFY(state->text().startsWith("Подключено"));
    auto* live = window.findChild<ImageViewer*>("live_view"); QVERIFY(live); QVERIFY(!live->isWindow());
    QTRY_VERIFY(!live->view()->scene()->itemsBoundingRect().isEmpty());
    QTest::qWait(80);
    if (qEnvironmentVariableIsSet("REMOTE_TEST_SCREENSHOT")) window.grab().save(qEnvironmentVariable("REMOTE_TEST_SCREENSHOT"));
    QFile file(record); QVERIFY(file.open(QIODevice::ReadOnly)); const auto data = file.readAll();
    QVERIFY(data.contains("sync off")); QVERIFY(!data.contains("sync on")); QVERIFY(data.contains("monitor start"));
    QTest::mouseClick(connect_button, Qt::LeftButton);
    QTRY_COMPARE(state->text(), QString("Отключено"));
    window.close();
  }
  void startup_connection_data() {
    QTest::addColumn<bool>("enabled"); QTest::addColumn<bool>("remember"); QTest::addColumn<bool>("terms");
    QTest::addColumn<QString>("user"); QTest::addColumn<QString>("password"); QTest::addColumn<QString>("host"); QTest::addColumn<bool>("expected");
    QTest::newRow("saved") << true << true << true << QString("test-user") << QString("test-secret") << QString("127.0.0.1") << true;
    QTest::newRow("disabled") << false << true << true << QString("test-user") << QString("test-secret") << QString("127.0.0.1") << false;
    QTest::newRow("not-saved") << true << false << true << QString("test-user") << QString("test-secret") << QString("127.0.0.1") << false;
    QTest::newRow("terms") << true << true << false << QString("test-user") << QString("test-secret") << QString("127.0.0.1") << false;
    QTest::newRow("empty-user") << true << true << true << QString() << QString("test-secret") << QString("127.0.0.1") << false;
    QTest::newRow("empty-password") << true << true << true << QString("test-user") << QString() << QString("127.0.0.1") << false;
    QTest::newRow("invalid-ip") << true << true << true << QString("test-user") << QString("test-secret") << QString("bad host") << false;
  }
  void startup_connection() {
    QFETCH(bool, enabled); QFETCH(bool, remember); QFETCH(bool, terms); QFETCH(QString, user); QFETCH(QString, password); QFETCH(QString, host); QFETCH(bool, expected);
    const auto record = temporary_.filePath("startup.jsonl"); QFile::remove(record); qputenv("REMOTE_TEST_RECORD", record.toUtf8());
    AppSettings settings; settings.auto_connect = enabled; settings.remember_credentials = remember; settings.sdk_terms_accepted = terms;
    settings.user = user; settings.password = password; settings.host = host; settings.folder = temporary_.filePath("startup-photos");
    QVERIFY(SettingsStore().save(settings)); QCOMPARE(SettingsStore().load().auto_connect, enabled);
    MainWindow window(FAKE_BACKEND_PATH); window.show(); auto* session = window.findChild<CameraSession*>(); QVERIFY(session);
    auto* button = window.findChild<QPushButton*>("connect"); QVERIFY(button->height() >= 38);
    if (expected) {
      QTRY_COMPARE(session->state(), CameraSession::State::Connected);
      QFile file(record); QVERIFY(file.open(QIODevice::ReadOnly)); const auto data = file.readAll();
      QCOMPARE(data.count("\"argv\""), 1); QVERIFY(data.contains("sync off")); QVERIFY(!data.contains("test-secret"));
      button->click(); QTRY_COMPARE(session->state(), CameraSession::State::Disconnected); QTest::qWait(350); QVERIFY(!session->active());
    } else {
      QTest::qWait(400); QVERIFY(!session->active()); QVERIFY(!QFileInfo::exists(record)); QVERIFY(!QApplication::activeModalWidget());
    }
    window.close();
  }
  void startup_can_be_canceled_before_launch_and_on_close() {
    AppSettings settings; settings.remember_credentials = settings.sdk_terms_accepted = true;
    settings.user = "test-user"; settings.password = "test-secret"; settings.folder = temporary_.filePath("canceled-startup");
    QVERIFY(SettingsStore().save(settings));
    { MainWindow window(FAKE_BACKEND_PATH); window.show(); window.findChild<QCheckBox*>("auto_connect")->setChecked(false);
      QTest::qWait(400); QVERIFY(!window.findChild<CameraSession*>()->active()); window.close(); }
    QVERIFY(!SettingsStore().load().auto_connect); settings.auto_connect = true; QVERIFY(SettingsStore().save(settings));
    { MainWindow window(FAKE_BACKEND_PATH); window.show(); window.close(); QTest::qWait(400); QVERIFY(!window.findChild<CameraSession*>()->active()); }
    { MainWindow window(FAKE_BACKEND_PATH); window.show(); auto* button = window.findChild<QPushButton*>("connect");
      button->click(); QTRY_COMPARE(window.findChild<CameraSession*>()->state(), CameraSession::State::Connected);
      button->click(); QTRY_COMPARE(window.findChild<CameraSession*>()->state(), CameraSession::State::Disconnected);
      QTest::qWait(400); QVERIFY(!window.findChild<CameraSession*>()->active()); window.close(); }
  }
};
QTEST_MAIN(RemoteTests)
#include "test_remote.moc"
