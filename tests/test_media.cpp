#include "gallery_widget.h"
#include "camera_card.h"
#include "photo_library.h"
#include "media_reader.h"
#include "studio_review.h"
#include "main_window.h"
#include "image_viewer.h"
#include "settings_store.h"
#include "transfer_file.h"
#include <QtTest>
#include <QBuffer>
#include <QComboBox>
#include <QDataStream>
#include <QFile>
#include <QGraphicsScene>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QProcess>
#include <QImageReader>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>

class MediaTests final : public QObject {
  Q_OBJECT
 private:
  QTemporaryDir directory_;
  QString make_jpeg(const QString& name, QSize size = {600, 400}) {
    const auto path = directory_.filePath(name);
    QImage image(size, QImage::Format_RGB32); image.fill(QColor("#3175a2"));
    return image.save(path, "JPEG") ? path : QString();
  }
  QString make_raw_preview() {
    const auto jpeg = make_jpeg("embedded.jpg"); QFile input(jpeg); if (!input.open(QIODevice::ReadOnly)) return {};
    const auto bytes = input.readAll();
    QByteArray raw; QDataStream out(&raw, QIODevice::WriteOnly); out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("II", 2); out << quint16(42) << quint32(8) << quint16(8);
    auto tag = [&out](int code, int type, int count, quint32 value) { out << quint16(code) << quint16(type) << quint32(count) << value; };
    tag(0x100, 4, 1, 6000); tag(0x101, 4, 1, 4000);
    tag(0x10f, 2, 5, 110); tag(0x110, 2, 10, 115); tag(0x112, 3, 1, 6);
    tag(0x14a, 4, 1, 128); tag(0xc612, 1, 4, 0x00000401); tag(0xc614, 2, 10, 115);
    out << quint32(0); out.writeRawData("SONY\0ILCE-6700\0", 15);
    while (raw.size() < 128) out << quint8(0);
    out << quint16(5);
    tag(0x100, 4, 1, 600); tag(0x101, 4, 1, 400); tag(0x103, 3, 1, 6);
    tag(0x201, 4, 1, 194); tag(0x202, 4, 1, bytes.size()); out << quint32(0);
    out.writeRawData(bytes.constData(), bytes.size());
    const auto path = directory_.filePath("synthetic.dng"); QFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(raw) != raw.size()) return {};
    return path;
  }
 private slots:
  void initTestCase() {
    QVERIFY(directory_.isValid());
    qputenv("XDG_CONFIG_HOME", directory_.filePath("config").toUtf8());
    qRegisterMetaType<MediaItems>(); qRegisterMetaType<MediaResult>();
    QProcess tool; tool.start("exiftool", {"-ver"}); QVERIFY(tool.waitForFinished()); QCOMPARE(tool.exitCode(), 0);
  }
  void cleanup() { qunsetenv("REMOTE_TEST_CAPTURE"); qunsetenv("REMOTE_TEST_JPEG"); }
  void jpeg_metadata_and_orientation() {
    const auto path = make_jpeg("exif.jpg"); QVERIFY(!path.isEmpty());
    QProcess write;
    write.start("exiftool", {"-config", "", "-overwrite_original", "-Model=ILCE-6700", "-LensModel=Test Lens",
        "-ExposureTime=1/125", "-FNumber=2.8", "-ISO=400", "-Orientation#=6", "--", path});
    QVERIFY(write.waitForFinished()); QCOMPARE(write.exitCode(), 0);
    const auto result = MediaReader::read(path);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QCOMPARE(result.image.size(), QSize(400, 600));
    const auto text = result.metadata.join('\n');
    QVERIFY(text.contains("ILCE-6700")); QVERIFY(text.contains("Test Lens"));
    QVERIFY(text.contains("1/125")); QVERIFY(text.contains("400")); QVERIFY(!result.raw_preview);
  }
  void raw_embedded_preview_is_labeled_and_oriented() {
    const auto path = make_raw_preview(); QVERIFY(!path.isEmpty());
    if (qEnvironmentVariableIsSet("REMOTE_RAW_FIXTURE_OUTPUT")) {
      QFile::remove(qEnvironmentVariable("REMOTE_RAW_FIXTURE_OUTPUT"));
      QVERIFY(QFile::copy(path, qEnvironmentVariable("REMOTE_RAW_FIXTURE_OUTPUT")));
    }
    const auto result = MediaReader::read(path);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QVERIFY(result.raw_preview);
    QCOMPARE(result.image.size(), QSize(400, 600)); QVERIFY(result.source.contains("JPEG-превью"));
    QVERIFY(result.metadata.join('\n').contains("6000 × 4000"));
    const auto thumb = MediaReader::read(path, true); QVERIFY(!thumb.image.isNull()); QVERIFY(thumb.image.height() <= 192);
  }
  void gallery_pairs_filters_and_search() {
    QTemporaryDir folder;
    QImage image(600, 400, QImage::Format_RGB32); image.fill(Qt::green);
    QVERIFY(image.save(folder.filePath("DSC001.JPG")));
    QVERIFY(image.save(folder.filePath("DSC002.JPG")));
    QFile raw(folder.filePath("DSC001.ARW")); QVERIFY(raw.open(QIODevice::WriteOnly)); raw.write("fixture"); raw.close();
    QDir().mkpath(folder.filePath(".sonshell-incoming/private"));
    QVERIFY(image.save(folder.filePath(".sonshell-incoming/private/unfinished.jpg")));
    PhotoLibrary library; library.set_auto_preview(false);
    MediaItems items;
    connect(&library, &PhotoLibrary::items_changed, this, [&items](const auto& result) { items = result; });
    library.set_directory(folder.path());
    QTRY_COMPARE_WITH_TIMEOUT(items.size(), 2, 4000);
    QVERIFY(std::any_of(items.begin(), items.end(), [](const auto& item) { return item.format() == "RAW+JPEG"; }));
    GalleryWidget gallery; gallery.set_items(items); gallery.resize(1000, 650); gallery.show();
    auto* list = gallery.findChild<QListView*>("gallery_list"); QVERIFY(list); QCOMPARE(list->model()->rowCount(), 2);
    auto* filter = gallery.findChild<QComboBox*>("gallery_format"); filter->setCurrentText("RAW+JPEG"); QCOMPARE(list->model()->rowCount(), 1);
    auto* search = gallery.findChild<QLineEdit*>("gallery_search"); search->setText("DSC002"); QCOMPARE(list->model()->rowCount(), 0);
    filter->setCurrentIndex(0); QCOMPARE(list->model()->rowCount(), 1);
    if (qEnvironmentVariableIsSet("REMOTE_GALLERY_SCREENSHOT")) {
      gallery.show_photo(MediaReader::read(folder.filePath("DSC002.JPG")));
      QTest::qWait(300); gallery.grab().save(qEnvironmentVariable("REMOTE_GALLERY_SCREENSHOT"));
    }
  }
  void file_publication_is_atomic_and_never_skips_partial() {
    QTemporaryDir folder; TransferFile file; std::string error;
    QVERIFY(file.prepare(folder.path().toStdString(), "test.arw", error));
    const auto staged = QString::fromStdString(file.staging_directory()) + "/test.arw";
    QFile output(staged); QVERIFY(output.open(QIODevice::WriteOnly)); output.write("complete fixture"); output.close();
    QVERIFY(!QFileInfo::exists(QString::fromStdString(file.target())));
    QVERIFY(file.publish(error)); QVERIFY(QFileInfo::exists(QString::fromStdString(file.target())));
    TransferFile duplicate; QVERIFY(duplicate.prepare(folder.path().toStdString(), "test.arw", error));
    QFile duplicate_output(QString::fromStdString(duplicate.staging_directory()) + "/test.arw");
    QVERIFY(duplicate_output.open(QIODevice::WriteOnly)); duplicate_output.write("replacement"); duplicate_output.close();
    QVERIFY(!duplicate.publish(error)); duplicate.discard();
    QFile original(QString::fromStdString(file.target())); QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("complete fixture"));
    TransferFile empty; QVERIFY(empty.prepare(folder.path().toStdString(), "missing.arw", error)); QVERIFY(!empty.publish(error)); empty.discard();
  }
  void canceled_decode_and_directory_generation() {
    auto canceled = std::make_shared<std::atomic<bool>>(true);
    const auto jpeg = make_jpeg("cancel.jpg"); QVERIFY(MediaReader::read(jpeg, false, canceled).image.isNull());
    QTemporaryDir first, second;
    QImage image(200, 100, QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(first.filePath("old.jpg")));
    PhotoLibrary library; library.set_auto_preview(false); MediaItems items;
    connect(&library, &PhotoLibrary::items_changed, this, [&items](const auto& result) { items = result; });
    library.set_directory(first.path()); library.set_directory(second.path());
    QTest::qWait(1300); QVERIFY(items.isEmpty());
    QVERIFY(image.save(second.filePath("current.jpg")));
    QTRY_COMPARE_WITH_TIMEOUT(items.size(), 1, 4000); QVERIFY(items.first().path().endsWith("current.jpg"));
  }
  void studio_restarts_holds_and_returns() {
    StudioReview review; review.configure(true, 2);
    MediaResult result; result.image = QImage(200, 100, QImage::Format_RGB32);
    QSignalSpy shown(&review, &StudioReview::photo_presented), returned(&review, &StudioReview::returned_to_liveview);
    review.present(result); QVERIFY(review.active());
    QTest::qWait(1100); review.present(result); QCOMPARE(shown.size(), 2);
    QTest::qWait(1100); QVERIFY(review.active());
    QTRY_COMPARE_WITH_TIMEOUT(returned.size(), 1, 1500); QVERIFY(!review.active());
    review.present(result); review.hold(true); QTest::qWait(2100); QVERIFY(review.active()); QVERIFY(review.held());
    review.hold(false); review.configure(false, 3); QVERIFY(!review.active()); QVERIFY(!review.held());
    review.present(result); QVERIFY(!review.active());
  }
  void photo_navigation_embedded_and_separate_fullscreen_window() {
    QTemporaryDir folder;
    for (int i = 0; i < 3; ++i) {
      QImage image(400 + i * 100, 300, QImage::Format_RGB32); image.fill(Qt::green);
      QVERIFY(image.save(folder.filePath(QString("photo-%1.jpg").arg(i))));
    }
    SettingsStore settings; auto values = settings.load(); values.folder = folder.path(); values.studio_review = false;
    QVERIFY(settings.save(values));
    MainWindow window(FAKE_BACKEND_PATH); window.show(); window.activateWindow();
    window.findChild<QTabWidget*>("main_tabs")->setCurrentIndex(2);
    auto* gallery = window.findChild<GalleryWidget*>(); auto* list = gallery->findChild<QListView*>("gallery_list");
    auto* photos = window.findChild<PhotoLibrary*>(); QSignalSpy loaded(photos, &PhotoLibrary::photo_loaded);
    QTRY_COMPARE_WITH_TIMEOUT(list->model()->rowCount(), 3, 4000);
    list->setCurrentIndex(list->model()->index(1, 0)); list->setCurrentIndex(list->model()->index(0, 0));
    const auto first = list->currentIndex().data(GalleryModel::PathRole).toString();
    QTRY_VERIFY(!loaded.isEmpty() && qvariant_cast<MediaResult>(loaded.last()[0]).path == first);
    gallery->viewer()->view()->setFocus(); QTest::qWait(20);
    QTest::keyClick(gallery->viewer()->view(), Qt::Key_Right);
    QTRY_COMPARE(list->currentIndex().row(), 1);
    QTest::keyClick(gallery->viewer()->view(), Qt::Key_Left); QTRY_COMPARE(list->currentIndex().row(), 0);
    QTest::mouseClick(gallery->findChild<QPushButton*>("open_photo_window"), Qt::LeftButton);
    QTRY_VERIFY(window.findChild<ImageViewer*>("photo_window"));
    auto* separate = window.findChild<ImageViewer*>("photo_window");
    separate->toggle_fullscreen(); QTest::qWait(30);
    QTest::keyClick(separate->view(), Qt::Key_PageDown); QTRY_COMPARE(list->currentIndex().row(), 1);
    const auto next = list->currentIndex().data(GalleryModel::PathRole).toString();
    QTRY_VERIFY(qvariant_cast<MediaResult>(loaded.last()[0]).path == next);
    QCOMPARE(separate->view()->scene()->itemsBoundingRect().size(), QSizeF(QImageReader(next).size()));
    QTest::keyClick(separate->view(), Qt::Key_PageUp); QTRY_COMPARE(list->currentIndex().row(), 0);
    gallery->findChild<QLineEdit*>("gallery_search")->setText(QFileInfo(next).fileName());
    QTRY_COMPARE(list->model()->rowCount(), 1);
    QTest::keyClick(separate->view(), Qt::Key_Right); QTRY_COMPARE(list->currentIndex().row(), 0);
    QTRY_VERIFY(qvariant_cast<MediaResult>(loaded.last()[0]).path == next);
    window.close();
  }
  void studio_uses_compressed_preview_without_original_transfer() {
    const auto live = make_jpeg("live.jpg", {640, 480}); const auto captured = make_jpeg("capture.jpg", {1200, 800});
    qputenv("REMOTE_TEST_JPEG", live.toUtf8()); qputenv("REMOTE_TEST_CAPTURE", captured.toUtf8());
    QTemporaryDir captures;
    SettingsStore settings; auto values = settings.load(); values.sdk_terms_accepted = true;
    values.folder = captures.path(); values.studio_review = true; values.review_seconds = 2; QVERIFY(settings.save(values));
    MainWindow window(FAKE_BACKEND_PATH); window.show();
    auto* connect_button = window.findChild<QPushButton*>("connect"); QTest::mouseClick(connect_button, Qt::LeftButton);
    auto* session = window.findChild<CameraSession*>(); QVERIFY(session);
    QTRY_COMPARE(session->state(), CameraSession::State::Connected);
    auto* viewer = window.findChild<ImageViewer*>("live_view");
    QTRY_COMPARE(viewer->view()->scene()->itemsBoundingRect().size(), QSizeF(640, 480));
    auto* review = window.findChild<StudioReview*>("studio_review"); QVERIFY(review);
    QSignalSpy completed(session, &CameraSession::download_completed), progress(session, &CameraSession::transfer_progress);
    QTest::mouseClick(window.findChild<QPushButton*>("command_shoot"), Qt::LeftButton);
    QTest::qWait(100); QCOMPARE(completed.size(), 0); QCOMPARE(progress.size(), 0);
    QTRY_VERIFY(review->active()); QCOMPARE(viewer->view()->scene()->itemsBoundingRect().size(), QSizeF(1200, 800));
    QTRY_VERIFY_WITH_TIMEOUT(!review->active(), 3500);
    QCOMPARE(viewer->view()->scene()->itemsBoundingRect().size(), QSizeF(640, 480));
    auto* card = window.findChild<CameraCard*>(); QVERIFY(card);
    QVERIFY(!QFileInfo::exists(captures.filePath("Slot_1/DCIM/100MSDCF/снимок 1.jpg")));
    QSignalSpy copied(card, &CameraCard::copied);
    card->copy({}, "all", CameraCard::CopyMode::All);
    QTRY_COMPARE(copied.size(), 1); QTest::qWait(400); QVERIFY(!review->active());
    auto* tabs = window.findChild<QTabWidget*>("main_tabs"); tabs->setCurrentIndex(2);
    auto* list = window.findChild<QListView*>("gallery_list");
    QTRY_COMPARE_WITH_TIMEOUT(list->model()->rowCount(), 1, 4000);
    if (qEnvironmentVariableIsSet("REMOTE_MAIN_GALLERY_SCREENSHOT")) window.grab().save(qEnvironmentVariable("REMOTE_MAIN_GALLERY_SCREENSHOT"));
    window.close();
  }
};
QTEST_MAIN(MediaTests)
#include "test_media.moc"
