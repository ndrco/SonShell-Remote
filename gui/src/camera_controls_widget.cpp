#include <QCoreApplication>
#include "camera_controls_widget.h"
#include "camera_session.h"
#include "camera_card.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QUuid>
#include <algorithm>
#include <cmath>

CameraControlsWidget::CameraControlsWidget(CameraSession* session, CameraCard* card, QWidget* parent)
    : QGroupBox(QCoreApplication::translate("App", "Параметры камеры · SDK"), parent), session_(session), card_(card) {
  setObjectName("camera_controls");
  auto* layout = new QFormLayout(this);
  layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
  // Stable REMOTE/1 names; numeric SDK codes arrive in the snapshot.
  const QStringList names{QCoreApplication::translate("App", "Режим фокуса"), QCoreApplication::translate("App", "Область AF"), QCoreApplication::translate("App", "Баланс белого"), QCoreApplication::translate("App", "Режим протяжки")};
  const QStringList keys{"focus_mode", "focus_area", "white_balance", "drive_mode"};
  for (int i = 0; i < names.size(); ++i) {
    auto* editor = new QComboBox(this); editor->setObjectName(keys[i]); editor->setMinimumContentsLength(15);
    editor->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    editor->addItem(QCoreApplication::translate("App", "Недоступно")); editor->setEnabled(false); editors_.append(editor); layout->addRow(names[i], editor);
    connect(editor, qOverload<int>(&QComboBox::activated), this, [this, editor](int index) {
      request("set", editor->property("sdk_code").toString() + ' ' + editor->itemData(index).toString());
    });
  }
  mouse_af_ = new QCheckBox(QCoreApplication::translate("App", "Автофокус по клику"), this); mouse_af_->setObjectName("mouse_af");
  mouse_af_->setToolTip(QCoreApplication::translate("App", "Клик выбирает точку и запускает автофокус. Перетаскивание перемещает изображение."));
  layout->addRow(mouse_af_);
  message_ = new QLabel(QCoreApplication::translate("App", "Подключи камеру для чтения доступных параметров"), this);
  message_->setObjectName("controls_message");
  message_->setWordWrap(true); message_->setTextFormat(Qt::PlainText); layout->addRow(message_);
  auto* refresh = new QPushButton(QCoreApplication::translate("App", "Прочитать параметры"), this); layout->addRow(refresh);
  refresh_timer_.setSingleShot(true); refresh_timer_.setInterval(250);
  deadline_.setSingleShot(true); deadline_.setInterval(7000);
  connect(refresh, &QPushButton::clicked, this, &CameraControlsWidget::refresh);
  connect(&refresh_timer_, &QTimer::timeout, this, &CameraControlsWidget::refresh);
  connect(mouse_af_, &QCheckBox::toggled, this, [this] { update_enabled(); });
  connect(&deadline_, &QTimer::timeout, this, [this] {
    pending_.clear(); available_af_ = false;
    for (auto* editor : editors_) editor->setProperty("writable", false);
    message_->setText(QCoreApplication::translate("App", "Камера не ответила. Прочитай параметры ещё раз.")); update_enabled();
  });
  connect(session_, &CameraSession::state_changed, this, [this, refresh](auto state, const QString&) {
    connected_ = state == CameraSession::State::Connected;
    refresh->setEnabled(connected_); pending_.clear(); deadline_.stop(); refresh_timer_.stop();
    available_af_ = false; dirty_ = false; last_error_.clear();
    for (auto* editor : editors_) { editor->clear(); editor->addItem(QCoreApplication::translate("App", "Недоступно")); editor->setProperty("writable", false); }
    message_->setText(connected_ ? QCoreApplication::translate("App", "Читаю возможности камеры…") : QCoreApplication::translate("App", "Подключи камеру для чтения доступных параметров"));
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
      const auto properties = event.value("properties").toArray();
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
          if (index < 0 && property.value("readable").toBool()) { editor->addItem(current, current); index = editor->count() - 1; }
          if (index >= 0) editor->setCurrentIndex(index);
        }
        if (!editor->count()) editor->addItem(QCoreApplication::translate("App", "Недоступно"));
        editor->setToolTip(editor->property("writable").toBool() ? QCoreApplication::translate("App", "Значения предоставлены камерой") : QCoreApplication::translate("App", "SDK не разрешает менять этот параметр в текущем режиме"));
      }
      available_af_ = event.value("af").toBool();
      message_->setText(available_af_ ? QCoreApplication::translate("App", "Клик в Live View выбирает точку и запускает автофокус.") :
          QCoreApplication::translate("App", "Автофокус по клику недоступен: включи режим AF, подвижную область фокуса и Live View."));
      if (!last_error_.isEmpty()) message_->setText(last_error_);
    } else if (type == "error") {
      const auto reason = event.value("message").toString();
      const auto description = reason == "af-outside" ? QCoreApplication::translate("App", "Точка за пределами допустимой области AF") :
          reason == "read-only" ? QCoreApplication::translate("App", "SDK не разрешает менять этот параметр в текущем режиме") :
          reason == "af-unavailable" ? QCoreApplication::translate("App", "Автофокус недоступен в текущем режиме камеры") :
          reason.startsWith("af-press:") ? QString(QCoreApplication::translate("App", "Точка перемещена, но запуск AF отклонён (SDK %1)")).arg(reason.mid(9)) :
          reason.startsWith("af-release:") ? QString(QCoreApplication::translate("App", "Не удалось подтвердить отпускание AF (SDK %1). Переподключи камеру.")).arg(reason.mid(11)) : reason;
      last_error_ = QCoreApplication::translate("App", "Команда отклонена камерой: ") + description;
      message_->setText(last_error_); dirty_ = operation != "list";
    } else {
      message_->setText(operation == "af" ? QCoreApplication::translate("App", "Точка выбрана, команда автофокуса принята камерой.") :
          QCoreApplication::translate("App", "Настройка принята SDK; это не подтверждение резкости.")); dirty_ = true;
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
  const bool ready = connected_ && pending_.isEmpty() && !card_->busy();
  for (auto* editor : editors_) editor->setEnabled(ready && editor->property("writable").toBool());
  mouse_af_->setEnabled(ready && available_af_ && live_);
  emit point_selection_changed(mouse_af_->isEnabled() && mouse_af_->isChecked());
}
void CameraControlsWidget::request(const QString& operation, const QString& arguments) {
  if (!connected_ || !pending_.isEmpty() || card_->busy()) return;
  if (operation != "list") last_error_.clear();
  operation_ = operation;
  pending_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!session_->send_command("controls " + operation + ' ' + pending_ + (arguments.isEmpty() ? "" : ' ' + arguments))) { pending_.clear(); return; }
  deadline_.start(); update_enabled();
}
void CameraControlsWidget::refresh() {
  dirty_ = true;
  if (!connected_ || !pending_.isEmpty() || card_->busy()) return;
  dirty_ = false; request("list");
}
void CameraControlsWidget::select_point(const QPointF& point) {
  if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || !mouse_af_->isEnabled() || !mouse_af_->isChecked() || point.x() < 0 || point.x() > 1 || point.y() < 0 || point.y() > 1) return;
  request("af", QString::number(std::clamp(qRound(point.x() * 639), 0, 639)) + ' ' + QString::number(std::clamp(qRound(point.y() * 479), 0, 479)));
}
