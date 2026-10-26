#include "camera_card.h"
#include "camera_card_widget.h"
#include "gallery_widget.h"
#include "image_viewer.h"
#include "main_window.h"
#include "settings_store.h"
#include <QTabWidget>
#include "photo_library.h"
#include "video_preview.h"
#include <QComboBox>
#include <QAction>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListView>
#include <QLineEdit>
#include <QLocalSocket>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTimer>
#include <QtTest>

class VideoTests final : public QObject {
  Q_OBJECT
 private:
  QTemporaryDir temporary_;
  QString mp4_, hevc_, jpeg_, fixture_, record_;
  void generate(const QStringList& args) {
    QProcess ffmpeg; ffmpeg.start("ffmpeg", QStringList{"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=12", "-t", "2", "-an", "-threads", "1"} + args);
    QVERIFY(ffmpeg.waitForFinished(20000)); QVERIFY2(ffmpeg.exitCode() == 0, ffmpeg.readAllStandardError().constData());
  }
  QByteArray commands() { QFile file(record_); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
  void connect_card(CameraSession& session, CameraCard& card, const QString& root) {
    connect(&session, &CameraSession::state_changed, &card, [&card](auto state, const QString&) { card.set_connected(state == CameraSession::State::Connected); });
    card.set_directory(root); session.start({FAKE_BACKEND_PATH, "127.0.0.1", "a6700", {}, {}, root, false});
  }
 private slots:
  void initTestCase() {
    QVERIFY(temporary_.isValid()); qputenv("XDG_CONFIG_HOME", temporary_.filePath("config").toUtf8());
    mp4_ = temporary_.filePath("C0001.MP4"); hevc_ = temporary_.filePath("stream.hevc"); jpeg_ = temporary_.filePath("poster.jpg");
    generate({"-c:v", "libx264", "-preset", "ultrafast", mp4_});
    generate({"-c:v", "libx265", "-preset", "ultrafast", "-x265-params", "pools=1:frame-threads=1:log-level=error", "-f", "hevc", hevc_});
    QImage image(160, 90, QImage::Format_RGB32); image.fill(Qt::green); QVERIFY(image.save(jpeg_));
    const auto xml = temporary_.filePath("C0001M01.XML"); QFile metadata(xml); QVERIFY(metadata.open(QIODevice::WriteOnly)); metadata.write("<clip/>"); metadata.close();
    auto file = [](const QString& path, int id, int format, const QString& remote) {
      return QJsonObject{{"id", id}, {"format", format}, {"size", QString::number(QFileInfo(path).size())}, {"path", remote}, {"source", path}};
    };
    QJsonObject movie{{"slot", 1}, {"id", 8}, {"stamp", "2026-10-23T00:30:00"}, {"capture_date", "2026-10-22"}, {"identity", "movie-1"},
        {"protected", false}, {"deletable", true}, {"files", QJsonArray{file(mp4_, 1, 0xb982, "PRIVATE/M4ROOT/CLIP/C0001.MP4"), file(xml, 2, 0xba82, "PRIVATE/M4ROOT/CLIP/C0001M01.XML")}}};
    QJsonObject photo{{"slot", 1}, {"id", 9}, {"stamp", "2026-10-21T10:00:00"}, {"identity", "photo-1"},
        {"protected", false}, {"deletable", true}, {"files", QJsonArray{file(jpeg_, 1, 0x3801, "DCIM/100MSDCF/DSC0001.JPG")}}};
    fixture_ = temporary_.filePath("catalog.json"); QFile fixture(fixture_); QVERIFY(fixture.open(QIODevice::WriteOnly)); fixture.write(QJsonDocument(QJsonArray{movie, photo}).toJson());
    record_ = temporary_.filePath("commands.jsonl");
  }
  void init() {
    QFile::remove(record_); qputenv("REMOTE_TEST_RECORD", record_.toUtf8()); qputenv("REMOTE_TEST_CARD", fixture_.toUtf8());
    qputenv("REMOTE_TEST_CAPTURE", jpeg_.toUtf8()); qputenv("REMOTE_TEST_JPEG", jpeg_.toUtf8()); qputenv("REMOTE_TEST_HEVC", hevc_.toUtf8());
  }
  void cleanup() {
    for (auto key : {"REMOTE_TEST_RECORD", "REMOTE_TEST_CARD", "REMOTE_TEST_CAPTURE", "REMOTE_TEST_HEVC", "REMOTE_TEST_JPEG"}) qunsetenv(key);
  }
  void local_video_poster_and_distinct_gallery_entry() {
    const auto result = MediaReader::read(mp4_); QVERIFY(result.video); QVERIFY2(!result.image.isNull(), qPrintable(result.error));
    QVERIFY(result.image.width() <= 1280); QVERIFY(result.source.contains(QString::fromUtf8("первый кадр")));
    QTemporaryDir root; QVERIFY(QFile::copy(mp4_, root.filePath("C0001.MP4"))); QVERIFY(QFile::copy(jpeg_, root.filePath("C0001.JPG")));
    PhotoLibrary library; library.set_auto_preview(false); QSignalSpy items(&library, &PhotoLibrary::items_changed); library.set_directory(root.path());
    QTRY_VERIFY(items.size() > 1); QTRY_COMPARE(qvariant_cast<MediaItems>(items.last()[0]).size(), 2);
    const auto collection = qvariant_cast<MediaItems>(items.last()[0]);
    QVERIFY(collection[0].key != collection[1].key); QVERIFY(collection[0].format() == "MP4" || collection[1].format() == "MP4");
    GalleryWidget gallery; gallery.set_items(collection); gallery.show_photo(result);
    QVERIFY(!gallery.viewer()->findChild<QWidget*>("movie_controls")->isHidden());
    auto* forward = gallery.viewer()->findChild<QPushButton*>("movie_forward");
    QTRY_VERIFY(forward->isEnabled()); QSignalSpy frames(gallery.viewer()->findChild<VideoPreview*>(), &VideoPreview::frame_ready);
    forward->click(); QTRY_VERIFY(!frames.isEmpty()); gallery.viewer()->stop_video();
  }
  void ffmpeg_local_playback_pause_and_stop() {
    VideoPreview player; QSignalSpy frames(&player, &VideoPreview::frame_ready), errors(&player, &VideoPreview::failed);
    player.play_local(mp4_); QTRY_VERIFY(frames.size() >= 2); player.pause_local(true); QVERIFY(player.running());
    player.pause_local(false); QTRY_VERIFY(frames.size() >= 3); player.stop();
    const auto before = frames.size(); QTest::qWait(100); QCOMPARE(frames.size(), before); QCOMPARE(errors.size(), 0);
  }
  void local_video_keeps_resolution_frame_rate_and_decoded_pixels() {
    const auto path = temporary_.filePath("native.mp4");
    {
      QProcess encoder;
      encoder.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=1920x1080:rate=24", "-t", "1", "-an", "-threads", "1", "-c:v", "libx264", "-preset", "ultrafast", path});
      QVERIFY(encoder.waitForFinished(20000)); QCOMPARE(encoder.exitCode(), 0);
    }
    QByteArray rgb;
    {
      QProcess reference;
      reference.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-threads", "2", "-i", path, "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-threads", "1", "pipe:1"});
      QVERIFY(reference.waitForFinished(10000)); QCOMPARE(reference.exitCode(), 0);
      rgb = reference.readAllStandardOutput(); QCOMPARE(rgb.size(), 1920 * 1080 * 3);
    }
    VideoPreview player; QSignalSpy done(&player, &VideoPreview::finished), errors(&player, &VideoPreview::failed);
    int count = 0; QImage first; bool native = true;
    connect(&player, &VideoPreview::frame_ready, this, [&](const QImage& image) {
      if (!count) first = image;
      ++count; native = native && image.size() == QSize(1920, 1080);
    });
    QElapsedTimer elapsed; elapsed.start();
    // Непрерывный цикл, как в приложении: QTRY/qWait делает паузы между
    // readyRead и может искусственно затормозить 142 MiB RGB через pipe.
    QEventLoop playback;
    QTimer deadline; deadline.setSingleShot(true);
    connect(&player, &VideoPreview::finished, &playback, &QEventLoop::quit);
    connect(&player, &VideoPreview::failed, &playback, &QEventLoop::quit);
    connect(&deadline, &QTimer::timeout, &playback, &QEventLoop::quit);
    deadline.start(10000); player.play_local(path); playback.exec();
    QVERIFY2(!done.isEmpty() || !errors.isEmpty(), qPrintable(QString("frames=%1; decoder_running=%2").arg(count).arg(player.running())));
    QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first()[0].toString())); QCOMPARE(done.size(), 1);
    QVERIFY2(elapsed.elapsed() >= 850, "Playback must follow presentation time, rather than burst all frames during input preroll");
    QCOMPARE(errors.size(), 0); QVERIFY(native); QCOMPARE(count, 24);
    QCOMPARE(first.format(), QImage::Format_RGB888);
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(first.constBits()), first.sizeInBytes()), rgb);
  }
  void five_second_buttons_follow_playback_preserve_pause_and_clamp() {
    const auto path = temporary_.filePath("seek.mp4");
    QProcess encoder;
    encoder.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=10", "-t", "13", "-an", "-threads", "1", "-c:v", "libx264", "-preset", "ultrafast", "-g", "1", path});
    QVERIFY(encoder.waitForFinished(10000)); QCOMPARE(encoder.exitCode(), 0);
    ImageViewer viewer; viewer.set_local_video(path); viewer.show(); viewer.toggle_fullscreen(); QTest::qWait(20);
    auto* player = viewer.findChild<VideoPreview*>(); QSignalSpy frames(player, &VideoPreview::frame_ready), errors(player, &VideoPreview::failed);
    auto* backward = viewer.findChild<QPushButton*>("movie_backward"); auto* forward = viewer.findChild<QPushButton*>("movie_forward");
    QTRY_COMPARE(player->duration_ms(), qint64(13000)); QVERIFY(backward->isVisible()); QVERIFY(forward->isEnabled());
    viewer.play_video(); QTRY_VERIFY(player->position_ms() >= 1100);
    viewer.findChild<QPushButton*>("movie_pause")->click(); QVERIFY(player->paused());
    const auto paused_position = player->position_ms(); QVERIFY(paused_position < 4000);
    const auto before = frames.size(); forward->click();
    QCOMPARE(player->position_ms(), paused_position + 5000); QTRY_COMPARE(frames.size(), before + 1); QVERIFY(player->paused());
    const auto image = qvariant_cast<QImage>(frames.last()[0]); QCOMPARE(image.size(), QSize(160, 90));
    QProcess reference;
    reference.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-ss", QString::number((paused_position + 5000) / 1000.0, 'f', 3), "-i", path, "-frames:v", "1", "-pix_fmt", "rgb24", "-threads", "1", "-f", "rawvideo", "pipe:1"});
    QVERIFY(reference.waitForFinished(10000)); QCOMPARE(reference.exitCode(), 0);
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes()), reference.readAllStandardOutput());
    QTest::qWait(150); QCOMPARE(frames.size(), before + 1); QCOMPARE(player->position_ms(), paused_position + 5000);
    backward->click(); QCOMPARE(player->position_ms(), paused_position); QTRY_COMPARE(frames.size(), before + 2);
    backward->click(); QCOMPARE(player->position_ms(), qint64(0)); QTRY_COMPARE(frames.size(), before + 3);
    // Repeated clicks before the first new frame accumulate from the requested position.
    forward->click(); forward->click(); QCOMPARE(player->position_ms(), qint64(10000)); QTRY_COMPARE(frames.size(), before + 4);
    forward->click(); QCOMPARE(player->position_ms(), qint64(12900)); QTRY_COMPARE(frames.size(), before + 5);
    QCOMPARE(errors.size(), 0); QVERIFY(player->paused());
    if (qEnvironmentVariableIsSet("REMOTE_TEST_SCREENSHOTS")) viewer.grab().save(qEnvironmentVariable("REMOTE_TEST_SCREENSHOTS") + "/video-seek-fullscreen.png");
    viewer.findChild<QPushButton*>("movie_stop")->click(); QVERIFY(!player->running()); QCOMPARE(player->position_ms(), qint64(0));
    viewer.leave_fullscreen(); viewer.set_local_video({}); QTest::qWait(50); QCOMPARE(player->duration_ms(), qint64(0));
  }
  void invalid_local_video_reports_decoder_error() {
    const auto path = temporary_.filePath("broken.mp4"); QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write("not a video"); file.close();
    VideoPreview player; QSignalSpy errors(&player, &VideoPreview::failed);
    player.play_local(path); QTRY_COMPARE(errors.size(), 1); QVERIFY(!player.running());
  }
  void h265_stream_decodes_and_drains_on_socket_end() {
    VideoPreview player; QSignalSpy frames(&player, &VideoPreview::frame_ready), errors(&player, &VideoPreview::failed), done(&player, &VideoPreview::finished);
    const auto socket = temporary_.filePath("test.sock"); QVERIFY(player.listen(socket));
    QLocalSocket sender; sender.connectToServer(socket); QVERIFY(sender.waitForConnected(2000));
    QFile source(hevc_); QVERIFY(source.open(QIODevice::ReadOnly)); const auto bytes = source.readAll();
    QCOMPARE(sender.write(bytes), qint64(bytes.size()));
    QTRY_VERIFY(frames.size() > 0); QCOMPARE(done.size(), 0); // Start before EOF, including short clips.
    sender.disconnectFromServer(); QTRY_COMPARE(done.size(), 1); QCOMPARE(errors.size(), 0); QCOMPARE(frames.size(), 24);
  }
  void empty_camera_stream_does_not_start_decoder_or_mask_network_error() {
    VideoPreview player; QSignalSpy errors(&player, &VideoPreview::failed), frames(&player, &VideoPreview::frame_ready);
    const auto path = temporary_.filePath("empty.sock"); QVERIFY(player.listen(path)); QVERIFY(!player.running());
    QLocalSocket sender; sender.connectToServer(path); QVERIFY(sender.waitForConnected(2000));
    QTest::qWait(50); sender.disconnectFromServer(); QTest::qWait(100);
    QVERIFY(!player.running()); QCOMPARE(errors.size(), 0); QCOMPARE(frames.size(), 0); player.stop();
  }
  void main_window_stops_liveview_for_movie_and_restores_it_on_tab_change() {
    QTemporaryDir root; AppSettings settings; settings.card_original_video = false; settings.host = "127.0.0.1"; settings.folder = root.path(); settings.sdk_terms_accepted = true;
    QVERIFY(SettingsStore().save(settings));
    MainWindow window(FAKE_BACKEND_PATH); window.show(); window.activateWindow();
    window.findChild<QPushButton*>("connect")->click();
    auto* session = window.findChild<CameraSession*>(); auto* card = window.findChild<CameraCard*>();
    QTRY_COMPARE(session->state(), CameraSession::State::Connected);
    auto* tabs = window.findChild<QTabWidget*>("main_tabs"); tabs->setCurrentIndex(1);
    auto* list = window.findChild<QTreeWidget*>("card_items"); QTRY_COMPARE(list->topLevelItemCount(), 2);
    list->setCurrentItem(list->topLevelItem(0)); QTRY_VERIFY(!card->busy());
    auto* status_path = window.findChild<QLineEdit*>("file_status_path"); QVERIFY(status_path);
    QCOMPARE(status_path->text(), QString("PRIVATE/M4ROOT/CLIP/C0001.MP4"));
    QVERIFY(status_path->isReadOnly());
    auto* viewer = window.findChild<ImageViewer*>("card_preview");
    QSignalSpy frames(viewer->findChild<VideoPreview*>(), &VideoPreview::frame_ready);
    viewer->findChild<QPushButton*>("movie_play")->click(); QTRY_VERIFY(frames.size() > 0);
    QVERIFY(!window.findChild<QPushButton*>("command_shoot")->isEnabled());
    tabs->setCurrentIndex(0); QTRY_VERIFY(!card->playing_video());
    QTRY_VERIFY(window.findChild<QPushButton*>("command_shoot")->isEnabled());
    QTRY_VERIFY(commands().count("monitor start") >= 2);
    window.close();
  }
  void original_playback_survives_settings_and_stops_on_tab_change() {
    QTemporaryDir root; AppSettings settings; settings.auto_connect = false; settings.host = "127.0.0.1";
    settings.folder = root.path(); settings.sdk_terms_accepted = true; QVERIFY(SettingsStore().save(settings));
    MainWindow window(FAKE_BACKEND_PATH); window.show(); window.findChild<QPushButton*>("connect")->click();
    auto* session = window.findChild<CameraSession*>(); auto* card = window.findChild<CameraCard*>();
    QTRY_COMPARE(session->state(), CameraSession::State::Connected);
    auto* tabs = window.findChild<QTabWidget*>("main_tabs"); tabs->setCurrentIndex(1);
    auto* list = window.findChild<QTreeWidget*>("card_items"); QTRY_COMPARE(list->topLevelItemCount(), 2);
    list->setCurrentItem(list->topLevelItem(0)); QTRY_VERIFY(!card->busy());
    auto* viewer = window.findChild<ImageViewer*>("card_preview"); auto* player = viewer->findChild<VideoPreview*>();
    QSignalSpy frames(player, &VideoPreview::frame_ready); viewer->findChild<QPushButton*>("movie_play")->click();
    QTRY_VERIFY(!frames.isEmpty()); viewer->findChild<QPushButton*>("movie_pause")->click(); QVERIFY(player->running());
    QSignalSpy catalog(card, &CameraCard::catalog_changed);
    window.findChild<QAction*>("settings_preview")->trigger(); window.findChild<QDialog*>("settings_dialog")->reject();
    QVERIFY(player->running()); QCOMPARE(catalog.size(), 0);
    QCOMPARE(list->currentItem()->data(0, Qt::UserRole).toString(), QString("1:8"));
    QCOMPARE(commands().count(" original"), 1); QVERIFY(!commands().contains("card movie"));
    QVERIFY(window.findChild<QPushButton*>("command_shoot")->isEnabled());
    tabs->setCurrentIndex(0); QVERIFY(!player->running()); window.close();
  }
  void video_navigation_buttons_work_in_gallery_and_separate_fullscreen() {
    QTemporaryDir root; QVERIFY(QFile::copy(mp4_, root.filePath("first.mp4"))); QVERIFY(QFile::copy(mp4_, root.filePath("second.mp4")));
    AppSettings settings; settings.auto_connect = false; settings.folder = root.path(); QVERIFY(SettingsStore().save(settings));
    MainWindow window(FAKE_BACKEND_PATH); window.show(); window.activateWindow(); window.findChild<QTabWidget*>("main_tabs")->setCurrentIndex(2);
    auto* gallery = window.findChild<GalleryWidget*>(); auto* list = gallery->findChild<QListView*>("gallery_list");
    QSignalSpy loaded(window.findChild<PhotoLibrary*>(), &PhotoLibrary::photo_loaded);
    QTRY_COMPARE(list->model()->rowCount(), 2);
    list->setCurrentIndex(list->model()->index(1, 0)); list->setCurrentIndex(list->model()->index(0, 0));
    const auto first = list->currentIndex().data(GalleryModel::PathRole).toString();
    QTRY_VERIFY(!loaded.isEmpty() && qvariant_cast<MediaResult>(loaded.last()[0]).path == first);
    auto* viewer = gallery->viewer(); viewer->findChild<QPushButton*>("viewer_next")->click();
    QTRY_COMPARE(list->currentIndex().row(), 1); const auto next = list->currentIndex().data(GalleryModel::PathRole).toString();
    QTRY_VERIFY(qvariant_cast<MediaResult>(loaded.last()[0]).path == next);
    viewer->findChild<QPushButton*>("viewer_previous")->click(); QTRY_COMPARE(list->currentIndex().row(), 0);
    QTRY_VERIFY(qvariant_cast<MediaResult>(loaded.last()[0]).path == first);
    gallery->findChild<QPushButton*>("open_photo_window")->click(); QTRY_VERIFY(window.findChild<ImageViewer*>("photo_window"));
    auto* separate = window.findChild<ImageViewer*>("photo_window"); separate->toggle_fullscreen(); QTest::qWait(30);
    auto* player = separate->findChild<VideoPreview*>(); QSignalSpy frames(player, &VideoPreview::frame_ready);
    separate->findChild<QPushButton*>("movie_play")->click(); QTRY_VERIFY(!frames.isEmpty());
    separate->findChild<QPushButton*>("movie_pause")->click(); QVERIFY(player->running());
    auto* seek_forward = separate->findChild<QPushButton*>("movie_forward"); QTRY_VERIFY(seek_forward->isEnabled()); QVERIFY(seek_forward->isVisible());
    const auto before_seek = frames.size(); seek_forward->click(); QTRY_VERIFY(frames.size() > before_seek); QVERIFY(player->paused());
    if (qEnvironmentVariableIsSet("REMOTE_TEST_SCREENSHOTS")) separate->grab().save(qEnvironmentVariable("REMOTE_TEST_SCREENSHOTS") + "/video-viewer-fullscreen.png");
    auto* forward = separate->findChild<QPushButton*>("viewer_next"); QVERIFY(forward->isVisible()); forward->click();
    QTRY_COMPARE(list->currentIndex().row(), 1); QTRY_VERIFY(qvariant_cast<MediaResult>(loaded.last()[0]).path == next);
    QTRY_VERIFY(!player->running()); QVERIFY(!separate->findChild<QWidget*>("movie_controls")->isHidden());
    separate->findChild<QPushButton*>("viewer_previous")->click(); QTRY_COMPARE(list->currentIndex().row(), 0);
    QTRY_VERIFY(qvariant_cast<MediaResult>(loaded.last()[0]).path == first);
    window.close();
  }
  void local_file_status_contains_absolute_path_and_survives_tab_changes() {
    QTemporaryDir root; const auto path = root.filePath("2026-10-22/Slot_1/CLIP/C0001.MP4");
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath())); QVERIFY(QFile::copy(mp4_, path));
    AppSettings settings; settings.folder = root.path(); QVERIFY(SettingsStore().save(settings));
    MainWindow window(FAKE_BACKEND_PATH); window.show();
    auto* tabs = window.findChild<QTabWidget*>("main_tabs"); tabs->setCurrentIndex(2);
    auto* status_path = window.findChild<QLineEdit*>("file_status_path"); QVERIFY(status_path);
    QTRY_COMPARE(status_path->text(), path); QCOMPARE(status_path->toolTip(), path);
    tabs->setCurrentIndex(0); QCOMPARE(status_path->text(), QString());
    tabs->setCurrentIndex(2); QCOMPARE(status_path->text(), path);
    window.close();
  }
  void remote_video_filter_preview_marks_copy_and_date_ledger() {
    QTemporaryDir root; CameraSession session; CameraCard card(&session); connect_card(session, card, root.path());
    QTRY_COMPARE(session.state(), CameraSession::State::Connected); card.refresh(); QTRY_VERIFY(!card.busy()); QCOMPARE(card.items().size(), 2);
    QVERIFY(card.items().first().is_video()); QCOMPARE(card.items().first().format(), QString("MP4+XML"));
    CameraCardWidget widget(&card); widget.resize(1200, 760); widget.show(); widget.activateWindow();
    auto* list = widget.findChild<QTreeWidget*>("card_items"); auto* viewer = widget.findChild<ImageViewer*>("card_preview");
    widget.findChild<QComboBox*>("card_video_quality")->setCurrentIndex(1);
    auto* format = widget.findChild<QComboBox*>("card_format"); format->setCurrentIndex(format->findData("video"));
    QVERIFY(!list->topLevelItem(0)->isHidden()); QVERIFY(list->topLevelItem(1)->isHidden());
    list->setCurrentItem(list->topLevelItem(0)); QTRY_VERIFY(!card.busy());
    QVERIFY(!commands().contains("card copy")); QVERIFY(!viewer->findChild<QWidget*>("movie_controls")->isHidden());
    viewer->toggle_fullscreen(); QTest::qWait(30); QTest::keyClick(viewer->view(), Qt::Key_Space); QTest::keyClick(viewer->view(), Qt::Key_Delete);
    QCOMPARE(list->currentItem()->checkState(0), Qt::Checked); QCOMPARE(list->currentItem()->checkState(1), Qt::Checked);
    QVERIFY(viewer->findChild<QLabel*>("action_overlay")->isVisible());
    QSignalSpy frames(viewer->findChild<VideoPreview*>(), &VideoPreview::frame_ready);
    QTest::mouseClick(viewer->findChild<QPushButton*>("movie_play"), Qt::LeftButton); QTRY_VERIFY(frames.size() > 0);
    QVERIFY(viewer->findChild<QLabel*>("movie_info")->text().contains("160 × 90"));
    auto* player = viewer->findChild<VideoPreview*>(); QSignalSpy seeks(viewer, &ImageViewer::movie_seek_requested);
    auto* seek_forward = viewer->findChild<QPushButton*>("movie_forward"); QTRY_VERIFY(seek_forward->isEnabled());
    const int target = int(player->position_ms() / 1000) + 5; seek_forward->click();
    QCOMPARE(seeks.size(), 1); QCOMPARE(seeks.first()[0].toInt(), target); QTRY_VERIFY(commands().contains("card movie-seek"));
    viewer->findChild<QPushButton*>("movie_backward")->click(); QCOMPARE(seeks.size(), 2);
    viewer->view()->setFocus(); QTest::keyClick(viewer->view(), Qt::Key_Space);
    QVERIFY(viewer->findChild<QLabel*>("action_overlay")->text().contains(QString::fromUtf8("Отметка копирования снята")));
    if (qEnvironmentVariableIsSet("REMOTE_TEST_SCREENSHOTS")) viewer->grab().save(qEnvironmentVariable("REMOTE_TEST_SCREENSHOTS") + "/video-overlay.png");
    QTest::keyClick(viewer->view(), Qt::Key_Space);
    QVERIFY(!commands().contains("card copy"));
    if (card.playing_video()) { QTest::mouseClick(viewer->findChild<QPushButton*>("movie_pause"), Qt::LeftButton); QTRY_VERIFY(commands().contains("card movie-pause")); }
    QTest::mouseClick(viewer->findChild<QPushButton*>("movie_stop"), Qt::LeftButton); QTRY_VERIFY(!card.busy());
    viewer->leave_fullscreen();
    QSignalSpy copied(&card, &CameraCard::copied); card.copy({"1:8"}, "video", CameraCard::CopyMode::Selected); QTRY_VERIFY(!card.busy()); QCOMPARE(copied.size(), 2);
    const auto path = root.filePath("2026-10-22/Slot_1/PRIVATE/M4ROOT/CLIP/C0001.MP4"); QVERIFY(QFileInfo::exists(path));
    QVERIFY(!card.is_new(card.items().first(), "video")); card.copy({}, "video", CameraCard::CopyMode::New); QTest::qWait(50); QCOMPARE(copied.size(), 2);
    // A legacy journal entry under the old Slot_N layout remains valid.
    const auto old = root.filePath("Slot_1/C0001.MP4"); QVERIFY(QDir().mkpath(QFileInfo(old).absolutePath())); QVERIFY(QFile::rename(path, old));
    QFile ledger(root.filePath(".sonshell-imports.json")); QVERIFY(ledger.open(QIODevice::ReadOnly)); auto entries = QJsonDocument::fromJson(ledger.readAll()).object(); ledger.close();
    for (auto i = entries.begin(); i != entries.end(); ++i) { auto entry = i.value().toObject(); if (entry.value("path") == path) { entry["path"] = old; i.value() = entry; } }
    QVERIFY(ledger.open(QIODevice::WriteOnly | QIODevice::Truncate)); ledger.write(QJsonDocument(entries).toJson()); ledger.close();
    card.set_directory(root.path()); QVERIFY(!card.is_new(card.items().first(), "video"));
    widget.close(); session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
};
QTEST_MAIN(VideoTests)
#include "test_video.moc"
