#include "camera_card_widget.h"
#include "image_viewer.h"
#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

CameraCardWidget::CameraCardWidget(CameraCard* card, QWidget* parent) : QWidget(parent), card_(card) {
  setObjectName("camera_card");
  auto* layout = new QVBoxLayout(this);
  auto* note = new QLabel("Файлы находятся в камере. Просмотр загружает только сжатое превью; оригиналы — кнопками копирования.", this);
  note->setWordWrap(true); layout->addWidget(note);
  auto* tools = new QHBoxLayout;
  auto add = [this, &tools](const QString& label, const QString& name, auto action) {
    auto* button = new QPushButton(label, this); button->setObjectName(name);
    connect(button, &QPushButton::clicked, this, action); tools->addWidget(button); actions_.append(button); return button;
  };
  add("Обновить карту", "card_refresh", [this] { card_->refresh(); });
  search_ = new QLineEdit(this); search_->setPlaceholderText("Имя, дата или слот"); tools->addWidget(search_, 1);
  format_ = new QComboBox(this); format_->setObjectName("card_format");
  format_->addItem("RAW + JPEG / HEIF", "all"); format_->addItem("Только RAW", "raw"); format_->addItem("Только JPEG", "jpeg");
  tools->addWidget(format_); actions_.append(format_); layout->addLayout(tools);
  auto* selection = new QHBoxLayout; tools = selection;
  add("Отметить все", "card_select_all", [this] { select("all"); });
  add("Отметить новые", "card_select_new", [this] { select("new"); });
  add("Снять отметки", "card_select_none", [this] { select("none"); });
  count_ = new QLabel(this); selection->addWidget(count_, 1); layout->addLayout(selection);
  auto* split = new QSplitter(this);
  list_ = new QTreeWidget(split); list_->setObjectName("card_items"); list_->setColumnCount(3);
  list_->setHeaderLabels({"Копия", "Брак", "Кадр на карте"});
  list_->setIconSize({72, 48}); list_->setRootIsDecorated(false); list_->setUniformRowHeights(true);
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  list_->header()->setStretchLastSection(true);
  viewer_ = new ImageViewer(split); viewer_->setObjectName("card_preview");
  viewer_->set_caption("Выбери кадр на карте для просмотра");
  split->addWidget(list_); split->addWidget(viewer_); split->setSizes({560, 550}); layout->addWidget(split, 1);
  auto* operations = new QHBoxLayout; tools = operations;
  add("Копировать отмеченные", "card_copy_selected", [this] { copy(CameraCard::CopyMode::Selected); });
  add("Копировать новые", "card_copy_new", [this] { copy(CameraCard::CopyMode::New); });
  add("Копировать все", "card_copy_all", [this] { copy(CameraCard::CopyMode::All); })->setToolTip(
      "Заново копировать все оригиналы. Существующие файлы сохраняются, новые копии получают суффиксы.");
  add("Удалить брак…", "card_delete", [this] { remove_rejected(); });
  cancel_ = new QPushButton("Остановить", this); cancel_->setObjectName("card_cancel"); cancel_->setEnabled(false);
  operations->addWidget(cancel_); connect(cancel_, &QPushButton::clicked, card_, &CameraCard::cancel);
  layout->addLayout(operations);
  progress_ = new QProgressBar(this); progress_->setRange(0, 100); progress_->setValue(0); layout->addWidget(progress_);
  status_ = new QLabel("Подключи камеру и обнови список карты", this); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); layout->addWidget(status_);
  connect(search_, &QLineEdit::textChanged, this, &CameraCardWidget::filter);
  connect(format_, &QComboBox::currentIndexChanged, this, &CameraCardWidget::rebuild);
  connect(list_, &QTreeWidget::itemSelectionChanged, this, &CameraCardWidget::request_preview);
  connect(list_, &QTreeWidget::itemDoubleClicked, this, [this] { viewer_->toggle_fullscreen(); });
  connect(card_, &CameraCard::catalog_changed, this, &CameraCardWidget::rebuild);
  connect(card_, &CameraCard::status_changed, status_, &QLabel::setText);
  connect(card_, &CameraCard::busy_changed, this, [this](bool busy) {
    for (auto* action : actions_) action->setEnabled(!busy);
    list_->setEnabled(!busy); cancel_->setEnabled(busy);
  });
  connect(card_, &CameraCard::progress_changed, this, [this](int done, int total, int percent) {
    progress_->setValue(total ? (done * 100 + percent) / total : percent);
    progress_->setFormat(total ? QString("%1 / %2 · %p%").arg(done).arg(total) : "%p%");
  });
  connect(card_, &CameraCard::preview_ready, this, [this](const QString& key, const QImage& image, bool) {
    previews_.insert(key, new QImage(image), int((image.sizeInBytes() + 1023) / 1024));
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
      auto* row = list_->topLevelItem(i);
      if (row->data(0, Qt::UserRole).toString() != key) continue;
      row->setIcon(2, QPixmap::fromImage(image.scaled(72, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
      if (row == list_->currentItem()) {
        displayed_ = key;
        viewer_->set_image(image, row->data(2, Qt::UserRole).toString() + " · сжатое превью камеры · 100% относится к превью");
      }
      break;
    }
  });
  rebuild();
}
void CameraCardWidget::leave_fullscreen() { viewer_->leave_fullscreen(); }
void CameraCardWidget::activated() { if (card_->items().isEmpty() && !card_->busy()) card_->refresh(); }
QStringList CameraCardWidget::checked(bool rejected) const {
  QStringList keys;
  for (int i = 0; i < list_->topLevelItemCount(); ++i) {
    auto* row = list_->topLevelItem(i);
    if (row->checkState(rejected ? 1 : 0) == Qt::Checked) keys.append(row->data(0, Qt::UserRole).toString());
  }
  return keys;
}
void CameraCardWidget::rebuild() {
  const auto selected = checked(), rejected = checked(true);
  const auto current = list_->currentItem() ? list_->currentItem()->data(0, Qt::UserRole).toString() : QString();
  QSignalBlocker blocker(list_); list_->clear();
  for (const auto& item : card_->items()) {
    auto* row = new QTreeWidgetItem(list_);
    row->setData(0, Qt::UserRole, item.key());
    row->setCheckState(0, selected.contains(item.key()) ? Qt::Checked : Qt::Unchecked);
    if (item.deletable && !item.protected_file) row->setCheckState(1, rejected.contains(item.key()) ? Qt::Checked : Qt::Unchecked);
    const auto title = item.name() + " · " + item.format();
    row->setData(2, Qt::UserRole, title);
    row->setText(2, title + QString("\nСлот %1 · %2 UTC\n").arg(item.slot).arg(QString(item.stamp).replace('T', ' ')) +
        (card_->is_new(item, format_->currentData().toString()) ? "Новый" : "Скопирован") +
        QString(item.protected_file ? " · защищён" : !item.deletable ? " · удаление недоступно" : ""));
    QStringList paths;
    for (const auto& file : item.files) paths.append(file.path + " · " + QString::number(file.size) + " байт");
    row->setToolTip(2, paths.join('\n'));
    if (auto* image = previews_.object(item.key())) row->setIcon(2, QPixmap::fromImage(image->scaled(72, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    if (item.key() == current) list_->setCurrentItem(row);
  }
  if (card_->items().isEmpty()) {
    previews_.clear(); displayed_.clear(); viewer_->set_image({}, "Выбери кадр на карте для просмотра");
  }
  filter();
}
void CameraCardWidget::filter() {
  int visible = 0;
  for (int i = 0; i < list_->topLevelItemCount(); ++i) {
    auto* row = list_->topLevelItem(i);
    const bool match = row->text(2).contains(search_->text().trimmed(), Qt::CaseInsensitive);
    row->setHidden(!match); if (match) ++visible;
  }
  count_->setText(QString("Показано %1 из %2").arg(visible).arg(list_->topLevelItemCount()));
}
void CameraCardWidget::select(const QString& mode) {
  for (int i = 0; i < list_->topLevelItemCount(); ++i) {
    auto* row = list_->topLevelItem(i);
    if (row->isHidden() && mode != "none") continue;
    bool mark = mode == "all";
    if (mode == "new") for (const auto& item : card_->items()) if (item.key() == row->data(0, Qt::UserRole).toString()) mark = card_->is_new(item, format_->currentData().toString());
    row->setCheckState(0, mark ? Qt::Checked : Qt::Unchecked);
  }
}
void CameraCardWidget::copy(CameraCard::CopyMode mode) {
  card_->copy(checked(), format_->currentData().toString(), mode);
}
void CameraCardWidget::request_preview() {
  auto* row = list_->currentItem(); if (!row) return;
  const auto key = row->data(0, Qt::UserRole).toString();
  if (auto* image = previews_.object(key)) {
    displayed_ = key; viewer_->set_image(*image, row->data(2, Qt::UserRole).toString() + " · сжатое превью камеры · 100% относится к превью");
  } else card_->preview(key);
}
void CameraCardWidget::remove_rejected() {
  const auto rejected = checked(true); if (rejected.isEmpty()) { status_->setText("Сначала отметь брак в отдельном столбце"); return; }
  QStringList names;
  int file_count = 0;
  for (const auto& item : card_->items()) if (rejected.contains(item.key())) {
    names.append(QString("Слот %1 · %2 · %3").arg(item.slot).arg(item.name(), item.format())); file_count += item.files.size();
  }
  QMessageBox confirm(QMessageBox::Warning, "Удалить брак с карты камеры", "Удалить навсегда кадров: " + QString::number(names.size()) +
      " (файлов: " + QString::number(file_count) + ")?\n\nБудут удалены все файлы каждого кадра, включая пару RAW+JPEG. Отменить удаление с карты нельзя. Локальные копии сохранятся.", QMessageBox::NoButton, this);
  confirm.setDetailedText(names.join('\n')); confirm.setTextFormat(Qt::PlainText);
  auto* erase = confirm.addButton("Удалить с карты", QMessageBox::DestructiveRole);
  auto* cancel = confirm.addButton("Отмена", QMessageBox::RejectRole); confirm.setDefaultButton(cancel);
  confirm.exec(); if (confirm.clickedButton() == erase) card_->remove(rejected);
}
