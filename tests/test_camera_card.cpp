#include "camera_card.h"
#include "camera_card_widget.h"
#include "settings_store.h"
#include "main_window.h"
#include "image_viewer.h"
#include <QTabWidget>
#include <QMessageBox>
#include <QTimer>
#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeWidget>

class CameraCardTests final : public QObject {
  Q_OBJECT
 private:
  QTemporaryDir temporary_;
  QString jpeg_, raw_, fixture_, record_;
  QJsonObject item(int slot, int id, const QString& source, bool protected_file = false) {
    const auto path = QString("DCIM/100MSDCF/кадр %1.JPG").arg(id);
    QJsonArray files{QJsonObject{{"id", 1}, {"format", 0x3801}, {"size", QString::number(QFileInfo(source).size())}, {"path", path}, {"source", source}}};
    if (id == 1) files.append(QJsonObject{{"id", 2}, {"format", 0xb101}, {"size", QString::number(QFileInfo(raw_).size())}, {"path", QString("DCIM/100MSDCF/кадр %1.ARW").arg(id)}, {"source", raw_}});
    return {{"slot", slot}, {"id", id}, {"stamp", QString("2026-10-21T12:00:0%1").arg(id)}, {"identity", QString("test-body-%1-%2").arg(slot).arg(id)}, {"protected", protected_file}, {"deletable", true}, {"files", files}};
  }
  void connect_card(CameraSession& session, CameraCard& card, const QString& folder) {
    connect(&session, &CameraSession::state_changed, &card, [&card](auto state, const QString&) { card.set_connected(state == CameraSession::State::Connected); });
    card.set_directory(folder);
    session.start({FAKE_BACKEND_PATH, "127.0.0.1", "a6700", {}, {}, folder, false});
  }
  QByteArray commands() { QFile file(record_); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
 private slots:
  void initTestCase() {
    QVERIFY(temporary_.isValid()); qputenv("XDG_CONFIG_HOME", temporary_.filePath("config").toUtf8()); jpeg_ = temporary_.filePath("preview.jpg");
    QImage image(640, 480, QImage::Format_RGB32); image.fill(Qt::red); QVERIFY(image.save(jpeg_));
    raw_ = temporary_.filePath("original.arw"); QFile raw(raw_); QVERIFY(raw.open(QIODevice::WriteOnly)); raw.write(QByteArray(1024 * 1024, 'R')); raw.close();
    fixture_ = temporary_.filePath("card.json");
    QFile fixture(fixture_); QVERIFY(fixture.open(QIODevice::WriteOnly));
    fixture.write(QJsonDocument(QJsonArray{item(1, 1, jpeg_), item(1, 2, jpeg_), item(2, 3, jpeg_, true)}).toJson());
    record_ = temporary_.filePath("record.jsonl");
  }
  void init() {
    QFile::remove(record_);
    qputenv("REMOTE_TEST_CARD", fixture_.toUtf8()); qputenv("REMOTE_TEST_CAPTURE", jpeg_.toUtf8());
    qputenv("REMOTE_TEST_RECORD", record_.toUtf8());
  }
  void cleanup() {
    for (const auto* name : {"REMOTE_TEST_CARD", "REMOTE_TEST_CAPTURE", "REMOTE_TEST_RECORD", "REMOTE_TEST_DELETE_FAIL", "REMOTE_TEST_SLOW_COPY"}) qunsetenv(name);
  }
  void legacy_settings_never_enable_original_autotransfer() {
    const auto path = temporary_.filePath("legacy/settings.ini");
    { QSettings old(path, QSettings::IniFormat); old.setValue("auto_sync", true); old.sync(); }
    SettingsStore settings(path); QVERIFY(!settings.load().auto_sync);
    auto values = settings.load(); values.auto_sync = true; QVERIFY(settings.save(values));
    QVERIFY(!settings.load().auto_sync); QSettings saved(path, QSettings::IniFormat); QVERIFY(!saved.contains("auto_sync"));
  }
  void shoot_list_preview_do_not_copy_originals() {
    QTemporaryDir folder; CameraSession session; CameraCard card(&session); connect_card(session, card, folder.path());
    QTRY_COMPARE(session.state(), CameraSession::State::Connected);
    QVERIFY(session.send_command("shoot")); QTest::qWait(80);
    QVERIFY(QDir(folder.path()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    card.refresh(); QTRY_VERIFY(!card.busy()); QCOMPARE(card.items().size(), 3);
    QSignalSpy preview(&card, &CameraCard::preview_ready);
    card.preview("1:1"); QTRY_COMPARE(preview.size(), 1); QVERIFY(!card.busy());
    QCOMPARE(qvariant_cast<QImage>(preview[0][1]).size(), QSize(640, 480));
    QVERIFY(!commands().contains("card copy")); QVERIFY(!commands().contains("sync on"));
    QVERIFY(QDir(folder.path()).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
  void selected_formats_new_resume_and_destination_identity() {
    QTemporaryDir folder; CameraSession session; CameraCard card(&session); connect_card(session, card, folder.path());
    QTRY_COMPARE(session.state(), CameraSession::State::Connected); card.refresh(); QTRY_VERIFY(!card.busy());
    QSignalSpy copied(&card, &CameraCard::copied);
    card.copy({"1:1"}, "jpeg", CameraCard::CopyMode::Selected); QTRY_COMPARE(copied.size(), 1); QTRY_VERIFY(!card.busy());
    QVERIFY(QFileInfo::exists(folder.filePath("Slot_1/DCIM/100MSDCF/кадр 1.JPG")));
    const auto item = card.items().at(2); QCOMPARE(item.key(), QString("1:1"));
    QVERIFY(!card.is_new(item, "jpeg")); QVERIFY(card.is_new(item, "all"));
    card.copy({}, "all", CameraCard::CopyMode::New); QTRY_COMPARE(copied.size(), 4); QTRY_VERIFY(!card.busy());
    card.copy({}, "all", CameraCard::CopyMode::New); QTest::qWait(100); QCOMPARE(copied.size(), 4);
    // Removing a completed local file makes just that file new again.
    QVERIFY(QFile::remove(folder.filePath("Slot_1/DCIM/100MSDCF/кадр 1.ARW")));
    card.copy({}, "raw", CameraCard::CopyMode::New); QTRY_COMPARE(copied.size(), 5); QTRY_VERIFY(!card.busy());
    card.copy({}, "all", CameraCard::CopyMode::All); QTRY_COMPARE(copied.size(), 9); QTRY_VERIFY(!card.busy());
    QVERIFY(QFileInfo::exists(folder.filePath("Slot_1/DCIM/100MSDCF/кадр 1_1.JPG")));
    // A new controller reloads the ledger; no duplicate transfers after restart.
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
    CameraSession second_session; CameraCard second(&second_session); connect_card(second_session, second, folder.path());
    QTRY_COMPARE(second_session.state(), CameraSession::State::Connected); second.refresh(); QTRY_VERIFY(!second.busy());
    QSignalSpy repeated(&second, &CameraCard::copied); second.copy({}, "all", CameraCard::CopyMode::New);
    QTest::qWait(100); QCOMPARE(repeated.size(), 0);
    QTemporaryDir other; second.set_directory(other.path());
    QVERIFY(second.is_new(second.items().first(), "all"));
    second_session.stop(); QTRY_COMPARE(second_session.state(), CameraSession::State::Disconnected);
  }
  void cancel_preserves_queue_and_no_success_on_delete_failure() {
    qputenv("REMOTE_TEST_SLOW_COPY", "1"); qputenv("REMOTE_TEST_DELETE_FAIL", "1");
    QTemporaryDir folder; CameraSession session; CameraCard card(&session); connect_card(session, card, folder.path());
    QTRY_COMPARE(session.state(), CameraSession::State::Connected); card.refresh(); QTRY_VERIFY(!card.busy());
    QSignalSpy progress(&card, &CameraCard::progress_changed), copied(&card, &CameraCard::copied);
    card.copy({}, "all", CameraCard::CopyMode::All); QTRY_VERIFY(commands().contains("card copy"));
    QTRY_VERIFY(progress.size() >= 2); card.cancel(); QTRY_VERIFY(!card.busy()); QCOMPARE(copied.size(), 0);
    const auto data = commands(); QCOMPARE(data.count("card copy"), 1); QVERIFY(data.contains("card cancel"));
    card.remove({"1:1", "1:2"}); QTRY_VERIFY(!card.busy()); QCOMPARE(card.items().size(), 3);
    QCOMPARE(commands().count("card delete"), 1); // Stop at the first error, retain remaining rejects.
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
  void main_window_import_tab_and_cancel_delete_confirmation() {
    QTemporaryDir folder; SettingsStore settings; auto values = settings.load(); values.sdk_terms_accepted = true;
    values.folder = folder.path(); values.studio_review = false; QVERIFY(settings.save(values));
    { QSettings old(settings.path(), QSettings::IniFormat); old.setValue("auto_sync", true); old.sync(); }
    MainWindow window(FAKE_BACKEND_PATH); window.show();
    QTest::mouseClick(window.findChild<QPushButton*>("connect"), Qt::LeftButton);
    auto* session = window.findChild<CameraSession*>(); QTRY_COMPARE(session->state(), CameraSession::State::Connected);
    QTest::mouseClick(window.findChild<QPushButton*>("command_shoot"), Qt::LeftButton); QTest::qWait(80);
    QVERIFY(!commands().contains("card copy")); QVERIFY(!commands().contains("sync on"));
    window.findChild<QTabWidget*>("main_tabs")->setCurrentIndex(1);
    auto* list = window.findChild<QTreeWidget*>("card_items"); QTRY_COMPARE(list->topLevelItemCount(), 3);
    auto* card = window.findChild<CameraCard*>(); QTRY_VERIFY(!card->busy());
    list->setCurrentItem(list->topLevelItem(1)); QTRY_VERIFY(!card->busy());
    list->topLevelItem(1)->setCheckState(1, Qt::Checked);
    QTimer::singleShot(50, &window, [&window] { if (auto* dialog = window.findChild<QMessageBox*>()) dialog->reject(); });
    QTest::mouseClick(window.findChild<QPushButton*>("card_delete"), Qt::LeftButton);
    QVERIFY(!commands().contains("card delete"));
    if (qEnvironmentVariableIsSet("REMOTE_MAIN_CARD_SCREENSHOT")) { QTest::qWait(100); window.grab().save(qEnvironmentVariable("REMOTE_MAIN_CARD_SCREENSHOT")); }
    auto* preview = window.findChild<ImageViewer*>("card_preview"); QVERIFY(preview);
    preview->toggle_fullscreen(); QVERIFY(preview->isFullScreen());
    window.close(); QVERIFY(!preview->isFullScreen()); QVERIFY(!preview->isWindow());
  }
  void reject_marks_are_separate_and_protected_frames_not_deleted() {
    QTemporaryDir folder; CameraSession session; CameraCard card(&session); connect_card(session, card, folder.path());
    QTRY_COMPARE(session.state(), CameraSession::State::Connected); card.refresh(); QTRY_VERIFY(!card.busy());
    CameraCardWidget widget(&card); widget.resize(1200, 700); widget.show();
    auto* list = widget.findChild<QTreeWidget*>("card_items"); QCOMPARE(list->topLevelItemCount(), 3);
    QTest::mouseClick(widget.findChild<QPushButton*>("card_select_all"), Qt::LeftButton);
    for (int i = 0; i < 3; ++i) { QCOMPARE(list->topLevelItem(i)->checkState(0), Qt::Checked); QCOMPARE(list->topLevelItem(i)->checkState(1), Qt::Unchecked); }
    card.remove({"2:3"}); QTest::qWait(50); QVERIFY(!commands().contains("card delete"));
    card.remove({"1:1"}); QTRY_COMPARE(card.items().size(), 2); QTRY_VERIFY(!card.busy());
    QVERIFY(!commands().contains("card copy"));
    if (qEnvironmentVariableIsSet("REMOTE_CARD_SCREENSHOT")) {
      list->setCurrentItem(list->topLevelItem(0)); QTRY_VERIFY(!card.busy()); QTest::qWait(100);
      widget.grab().save(qEnvironmentVariable("REMOTE_CARD_SCREENSHOT"));
    }
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
};
QTEST_MAIN(CameraCardTests)
#include "test_camera_card.moc"
