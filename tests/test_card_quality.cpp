#include "camera_card.h"
#include "camera_card_widget.h"
#include "image_viewer.h"
#include "video_preview.h"
#include <QComboBox>
#include <QDataStream>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

class CardQualityTests final : public QObject {
  Q_OBJECT
 private:
  QTemporaryDir temporary_;
  QString full_, small_, raw_, movie_, fixture_, record_;
  QJsonObject file(const QString& source, int id, int format, const QString& path) {
    return {{"id", id}, {"format", format}, {"size", QString::number(QFileInfo(source).size())}, {"path", path}, {"source", source}};
  }
  QJsonObject item(int id, const QJsonArray& files) {
    return {{"slot", 1}, {"id", id}, {"stamp", QString("2026-10-24T12:00:0%1").arg(id)},
        {"identity", QString("quality-%1").arg(id)}, {"deletable", true}, {"files", files}};
  }
  QByteArray commands() { QFile file(record_); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
  void connect_card(CameraSession& session, CameraCard& card, const QString& folder) {
    connect(&session, &CameraSession::state_changed, &card, [&card](auto state, const QString&) { card.set_connected(state == CameraSession::State::Connected); });
    card.set_directory(folder); session.start({FAKE_BACKEND_PATH, "127.0.0.1", "a6700", {}, {}, folder, false});
    QTRY_COMPARE(session.state(), CameraSession::State::Connected); card.refresh(); QTRY_VERIFY(!card.busy());
  }
  QTreeWidgetItem* row(CameraCardWidget& widget, int id) {
    auto* list = widget.findChild<QTreeWidget*>("card_items");
    for (int i = 0; i < list->topLevelItemCount(); ++i) if (list->topLevelItem(i)->data(0, Qt::UserRole) == QString("1:%1").arg(id)) return list->topLevelItem(i);
    return nullptr;
  }
 private slots:
  void initTestCase() {
    QVERIFY(temporary_.isValid());
    full_ = temporary_.filePath("full.jpg"); small_ = temporary_.filePath("small.jpg");
    QImage image(6192, 4128, QImage::Format_RGB32); image.fill(QColor("#3175a2")); QVERIFY(image.save(full_));
    image = QImage(320, 180, QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(small_));
    // Minimal TIFF/DNG with an embedded JPEG; ExifTool identifies its contents,
    // while the transfer and reader use the Sony RAW file extension.
    image = QImage(600, 400, QImage::Format_RGB32); image.fill(Qt::green);
    const auto embedded = temporary_.filePath("embedded.jpg"); QVERIFY(image.save(embedded));
    QFile input(embedded); QVERIFY(input.open(QIODevice::ReadOnly)); const auto bytes = input.readAll();
    QByteArray raw; QDataStream out(&raw, QIODevice::WriteOnly); out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("II", 2); out << quint16(42) << quint32(8) << quint16(8);
    auto tag = [&out](int code, int type, int count, quint32 value) { out << quint16(code) << quint16(type) << quint32(count) << value; };
    tag(0x100, 4, 1, 6000); tag(0x101, 4, 1, 4000); tag(0x10f, 2, 5, 110); tag(0x110, 2, 10, 115);
    tag(0x112, 3, 1, 1); tag(0x14a, 4, 1, 128); tag(0xc612, 1, 4, 0x00000401); tag(0xc614, 2, 10, 115);
    out << quint32(0); out.writeRawData("SONY\0ILCE-6700\0", 15); while (raw.size() < 128) out << quint8(0);
    out << quint16(5); tag(0x100, 4, 1, 600); tag(0x101, 4, 1, 400); tag(0x103, 3, 1, 6);
    tag(0x201, 4, 1, 194); tag(0x202, 4, 1, bytes.size()); out << quint32(0); out.writeRawData(bytes.constData(), bytes.size());
    raw_ = temporary_.filePath("raw.arw"); QFile output(raw_); QVERIFY(output.open(QIODevice::WriteOnly)); output.write(raw); output.close();
    movie_ = temporary_.filePath("movie.mp4"); QProcess encoder;
    encoder.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=1920x1080:rate=24", "-t", "1", "-an", "-threads", "1", "-c:v", "libx264", "-preset", "ultrafast", movie_});
    QVERIFY(encoder.waitForFinished(20000)); QCOMPARE(encoder.exitCode(), 0);
    QJsonArray items{item(1, {file(raw_, 2, 0xb101, "DCIM/one.ARW"), file(full_, 1, 0x3801, "DCIM/one.JPG")}),
        item(2, {file(raw_, 2, 0xb101, "DCIM/two.ARW")}), item(3, {file(movie_, 1, 0xb982, "PRIVATE/three.MP4")}),
        item(4, {file(small_, 1, 0x3801, "DCIM/four.JPG")})};
    fixture_ = temporary_.filePath("catalog.json"); QFile fixture(fixture_); QVERIFY(fixture.open(QIODevice::WriteOnly)); fixture.write(QJsonDocument(items).toJson());
    record_ = temporary_.filePath("commands.jsonl");
  }
  void init() {
    QFile::remove(record_); qputenv("REMOTE_TEST_CARD", fixture_.toUtf8()); qputenv("REMOTE_TEST_CAPTURE", small_.toUtf8()); qputenv("REMOTE_TEST_RECORD", record_.toUtf8());
  }
  void cleanup() {
    for (auto* name : {"REMOTE_TEST_CARD", "REMOTE_TEST_CAPTURE", "REMOTE_TEST_RECORD", "REMOTE_TEST_SLOW_ORIGINAL", "REMOTE_TEST_PREVIEW_DELAY"}) qunsetenv(name);
  }
  void jpeg_defaults_to_native_pixels_and_quick_mode_remains_explicit() {
    QTemporaryDir folder; CameraSession session; CameraCard card(&session); connect_card(session, card, folder.path());
    CameraCardWidget widget(&card); widget.resize(1300, 750); widget.show();
    auto* list = widget.findChild<QTreeWidget*>(); auto* viewer = widget.findChild<ImageViewer*>();
    QSignalSpy ready(&card, &CameraCard::original_ready); list->setCurrentItem(row(widget, 1)); QTRY_COMPARE(ready.size(), 1); QTRY_VERIFY(!card.busy());
    QCOMPARE(viewer->view()->sceneRect().size(), QSizeF(6192, 4128)); viewer->view()->actual_size(); QCOMPARE(viewer->view()->zoom_factor(), 1.0);
    const auto result = qvariant_cast<MediaResult>(ready[0][1]); QCOMPARE(result.image.pixelColor(100, 100), QImage(full_).pixelColor(100, 100));
    QVERIFY(commands().contains(" 1 original")); QVERIFY(!commands().contains(" 2 original")); QVERIFY(!commands().contains("card copy"));
    QVERIFY(card.is_new(card.items().last(), "all")); QVERIFY(!card.has_imports()); QVERIFY(QDir(folder.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    viewer->toggle_fullscreen(); QVERIFY(widget.findChild<QComboBox*>("card_quality")->isVisible()); QVERIFY(widget.findChild<QPushButton*>("card_original")->isVisible());
    widget.findChild<QComboBox*>("card_quality")->setCurrentIndex(1); QTRY_VERIFY(!card.busy()); QCOMPARE(viewer->view()->sceneRect().size(), QSizeF(320, 180));
    viewer->leave_fullscreen(); session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected); QVERIFY(!QFileInfo::exists(result.path));
  }
  void raw_is_explicit_and_uses_best_embedded_jpeg() {
    QTemporaryDir folder; CameraSession session; CameraCard card(&session); connect_card(session, card, folder.path());
    CameraCardWidget widget(&card); widget.show(); auto* list = widget.findChild<QTreeWidget*>();
    list->setCurrentItem(row(widget, 2)); QTRY_VERIFY(!card.busy()); QVERIFY(!commands().contains("original"));
    QSignalSpy ready(&card, &CameraCard::original_ready); widget.findChild<QPushButton*>("card_original")->click(); QTRY_COMPARE(ready.size(), 1);
    const auto result = qvariant_cast<MediaResult>(ready[0][1]); QVERIFY(result.raw_preview); QCOMPARE(result.image.size(), QSize(600, 400));
    QVERIFY(result.source.contains(QString::fromUtf8("embedded"))); QVERIFY(result.path.endsWith(".arw")); QVERIFY(!card.has_imports());
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
  void video_original_plays_native_frames_and_cache_avoids_retransfer() {
    QTemporaryDir folder; CameraSession session; CameraCard card(&session); connect_card(session, card, folder.path());
    CameraCardWidget widget(&card); widget.show(); auto* list = widget.findChild<QTreeWidget*>(); auto* viewer = widget.findChild<ImageViewer*>();
    list->setCurrentItem(row(widget, 3)); QTRY_VERIFY(!card.busy()); QVERIFY(!commands().contains("original"));
    QSignalSpy ready(&card, &CameraCard::original_ready); widget.findChild<QPushButton*>("card_original")->click(); QTRY_COMPARE(ready.size(), 1); QTRY_VERIFY(!card.busy());
    const auto result = qvariant_cast<MediaResult>(ready[0][1]); QCOMPARE(result.image.size(), QSize(1920, 1080)); QVERIFY(result.video);
    QSignalSpy frames(viewer->findChild<VideoPreview*>(), &VideoPreview::frame_ready); viewer->findChild<QPushButton*>("movie_play")->click(); QTRY_VERIFY(frames.size() > 0);
    QCOMPARE(qvariant_cast<QImage>(frames[0][0]).size(), QSize(1920, 1080)); QVERIFY(!commands().contains("card movie"));
    viewer->findChild<QPushButton*>("movie_pause")->click(); QVERIFY(viewer->findChild<VideoPreview*>()->running());
    widget.deactivated(); QVERIFY(!viewer->findChild<VideoPreview*>()->running());
    widget.findChild<QPushButton*>("card_original")->click(); QTRY_COMPARE(ready.size(), 2); QTRY_VERIFY(!card.busy());
    QCOMPARE(commands().count(" original"), 1); QVERIFY(QFileInfo::exists(result.path)); QVERIFY(!card.has_imports());
    list->setCurrentItem(row(widget, 4)); QTRY_COMPARE(ready.size(), 3); QTRY_VERIFY(!card.busy()); QVERIFY(!QFileInfo::exists(result.path));
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
  void play_downloads_original_then_starts_and_reuses_cache_without_sdk_stream() {
    QTemporaryDir folder; CameraSession session; CameraCard card(&session); connect_card(session, card, folder.path());
    CameraCardWidget widget(&card); widget.show(); auto* list = widget.findChild<QTreeWidget*>(); auto* viewer = widget.findChild<ImageViewer*>();
    list->setCurrentItem(row(widget, 3)); QTRY_VERIFY(!card.busy()); QVERIFY(!commands().contains(" original"));
    QSignalSpy frames(viewer->findChild<VideoPreview*>(), &VideoPreview::frame_ready);
    viewer->findChild<QPushButton*>("movie_play")->click(); QTRY_VERIFY(frames.size() > 0); QTRY_VERIFY(!card.busy());
    QCOMPARE(qvariant_cast<QImage>(frames[0][0]).size(), QSize(1920, 1080));
    QCOMPARE(commands().count(" original"), 1); QVERIFY(!commands().contains("card movie")); QVERIFY(!card.has_imports());
    widget.deactivated(); frames.clear();
    viewer->findChild<QPushButton*>("movie_play")->click(); QTRY_VERIFY(frames.size() > 0);
    QCOMPARE(commands().count(" original"), 1); widget.deactivated();
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
  void canceled_play_download_never_autoplays_after_navigation() {
    qputenv("REMOTE_TEST_SLOW_ORIGINAL", "1");
    QTemporaryDir folder; CameraSession session; CameraCard card(&session); connect_card(session, card, folder.path());
    CameraCardWidget widget(&card); widget.show(); auto* list = widget.findChild<QTreeWidget*>(); auto* viewer = widget.findChild<ImageViewer*>();
    list->setCurrentItem(row(widget, 3)); QTRY_VERIFY(!card.busy());
    QSignalSpy frames(viewer->findChild<VideoPreview*>(), &VideoPreview::frame_ready);
    viewer->findChild<QPushButton*>("movie_play")->click(); QTRY_VERIFY(card.loading_original());
    widget.findChild<QPushButton*>("card_preview_cancel")->click(); QTRY_VERIFY(!card.busy());
    list->setCurrentItem(row(widget, 2)); QTRY_VERIFY(!card.busy()); QTest::qWait(100);
    QCOMPARE(frames.size(), 0); QVERIFY(!viewer->findChild<VideoPreview*>()->running()); QVERIFY(!commands().contains("card movie"));
    widget.deactivated(); session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
  void fullscreen_cancel_reports_progress_and_does_not_import() {
    QTemporaryDir folder; CameraSession session; CameraCard card(&session); connect_card(session, card, folder.path());
    CameraCardWidget widget(&card); widget.show(); auto* list = widget.findChild<QTreeWidget*>(); auto* viewer = widget.findChild<ImageViewer*>();
    list->setCurrentItem(row(widget, 3)); QTRY_VERIFY(!card.busy()); qputenv("REMOTE_TEST_SLOW_ORIGINAL", "1");
    // The fixture process inherits its environment at start; restart with the held transfer.
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected); connect_card(session, card, folder.path());
    list->setCurrentItem(row(widget, 3)); QTRY_VERIFY(!card.busy()); viewer->toggle_fullscreen();
    QSignalSpy ready(&card, &CameraCard::original_ready); widget.findChild<QPushButton*>("card_original")->click();
    auto* stop = widget.findChild<QPushButton*>("card_preview_cancel"); QTRY_VERIFY(stop->isEnabled());
    QTRY_COMPARE(widget.findChild<QLabel*>("card_preview_status")->text(), QString("45%")); QVERIFY(stop->isVisible());
    auto* progress = viewer->findChild<QProgressBar*>("card_preview_progress"); QVERIFY(progress);
    QVERIFY(progress->isVisible()); QCOMPARE(progress->window(), viewer); QCOMPARE(progress->value(), 45);
    QVERIFY(progress->isTextVisible());
    if (qEnvironmentVariableIsSet("REMOTE_TEST_SCREENSHOTS")) viewer->grab().save(qEnvironmentVariable("REMOTE_TEST_SCREENSHOTS") + "/fullscreen-download-progress.png");
    viewer->leave_fullscreen(); QTest::qWait(20); QVERIFY(progress->isVisible()); QCOMPARE(progress->value(), 45);
    viewer->toggle_fullscreen(); QTest::qWait(20); QVERIFY(progress->isVisible()); QCOMPARE(progress->value(), 45);
    stop->click(); QTRY_VERIFY(!card.busy()); QCOMPARE(ready.size(), 0); QVERIFY(!stop->isEnabled()); QVERIFY(progress->isHidden());
    QVERIFY(viewer->findChild<QLabel*>("action_overlay")->text().contains(QString::fromUtf8("canceled")));
    QVERIFY(!card.has_imports()); QVERIFY(QDir(session.card_cache_path()).entryList({"*.mp4"}, QDir::Files).isEmpty());
    viewer->leave_fullscreen(); session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
  void fullscreen_buttons_cancel_loading_and_navigate_without_autoplay() {
    qputenv("REMOTE_TEST_SLOW_ORIGINAL", "1");
    QTemporaryDir folder; CameraSession session; CameraCard card(&session); connect_card(session, card, folder.path());
    CameraCardWidget widget(&card); widget.show(); auto* list = widget.findChild<QTreeWidget*>(); auto* viewer = widget.findChild<ImageViewer*>();
    list->setCurrentItem(row(widget, 3)); QTRY_VERIFY(!card.busy()); viewer->toggle_fullscreen(); QTest::qWait(30);
    auto* previous = viewer->findChild<QPushButton*>("viewer_previous"); auto* next = viewer->findChild<QPushButton*>("viewer_next");
    QVERIFY(previous->isVisible()); QVERIFY(next->isVisible());
    QSignalSpy ready(&card, &CameraCard::original_ready), previews(&card, &CameraCard::preview_ready);
    QSignalSpy frames(viewer->findChild<VideoPreview*>(), &VideoPreview::frame_ready);
    viewer->findChild<QPushButton*>("movie_play")->click();
    auto* progress = viewer->findChild<QProgressBar*>("card_preview_progress"); QTRY_COMPARE(progress->value(), 45); QVERIFY(progress->isVisible());
    QVERIFY(next->isEnabled()); next->click();
    QTRY_COMPARE(list->currentItem()->data(0, Qt::UserRole).toString(), QString("1:2"));
    QTRY_VERIFY(!previews.isEmpty() && previews.last()[0].toString() == "1:2"); QTRY_VERIFY(!card.busy());
    QVERIFY(commands().contains("card cancel")); QVERIFY(progress->isHidden()); QCOMPARE(ready.size(), 0);
    previous->click(); QTRY_COMPARE(list->currentItem()->data(0, Qt::UserRole).toString(), QString("1:3")); QTRY_VERIFY(!card.busy());
    QTest::qWait(100); QCOMPARE(frames.size(), 0); QCOMPARE(commands().count(" original"), 1); QVERIFY(!card.has_imports());
    viewer->leave_fullscreen(); session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
  void imported_original_is_reused_and_preserved() {
    QTemporaryDir folder; CameraSession session; CameraCard card(&session); connect_card(session, card, folder.path());
    QSignalSpy copied(&card, &CameraCard::copied); card.copy({"1:3"}, "video", CameraCard::CopyMode::Selected); QTRY_COMPARE(copied.size(), 1); QTRY_VERIFY(!card.busy());
    QSignalSpy ready(&card, &CameraCard::original_ready); card.original("1:3"); QTRY_COMPARE(ready.size(), 1); QTRY_VERIFY(!card.busy());
    QCOMPARE(qvariant_cast<MediaResult>(ready[0][1]).path, copied[0][0].toString()); QVERIFY(!commands().contains("card preview"));
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected); QVERIFY(QFileInfo::exists(copied[0][0].toString()));
  }
  void rapid_selection_suppresses_obsolete_originals() {
    qputenv("REMOTE_TEST_PREVIEW_DELAY", "0.15");
    QTemporaryDir folder; CameraSession session; CameraCard card(&session); connect_card(session, card, folder.path());
    CameraCardWidget widget(&card); widget.show(); auto* list = widget.findChild<QTreeWidget*>(); auto* viewer = widget.findChild<ImageViewer*>();
    list->setCurrentItem(row(widget, 1)); QTRY_VERIFY(commands().contains(" original")); list->setCurrentItem(row(widget, 4));
    QTRY_VERIFY_WITH_TIMEOUT(!card.busy(), 10000); QTRY_COMPARE(viewer->view()->sceneRect().size(), QSizeF(320, 180));
    QCOMPARE(widget.selected_path(), QString("DCIM/four.JPG")); QVERIFY(!commands().contains("card copy"));
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
};
QTEST_MAIN(CardQualityTests)
#include "test_card_quality.moc"
