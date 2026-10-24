#include "camera_controls_widget.h"
#include "exposure_control.h"
#include "camera_card.h"
#include "camera_session.h"
#include "help_dialog.h"
#include "image_viewer.h"
#include "language_support.h"
#include "main_window.h"
#include "settings_store.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QDial>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QWheelEvent>
#include <QtTest>

class ControlsTests final : public QObject {
  Q_OBJECT
 private:
  QTemporaryDir temporary_;
  QByteArray commands() { QFile file(temporary_.filePath("commands.jsonl")); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
 private slots:
  void initTestCase() { QVERIFY(temporary_.isValid()); qputenv("XDG_CONFIG_HOME", temporary_.filePath("config").toUtf8()); }
  void init() {
    QFile::remove(temporary_.filePath("commands.jsonl"));
    qputenv("REMOTE_TEST_RECORD", temporary_.filePath("commands.jsonl").toUtf8());
  }
  void cleanup() {
    for (const auto* key : {"REMOTE_TEST_RECORD", "REMOTE_TEST_CONTROLS_READONLY", "REMOTE_TEST_CONTROLS_FAIL", "REMOTE_TEST_CONTROLS_LIST_FAIL", "REMOTE_TEST_AF_FAIL", "REMOTE_TEST_AF_UNAVAILABLE"}) qunsetenv(key);
  }
  void image_coordinates_exclude_margins_and_follow_zoom_and_pan() {
    ImageView view; view.resize(1000, 400); view.show();
    QImage image(800, 600, QImage::Format_RGB32); image.fill(Qt::blue); view.set_image(image);
    QTest::qWait(30);
    const auto center = view.mapFromScene(QPointF(400, 300));
    const auto normalized = view.image_position(center);
    QVERIFY(std::abs(normalized.x() - 0.5) < 0.005); QVERIFY(std::abs(normalized.y() - 0.5) < 0.005);
    QVERIFY(view.image_position({0, 0}).x() < 0);
    view.actual_size(); view.zoom(2); view.centerOn(500, 350);
    const auto panned = view.image_position(view.mapFromScene(QPointF(500, 350)));
    QVERIFY(std::abs(panned.x() - 0.625) < 0.005); QVERIFY(std::abs(panned.y() - 350.0 / 600) < 0.005);
    QSignalSpy selected(&view, &ImageView::point_selected); view.set_point_selection(true);
    const auto point = view.mapFromScene(QPointF(500, 350));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point); QCOMPARE(selected.size(), 1);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, point + QPoint(60, 0)); QCOMPARE(selected.size(), 1);
    view.set_point_selection(false); QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point); QCOMPARE(selected.size(), 1);
    view.set_image({}); QVERIFY(view.image_position(point).x() < 0);
  }
  void capability_controls_and_af_follow_confirmed_session_state() {
    CameraSession session; CameraCard card(&session); CameraControlsWidget controls(&session, &card); controls.show();
    session.start({FAKE_BACKEND_PATH, "127.0.0.1", "a6700", {}, {}, temporary_.path(), false});
    auto* focus = controls.findChild<QComboBox*>("focus_mode"); auto* af = controls.findChild<QCheckBox*>("mouse_af");
    QTRY_VERIFY(focus->isEnabled()); QCOMPARE(focus->count(), 2); QVERIFY(!af->isEnabled());
    controls.set_live(true); QVERIFY(af->isEnabled()); af->setChecked(true);
    controls.select_point({0.5, 0.5}); QTRY_VERIFY(commands().contains("controls af")); QTRY_VERIFY(af->isEnabled());
    QVERIFY(commands().contains("320 240"));
    focus->setFocus(); QTest::keyClick(focus, Qt::Key_Down); QTRY_VERIFY(commands().contains("controls set"));
    QTRY_VERIFY(focus->isEnabled());
    controls.set_live(false); QVERIFY(!af->isEnabled());
    const auto before = commands().count("controls af"); controls.select_point({0.7, 0.4}); QTest::qWait(30);
    QCOMPARE(commands().count("controls af"), before);
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
    QVERIFY(!focus->isEnabled()); QVERIFY(!af->isEnabled());
  }
  void read_only_camera_does_not_offer_controls_or_af() {
    qputenv("REMOTE_TEST_CONTROLS_READONLY", "1");
    CameraSession session; CameraCard card(&session); CameraControlsWidget controls(&session, &card);
    session.start({FAKE_BACKEND_PATH, "127.0.0.1", "a6700", {}, {}, temporary_.path(), false});
    auto* focus = controls.findChild<QComboBox*>("focus_mode");
    QTRY_COMPARE(focus->count(), 2); QVERIFY(!focus->isEnabled());
    controls.set_live(true); QVERIFY(!controls.findChild<QCheckBox*>("mouse_af")->isEnabled());
    controls.select_point({0.5, 0.5}); QVERIFY(!commands().contains("controls af"));
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
  void unsupported_capabilities_do_not_start_a_retry_loop() {
    qputenv("REMOTE_TEST_CONTROLS_LIST_FAIL", "1");
    CameraSession session; CameraCard card(&session); CameraControlsWidget controls(&session, &card);
    session.start({FAKE_BACKEND_PATH, "127.0.0.1", "a6700", {}, {}, temporary_.path(), false});
    QTRY_VERIFY(commands().contains("controls list")); QTest::qWait(800);
    QCOMPARE(commands().count("controls list"), 1);
    QVERIFY(!controls.findChild<QComboBox*>("focus_mode")->isEnabled());
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
  void rejected_autofocus_is_explained_after_point_was_moved() {
    qputenv("REMOTE_TEST_AF_FAIL", "1");
    CameraSession session; CameraCard card(&session); CameraControlsWidget controls(&session, &card);
    session.start({FAKE_BACKEND_PATH, "127.0.0.1", "a6700", {}, {}, temporary_.path(), false});
    controls.set_live(true); auto* af = controls.findChild<QCheckBox*>("mouse_af");
    QTRY_VERIFY(af->isEnabled()); af->setChecked(true); controls.select_point({0.5, 0.5});
    auto* message = controls.findChild<QLabel*>("controls_message");
    QTRY_VERIFY(message->text().contains(QString::fromUtf8("запуск AF отклонён")));
    QTest::qWait(500); QVERIFY(message->text().contains(QString::fromUtf8("запуск AF отклонён")));
    QVERIFY(message->text().contains("32768"));
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
  void unavailable_autofocus_does_not_offer_click_action() {
    qputenv("REMOTE_TEST_AF_UNAVAILABLE", "1");
    CameraSession session; CameraCard card(&session); CameraControlsWidget controls(&session, &card);
    session.start({FAKE_BACKEND_PATH, "127.0.0.1", "a6700", {}, {}, temporary_.path(), false});
    controls.set_live(true); QTRY_VERIFY(controls.findChild<QComboBox*>("focus_mode")->isEnabled());
    QVERIFY(!controls.findChild<QCheckBox*>("mouse_af")->isEnabled());
    controls.select_point({0.5, 0.5}); QVERIFY(!commands().contains("controls af"));
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
  void rejected_setting_restores_camera_value_and_keeps_error_visible() {
    qputenv("REMOTE_TEST_CONTROLS_FAIL", "1");
    CameraSession session; CameraCard card(&session); CameraControlsWidget controls(&session, &card); controls.show();
    session.start({FAKE_BACKEND_PATH, "127.0.0.1", "a6700", {}, {}, temporary_.path(), false});
    auto* focus = controls.findChild<QComboBox*>("focus_mode"); QTRY_VERIFY(focus->isEnabled());
    focus->setFocus(); QTest::keyClick(focus, Qt::Key_Down); QTRY_VERIFY(commands().contains("controls set"));
    QTRY_VERIFY(focus->isEnabled()); QTRY_COMPARE(focus->currentData().toString(), QString("1"));
    bool visible_error = false;
    for (auto* label : controls.findChildren<QLabel*>()) if (label->text().contains(QString::fromUtf8("Команда отклонена"))) visible_error = true;
    QVERIFY(visible_error);
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
  void exposure_dial_manual_input_and_auto_fit_in_main_window() {
    auto values = SettingsStore().load(); values.sdk_terms_accepted = true; values.folder = temporary_.filePath("photos");
    QVERIFY(SettingsStore().save(values));
    MainWindow window(FAKE_BACKEND_PATH); window.resize(1100, 800); window.show();
    QTest::mouseClick(window.findChild<QPushButton*>("connect"), Qt::LeftButton);
    auto* iso = window.findChild<ExposureControl*>("iso"); QVERIFY(iso);
    auto* value = iso->findChild<QComboBox*>("value"); auto* dial = iso->findChild<QDial*>("dial");
    QTRY_VERIFY(dial->isEnabled()); QCOMPARE(value->currentText(), QString("AUTO"));
    QVERIFY(value->lineEdit()->contentsRect().width() >= value->fontMetrics().horizontalAdvance("AUTO") + 8);
    const auto before = commands().count("controls set");
    // Moving a held dial changes the displayed value, without streaming writes to the SDK.
    dial->setSliderDown(true); dial->setSliderPosition(1); dial->setSliderPosition(3);
    QCOMPARE(value->currentText(), QString("400")); QTest::qWait(350);
    QCOMPARE(commands().count("controls set"), before);
    dial->setSliderDown(false); QTRY_VERIFY(commands().contains("110 400"));
    QTRY_VERIFY(dial->isEnabled()); QTRY_COMPARE(value->currentText(), QString("AUTO")); // fake camera keeps its original value
    value->setEditText("iso 200"); QTest::keyClick(value->lineEdit(), Qt::Key_Return);
    QTRY_VERIFY(commands().contains("110 200")); QTRY_VERIFY(dial->isEnabled());
    const auto accepted = commands().count("controls set");
    value->setEditText("123"); QTest::mouseClick(iso->findChild<QPushButton*>("apply"), Qt::LeftButton);
    QTest::qWait(350); QCOMPARE(commands().count("controls set"), accepted);
    QVERIFY(iso->findChild<QLabel*>("validation")->isVisible());
    QTest::keyClick(value->lineEdit(), Qt::Key_Escape); QCOMPARE(value->currentText(), QString("AUTO"));
    const auto wheel_before = commands().count("controls set");
    const auto center = dial->rect().center();
    QWheelEvent wheel(center, dial->mapToGlobal(center), {}, {0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(dial, &wheel);
    QCOMPARE(commands().count("controls set"), wheel_before); QTRY_VERIFY(commands().contains("110 100"));
    QTRY_VERIFY(dial->isEnabled()); QTRY_COMPARE(value->currentText(), QString("AUTO"));
    if (qEnvironmentVariableIsSet("REMOTE_TEST_SCREENSHOTS")) {
      window.findChild<QScrollArea*>()->verticalScrollBar()->setValue(0);
      window.grab().save(qEnvironmentVariable("REMOTE_TEST_SCREENSHOTS") + "/exposure-ru.png");
    }
    window.close();
  }
  void exposure_steps_follow_camera_and_read_only_state() {
    CameraSession session; CameraCard card(&session); CameraControlsWidget controls(&session, &card); controls.show();
    session.start({FAKE_BACKEND_PATH, "127.0.0.1", "a6700", {}, {}, temporary_.path(), false});
    auto* shutter = controls.findChild<ExposureControl*>("shutter"); auto* dial = shutter->findChild<QDial*>("dial");
    QTRY_VERIFY(dial->isEnabled()); auto* value = shutter->findChild<QComboBox*>("value");
    QCOMPARE(value->itemText(0), QString("1/250")); QCOMPARE(value->itemText(2), QString("1/60"));
    value->setEditText("1/60"); QTest::keyClick(value->lineEdit(), Qt::Key_Return); QTRY_VERIFY(commands().contains("111 65596"));
    QTRY_VERIFY(dial->isEnabled());
    auto* aperture = controls.findChild<ExposureControl*>("aperture");
    auto* f = aperture->findChild<QComboBox*>("value"); f->setEditText("4,0");
    const auto lists = commands().count("controls list"); session.controls_changed(); QTest::qWait(350);
    QCOMPARE(f->currentText(), QString("4,0")); QCOMPARE(commands().count("controls list"), lists);
    QTest::keyClick(f->lineEdit(), Qt::Key_Return); QTRY_VERIFY(commands().contains("112 400"));
    QTRY_VERIFY(dial->isEnabled());
    auto* ev = controls.findChild<ExposureControl*>("ev"); auto* e = ev->findChild<QComboBox*>("value");
    QCOMPARE(e->itemText(0), QString("-0.3")); e->setEditText("-0,3 EV");
    QTest::keyClick(e->lineEdit(), Qt::Key_Return); QTRY_VERIFY(commands().contains("113 65203"));
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected); QVERIFY(!dial->isEnabled());
    qputenv("REMOTE_TEST_CONTROLS_READONLY", "1");
    session.start({FAKE_BACKEND_PATH, "127.0.0.1", "a6700", {}, {}, temporary_.path(), false});
    QTRY_COMPARE(value->count(), 3); QVERIFY(!dial->isEnabled()); QVERIFY(!value->isEnabled());
    session.stop(); QTRY_COMPARE(session.state(), CameraSession::State::Disconnected);
  }
  void exposure_wheel_is_coalesced_and_cancelled_when_unavailable() {
    ExposureControl control("iso"); control.show();
    control.set_snapshot({{"code", 110}, {"readable", true}, {"writable", true}, {"current", "100"},
        {"values", QJsonArray{QJsonObject{{"value", "100"}, {"label", "100"}}, QJsonObject{{"value", "200"}, {"label", "200"}}, QJsonObject{{"value", "400"}, {"label", "400"}}}}});
    control.set_ready(true); auto* dial = control.findChild<QDial*>("dial");
    QSignalSpy requested(&control, &ExposureControl::value_requested);
    auto wheel = [&] {
      const auto p = dial->rect().center();
      QWheelEvent event(p, dial->mapToGlobal(p), {}, {0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
      QApplication::sendEvent(dial, &event);
    };
    wheel(); QTest::qWait(100); wheel(); QCOMPARE(requested.size(), 0);
    QTRY_COMPARE(requested.size(), 1); QCOMPARE(requested[0][1].toString(), QString("400"));
    control.cancel_edit(); requested.clear(); wheel(); control.set_ready(false); QTest::qWait(350);
    QCOMPARE(requested.size(), 0); QVERIFY(!dial->isEnabled());
  }
  void english_help_language_persistence_and_fullscreen_f1() {
    LanguageSupport language; QVERIFY(language.install("en"));
    auto values = SettingsStore().load(); values.language = "en"; values.folder = temporary_.filePath("photos");
    QVERIFY(SettingsStore().save(values));
    {
      MainWindow window(FAKE_BACKEND_PATH); window.show(); window.activateWindow();
      QCOMPARE(window.findChild<QPushButton*>("connect")->text(), QString("Connect"));
      auto* live = window.findChild<ImageViewer*>("live_view"); live->toggle_fullscreen(); QTest::qWait(30);
      QTest::keyClick(live->view(), Qt::Key_F1);
      QTRY_VERIFY(window.findChild<HelpDialog*>("help_dialog"));
      auto* help = window.findChild<HelpDialog*>("help_dialog");
      const auto text = help->findChild<QTextBrowser*>("help_content")->toPlainText();
      QVERIFY(text.contains("Delete only marks a reject.")); QVERIFY(text.contains("Space")); QVERIFY(text.size() > 3000);
      help->close(); live->leave_fullscreen();
      window.findChild<QAction*>("language_ru")->trigger(); QCOMPARE(SettingsStore().load().language, QString("ru"));
      // Changing the next-launch choice does not partially translate the current UI.
      QCOMPARE(window.findChild<QPushButton*>("connect")->text(), QString("Connect"));
      if (qEnvironmentVariableIsSet("REMOTE_TEST_SCREENSHOTS")) {
        const auto path = qEnvironmentVariable("REMOTE_TEST_SCREENSHOTS");
        window.grab().save(path + "/main-en.png"); help = new HelpDialog("en", &window);
        help->show(); QTest::qWait(30); help->grab().save(path + "/help-en.png"); help->close();
      }
      window.close();
    }
    QVERIFY(language.install(SettingsStore().load().language));
    MainWindow russian(FAKE_BACKEND_PATH);
    QCOMPARE(russian.findChild<QPushButton*>("connect")->text(), QString::fromUtf8("Подключить"));
    if (qEnvironmentVariableIsSet("REMOTE_TEST_SCREENSHOTS")) {
      russian.show(); QTest::qWait(30); russian.grab().save(qEnvironmentVariable("REMOTE_TEST_SCREENSHOTS") + "/main-ru.png");
    }
    HelpDialog help("ru"); QVERIFY(help.findChild<QTextBrowser*>()->toPlainText().contains("Delete только ставит отметку."));
    QSettings raw(SettingsStore().path(), QSettings::IniFormat); raw.setValue("language", "invalid"); raw.sync();
    QCOMPARE(SettingsStore().load().language, QString("ru"));
  }
};
QTEST_MAIN(ControlsTests)
#include "test_controls.moc"
