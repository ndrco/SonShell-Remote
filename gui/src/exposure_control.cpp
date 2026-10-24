#include "exposure_control.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDial>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

ExposureControl::ExposureControl(const QString& name, QWidget* parent) : QWidget(parent) {
  setObjectName(name);
  auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(2);
  auto* row = new QHBoxLayout; layout->addLayout(row);
  dial_ = new QDial(this); dial_->setObjectName("dial"); dial_->setFixedSize(46, 46);
  dial_->setWrapping(false); dial_->setNotchesVisible(true); dial_->setPageStep(1);
  dial_->setToolTip(QCoreApplication::translate("App", "Поверни мышью или колёсиком. Значение применяется после отпускания."));
  editor_ = new QComboBox(this); editor_->setObjectName("value"); editor_->setEditable(true);
  editor_->setInsertPolicy(QComboBox::NoInsert); editor_->setMinimumContentsLength(10);
  editor_->lineEdit()->installEventFilter(this);
  editor_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  editor_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  editor_->setToolTip(QCoreApplication::translate("App", "Введи значение и нажми Enter или «Задать». Список содержит значения камеры."));
  apply_ = new QPushButton(QCoreApplication::translate("App", "Задать"), this); apply_->setObjectName("apply");
  row->addWidget(dial_); row->addWidget(editor_, 1); row->addWidget(apply_);
  error_ = new QLabel(this); error_->setObjectName("validation"); error_->setWordWrap(true);
  error_->setTextFormat(Qt::PlainText); error_->hide(); layout->addWidget(error_);
  settle_.setSingleShot(true); settle_.setInterval(250);
  connect(dial_, &QDial::valueChanged, this, [this](int index) {
    if (!ready_ || !writable_ || index >= editor_->count()) return;
    editor_->setCurrentIndex(index); edited_ = true; error_->hide();
    if (!dial_->isSliderDown()) settle_.start();
  });
  connect(dial_, &QDial::sliderPressed, &settle_, &QTimer::stop);
  connect(dial_, &QDial::sliderReleased, this, [this] { if (edited_) apply(); else emit editing_finished(); });
  connect(&settle_, &QTimer::timeout, this, &ExposureControl::apply);
  connect(editor_, &QComboBox::editTextChanged, this, [this] { edited_ = true; error_->hide(); });
  connect(editor_, qOverload<int>(&QComboBox::activated), this, [this] { apply(); });
  connect(editor_->lineEdit(), &QLineEdit::returnPressed, this, &ExposureControl::apply);
  connect(apply_, &QPushButton::clicked, this, &ExposureControl::apply);
  set_snapshot({});
}

QString ExposureControl::normalized(QString text) const {
  text = text.trimmed().toUpper().remove(' ').replace(',', '.');
  if (objectName() == "iso" && text.startsWith("ISO")) text.remove(0, 3);
  if (objectName() == "aperture") { if (text.startsWith("F/")) text.remove(0, 2); else if (text.startsWith('F')) text.remove(0, 1); }
  if (objectName() == "ev" && text.endsWith("EV")) text.chop(2);
  if (objectName() == "shutter" && (text.endsWith('S') || text.endsWith('"'))) text.chop(1);
  bool ok = false; double value = text.toDouble(&ok);
  if (!ok && objectName() == "shutter" && text.count('/') == 1) {
    bool numerator_ok = false, denominator_ok = false;
    const auto numerator = text.section('/', 0, 0).toDouble(&numerator_ok);
    const auto denominator = text.section('/', 1, 1).toDouble(&denominator_ok);
    ok = numerator_ok && denominator_ok && denominator > 0;
    if (ok) value = numerator / denominator;
  }
  return ok && std::isfinite(value) ? QString::number(value, 'g', 12) : text;
}

void ExposureControl::set_snapshot(const QJsonObject& property) {
  snapshot_ = property;
  settle_.stop(); edited_ = false; error_->hide();
  QSignalBlocker editor_guard(editor_), dial_guard(dial_);
  editor_->clear(); code_ = QString::number(property.value("code").toInt());
  current_ = property.value("current").toString();
  auto choices = property.value("values").toArray();
  // Raw SDK order is not necessarily numeric, particularly signed EV and shutter fractions.
  auto rank = [this](const QJsonValue& choice) {
    const auto raw = choice.toObject().value("value").toString().toULongLong();
    if (objectName() == "ev") return double(qint16(raw));
    if (objectName() == "iso") return (raw & 0xffffff) == 0xffffff ? -1.0 : double(raw & 0xffffff);
    if (objectName() == "shutter") return (raw & 0xffff) ? double(raw >> 16) / (raw & 0xffff) : 1e20;
    return double(raw);
  };
  QList<QJsonValue> sorted; for (const auto& choice : choices) sorted.append(choice);
  std::stable_sort(sorted.begin(), sorted.end(), [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
  for (const auto& entry : sorted) {
    const auto value = entry.toObject(); editor_->addItem(value.value("label").toString(), value.value("value").toString());
  }
  writable_ = property.value("writable").toBool() && !sorted.isEmpty();
  int index = editor_->findData(current_);
  dial_->setRange(0, std::max(0, int(editor_->count()) - 1));
  if (index >= 0) { editor_->setCurrentIndex(index); dial_->setValue(index); }
  else editor_->setEditText(property.value("readable").toBool() ? property.value("current_label").toString(current_) : QCoreApplication::translate("App", "Недоступно"));
  set_ready(ready_);
}

void ExposureControl::set_ready(bool ready) {
  ready_ = ready;
  const bool enabled = ready && writable_;
  editor_->setEnabled(enabled); apply_->setEnabled(enabled); dial_->setEnabled(enabled && editor_->count() > 1);
  if (!enabled) { settle_.stop(); edited_ = false; }
  setToolTip(writable_ ? QString() : QCoreApplication::translate("App", "SDK не разрешает менять этот параметр в текущем режиме"));
}

bool ExposureControl::editing() const { return edited_ || dial_->isSliderDown() || settle_.isActive(); }

void ExposureControl::cancel_edit() { set_snapshot(snapshot_); emit editing_finished(); }

bool ExposureControl::eventFilter(QObject* watched, QEvent* event) {
  if (watched == editor_->lineEdit() && event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
    cancel_edit(); return true;
  }
  return QWidget::eventFilter(watched, event);
}

void ExposureControl::apply() {
  settle_.stop();
  if (!ready_ || !writable_) return;
  const auto input = normalized(editor_->currentText());
  int match = editor_->currentIndex();
  if (match < 0 || editor_->currentText() != editor_->itemText(match)) {
    match = -1;
    for (int i = 0; i < editor_->count(); ++i) if (normalized(editor_->itemText(i)) == input) { match = i; break; }
  }
  if (match < 0) {
    error_->setText(QCoreApplication::translate("App", "Выбери доступное значение из списка камеры.")); error_->show(); return;
  }
  { QSignalBlocker editor_guard(editor_), dial_guard(dial_); editor_->setCurrentIndex(match); editor_->setEditText(editor_->itemText(match)); dial_->setValue(match); }
  edited_ = false; error_->hide();
  const auto value = editor_->itemData(match).toString();
  if (value != current_) emit value_requested(code_, value);
  emit editing_finished();
}
