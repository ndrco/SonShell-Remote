#include <QCoreApplication>
#include "camera_controls_widget.h"
#include "camera_session.h"
#include "camera_card.h"
#include "exposure_control.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

CameraControlsWidget::CameraControlsWidget(CameraSession* session, CameraCard* card, QWidget* parent)
    : QGroupBox(QCoreApplication::translate("App", "Camera settings · SDK"), parent), session_(session), card_(card) {
  setObjectName("camera_controls");
  auto* groups = new QVBoxLayout(this);
  auto* exposure_box = new QGroupBox(QCoreApplication::translate("App", "Exposure"), this);
  auto* exposure_layout = new QFormLayout(exposure_box);
  exposure_layout->setRowWrapPolicy(QFormLayout::WrapAllRows);
  const QStringList exposure_names{"ISO", QCoreApplication::translate("App", "Shutter speed"), QCoreApplication::translate("App", "Aperture"), QCoreApplication::translate("App", "Exposure compensation")};
  const QStringList exposure_keys{"iso", "shutter", "aperture", "ev"};
  for (int i = 0; i < exposure_names.size(); ++i) {
    auto* control = new ExposureControl(exposure_keys[i], exposure_box); exposure_.append(control);
    exposure_layout->addRow(exposure_names[i], control);
    connect(control, &ExposureControl::value_requested, this, [this](const QString& code, const QString& value) { request("set", code + ' ' + value); });
    connect(control, &ExposureControl::editing_finished, this, [this] { if (dirty_ && !refresh_timer_.isActive()) refresh_timer_.start(); });
  }
  groups->addWidget(exposure_box);
  auto* layout = new QFormLayout; groups->addLayout(layout);
  layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
  auto add_editor = [this](QFormLayout* form, const QString& name, const QString& key) {
    auto* editor = new QComboBox(this); editor->setObjectName(key); editor->setMinimumContentsLength(15);
    editor->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    editor->addItem(QCoreApplication::translate("App", "Unavailable")); editor->setEnabled(false); editors_.append(editor); form->addRow(name, editor);
    connect(editor, qOverload<int>(&QComboBox::activated), this, [this, editor](int index) {
      request("set", editor->property("sdk_code").toString() + ' ' + editor->itemData(index).toString());
    });
  };
  const QStringList names{QCoreApplication::translate("App", "Focus mode"), QCoreApplication::translate("App", "AF area"), QCoreApplication::translate("App", "White balance"), QCoreApplication::translate("App", "Drive mode")};
  const QStringList keys{"focus_mode", "focus_area", "white_balance", "drive_mode"};
  for (int i = 0; i < names.size(); ++i) {
    add_editor(layout, names[i], keys[i]);
  }
  mouse_af_ = new QCheckBox(QCoreApplication::translate("App", "Click to focus"), this); mouse_af_->setObjectName("mouse_af");
  mouse_af_->setToolTip(QCoreApplication::translate("App", "Click to select a point and start autofocus. Drag to pan the image."));
  layout->addRow(mouse_af_);
  auto section = [this, groups](const QString& title, const QString& name) {
    auto* toggle = new QToolButton(this); toggle->setObjectName(name); toggle->setText(title);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); toggle->setArrowType(Qt::RightArrow); toggle->setCheckable(true);
    toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); groups->addWidget(toggle);
    auto* body = new QWidget(this); body->setObjectName(name + "_body"); body->hide(); groups->addWidget(body);
    auto* form = new QFormLayout(body); form->setRowWrapPolicy(QFormLayout::WrapAllRows); form->setContentsMargins(8, 0, 0, 6);
    connect(toggle, &QToolButton::toggled, this, [toggle, body](bool expanded) { body->setVisible(expanded); toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow); });
    return form;
  };
  auto* photo = section(QCoreApplication::translate("App", "Photo recording"), "record_photo");
  findChild<QToolButton*>("record_photo")->setToolTip(QCoreApplication::translate("App", "Format and quality of new photos on the card. Choose JPEG / HEIF first, then the photo format. Capabilities depend on the camera."));
  add_editor(photo, QCoreApplication::translate("App", "Photo format RAW/JPEG/HEIF"), "file_type");
  add_editor(photo, QCoreApplication::translate("App", "JPEG / HEIF"), "compression_format");
  add_editor(photo, QCoreApplication::translate("App", "JPEG/HEIF quality"), "image_quality");
  add_editor(photo, QCoreApplication::translate("App", "Photo size"), "image_size");
  add_editor(photo, QCoreApplication::translate("App", "Aspect ratio"), "aspect_ratio");
  add_editor(photo, QCoreApplication::translate("App", "RAW compression"), "raw_compression");
  add_editor(photo, QCoreApplication::translate("App", "Photo stabilization"), "steady_still");
  auto* movie = section(QCoreApplication::translate("App", "Video recording"), "record_movie");
  findChild<QToolButton*>("record_movie")->setToolTip(QCoreApplication::translate("App", "Choose the video format, then the frame rate and recording settings from the updated camera choices."));
  add_editor(movie, QCoreApplication::translate("App", "Video format"), "movie_format");
  add_editor(movie, QCoreApplication::translate("App", "Frame rate"), "movie_framerate");
  add_editor(movie, QCoreApplication::translate("App", "Video bitrate and color"), "movie_setting");
  add_editor(movie, QCoreApplication::translate("App", "Video stabilization"), "steady_movie");
  message_ = new QLabel(QCoreApplication::translate("App", "Connect a camera to read available settings"), this);
  message_->setObjectName("controls_message");
  message_->setWordWrap(true); message_->setTextFormat(Qt::PlainText); groups->addWidget(message_);
  auto* refresh = new QPushButton(QCoreApplication::translate("App", "Read settings"), this); groups->addWidget(refresh);
  refresh_timer_.setSingleShot(true); refresh_timer_.setInterval(250);
  deadline_.setSingleShot(true); deadline_.setInterval(7000);
  connect(refresh, &QPushButton::clicked, this, [this] { for (auto* control : exposure_) control->cancel_edit(); this->refresh(); });
  connect(&refresh_timer_, &QTimer::timeout, this, &CameraControlsWidget::refresh);
  connect(mouse_af_, &QCheckBox::toggled, this, [this] { update_enabled(); });
  connect(&deadline_, &QTimer::timeout, this, [this] {
    pending_.clear(); available_af_ = false;
    for (auto* editor : editors_) editor->setProperty("writable", false);
    for (auto* control : exposure_) control->set_snapshot({});
    message_->setText(QCoreApplication::translate("App", "The camera did not respond. Read settings again.")); update_enabled();
  });
  connect(session_, &CameraSession::state_changed, this, [this, refresh](auto state, const QString&) {
    connected_ = state == CameraSession::State::Connected;
    refresh->setEnabled(connected_); pending_.clear(); deadline_.stop(); refresh_timer_.stop();
    available_af_ = false; dirty_ = false; awaiting_snapshot_ = false; last_error_.clear();
    for (auto* editor : editors_) { editor->clear(); editor->addItem(QCoreApplication::translate("App", "Unavailable")); editor->setProperty("writable", false); }
    for (auto* control : exposure_) control->set_snapshot({});
    message_->setText(connected_ ? QCoreApplication::translate("App", "Reading camera capabilities…") : QCoreApplication::translate("App", "Connect a camera to read available settings"));
    update_enabled(); if (connected_) refresh_timer_.start();
  });
  refresh->setEnabled(false);
  connect(session_, &CameraSession::controls_changed, this, [this] {
    dirty_ = true;
    if (!refresh_timer_.isActive()) refresh_timer_.start();
  });
  connect(card_, &CameraCard::busy_changed, this, [this](bool busy) {
    update_enabled(); if (!busy && dirty_ && !refresh_timer_.isActive()) refresh_timer_.start();
  });
  connect(session_, &CameraSession::controls_event, this, [this](const QJsonObject& event) {
    if (pending_.isEmpty() || event.value("request").toString() != pending_) return;
    const auto type = event.value("type").toString();
    if (type != "snapshot" && type != "applied" && type != "error") return;
    const auto operation = operation_;
    pending_.clear(); deadline_.stop();
    if (type == "snapshot") {
      awaiting_snapshot_ = false;
      const auto properties = event.value("properties").toArray();
      for (auto* control : exposure_) {
        QJsonObject found;
        for (const auto& entry : properties) if (entry.toObject().value("name").toString() == control->objectName()) { found = entry.toObject(); break; }
        control->set_snapshot(found);
      }
      for (auto* editor : editors_) {
        QSignalBlocker guard(editor); editor->clear(); editor->setProperty("writable", false);
        for (const auto& entry : properties) {
          const auto property = entry.toObject();
          if (property.value("name").toString() != editor->objectName()) continue;
          editor->setProperty("sdk_code", property.value("code").toInt());
          editor->setProperty("writable", property.value("writable").toBool());
          const auto current = property.value("current").toString();
          for (const auto& choice : property.value("values").toArray()) {
            const auto value = choice.toObject(); editor->addItem(value.value("label").toString(), value.value("value").toString());
          }
          int index = editor->findData(current);
          if (index < 0 && property.value("readable").toBool()) { editor->addItem(property.value("current_label").toString(current), current); index = editor->count() - 1; }
          if (index >= 0) editor->setCurrentIndex(index);
        }
        if (!editor->count()) editor->addItem(QCoreApplication::translate("App", "Unavailable"));
        editor->setToolTip(editor->currentText() + '\n' + (editor->property("writable").toBool() ? QCoreApplication::translate("App", "Values provided by the camera") : QCoreApplication::translate("App", "The SDK does not allow changing this setting in the current mode")));
      }
      available_af_ = event.value("af").toBool();
      message_->setText(available_af_ ? QCoreApplication::translate("App", "Click in Live View to select a point and start autofocus.") :
          QCoreApplication::translate("App", "Click to focus is unavailable: enable an AF mode, a movable focus area and Live View."));
      if (!last_error_.isEmpty()) message_->setText(last_error_);
    } else if (type == "error") {
      if (operation != "list") awaiting_snapshot_ = true;
      const auto reason = event.value("message").toString();
      const auto description = reason == "af-outside" ? QCoreApplication::translate("App", "Point outside the permitted AF area") :
          reason == "read-only" ? QCoreApplication::translate("App", "The SDK does not allow changing this setting in the current mode") :
          reason == "af-unavailable" ? QCoreApplication::translate("App", "Autofocus is unavailable in the current camera mode") :
          reason.startsWith("af-press:") ? QString(QCoreApplication::translate("App", "Point moved, but starting AF was rejected (SDK %1)")).arg(reason.mid(9)) :
          reason.startsWith("af-release:") ? QString(QCoreApplication::translate("App", "AF release could not be confirmed (SDK %1). Reconnect the camera.")).arg(reason.mid(11)) : reason;
      last_error_ = QCoreApplication::translate("App", "Command rejected by the camera: ") + description;
      message_->setText(last_error_); dirty_ = operation != "list";
    } else {
      awaiting_snapshot_ = true;
      message_->setText(operation == "af" ? QCoreApplication::translate("App", "Point selected; the camera accepted the autofocus command.") :
          QCoreApplication::translate("App", "Setting sent. Reading the camera value…")); dirty_ = true;
    }
    update_enabled(); if (dirty_ && !refresh_timer_.isActive()) refresh_timer_.start();
  });
  update_enabled();
}
void CameraControlsWidget::set_live(bool enabled) {
  const bool started = enabled && !live_; live_ = enabled; update_enabled();
  if (started && connected_ && !refresh_timer_.isActive()) refresh_timer_.start();
}
void CameraControlsWidget::update_enabled() {
  const bool ready = connected_ && pending_.isEmpty() && !awaiting_snapshot_ && !card_->busy();
  for (auto* editor : editors_) editor->setEnabled(ready && editor->property("writable").toBool());
  for (auto* control : exposure_) control->set_ready(ready);
  mouse_af_->setEnabled(ready && available_af_ && live_);
  emit point_selection_changed(mouse_af_->isEnabled() && mouse_af_->isChecked());
}
void CameraControlsWidget::request(const QString& operation, const QString& arguments) {
  if (!connected_ || !pending_.isEmpty() || card_->busy() || (operation != "list" && awaiting_snapshot_)) return;
  if (operation != "list") last_error_.clear();
  operation_ = operation;
  pending_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!session_->send_command("controls " + operation + ' ' + pending_ + (arguments.isEmpty() ? "" : ' ' + arguments))) { pending_.clear(); return; }
  deadline_.start(); update_enabled();
}
void CameraControlsWidget::refresh() {
  dirty_ = true;
  if (!connected_ || !pending_.isEmpty() || card_->busy()) return;
  for (auto* control : exposure_) if (control->editing()) return;
  dirty_ = false; request("list");
}
void CameraControlsWidget::select_point(const QPointF& point) {
  if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || !mouse_af_->isEnabled() || !mouse_af_->isChecked() || point.x() < 0 || point.x() > 1 || point.y() < 0 || point.y() > 1) return;
  request("af", QString::number(std::clamp(qRound(point.x() * 639), 0, 639)) + ' ' + QString::number(std::clamp(qRound(point.y() * 479), 0, 479)));
}
