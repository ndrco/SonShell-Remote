#include <QCoreApplication>
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
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QShortcut>
#include <QTimer>
#include <algorithm>

namespace {
QString preview_caption(const QTreeWidgetItem* row) {
  return row->data(2, Qt::UserRole).toString() + (row->data(2, Qt::UserRole + 1).toStringList().contains("video") ?
      QCoreApplication::translate("App", " · video: camera thumbnail") : QCoreApplication::translate("App", " · compressed camera preview · 100% refers to the preview"));
}
class CardCheckDelegate final : public QStyledItemDelegate {
 public:
  explicit CardCheckDelegate(QObject* parent) : QStyledItemDelegate(parent) {}
  static QRect indicator(const QRect& cell) { return QRect(cell.center() - QPoint(11, 11), QSize(22, 22)); }
  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    QStyleOptionViewItem background(option); initStyleOption(&background, index);
    background.features &= ~QStyleOptionViewItem::HasCheckIndicator;
    option.widget->style()->drawControl(QStyle::CE_ItemViewItem, &background, painter, option.widget);
    if (!index.data(Qt::CheckStateRole).isValid()) return;
    painter->save(); painter->setRenderHint(QPainter::Antialiasing);
    const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
    const QColor color(index.column() == 1 ? "#b42318" : "#0869b0");
    painter->setPen(QPen(QColor("#111827"), 2));
    painter->setBrush(checked ? color : QColor("#ffffff"));
    const auto rect = indicator(option.rect).adjusted(1, 1, -1, -1);
    painter->drawRoundedRect(rect, 3, 3);
    if (checked) {
      QPainterPath check; check.moveTo(rect.left() + 4, rect.center().y());
      check.lineTo(rect.left() + 8, rect.bottom() - 4); check.lineTo(rect.right() - 3, rect.top() + 4);
      painter->setPen(QPen(Qt::white, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); painter->drawPath(check);
    }
    painter->restore();
  }
  bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override {
    if (!index.data(Qt::CheckStateRole).isValid() || !(index.flags() & Qt::ItemIsEnabled)) return false;
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseButtonDblClick) {
      const auto* mouse = static_cast<QMouseEvent*>(event);
      if (mouse->button() != Qt::LeftButton || !indicator(option.rect).contains(mouse->position().toPoint())) return false;
      if (event->type() != QEvent::MouseButtonRelease) return true;
    } else return false; // Keyboard marking always targets the named column.
    return model->setData(index, index.data(Qt::CheckStateRole).toInt() == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
  }
  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    return QStyledItemDelegate::sizeHint(option, index).expandedTo(QSize(32, 28));
  }
};
}

CameraCardWidget::CameraCardWidget(CameraCard* card, QWidget* parent) : QWidget(parent), card_(card) {
  setObjectName("camera_card");
  auto* layout = new QVBoxLayout(this);
  auto* note = new QLabel(QCoreApplication::translate("App", "Photos: original JPEG or quick preview. Video: Play downloads the original; the button shows its size. SDK streaming is in Settings. Viewing does not replace importing."), this);
  note->setWordWrap(true); layout->addWidget(note);
  auto* tools = new QHBoxLayout;
  auto add = [this, &tools](const QString& label, const QString& name, auto action) {
    auto* button = new QPushButton(label, this); button->setObjectName(name);
    connect(button, &QPushButton::clicked, this, action); tools->addWidget(button); actions_.append(button); return button;
  };
  add(QCoreApplication::translate("App", "Refresh card"), "card_refresh", [this] { card_->refresh(); });
  search_ = new QLineEdit(this); search_->setPlaceholderText(QCoreApplication::translate("App", "Name, date or slot")); tools->addWidget(search_, 1);
  format_ = new QComboBox(this); format_->setObjectName("card_format");
  format_->addItem(QCoreApplication::translate("App", "Photos and videos"), "all");
  format_->addItem(QCoreApplication::translate("App", "Photos only"), "photo");
  format_->addItem(QCoreApplication::translate("App", "Video only"), "video"); format_->addItem(QCoreApplication::translate("App", "RAW only"), "raw"); format_->addItem(QCoreApplication::translate("App", "JPEG only"), "jpeg");
  tools->addWidget(format_); actions_.append(format_); layout->addLayout(tools);
  auto* selection = new QHBoxLayout; tools = selection;
  add(QCoreApplication::translate("App", "Mark all"), "card_select_all", [this] { select("all"); });
  add(QCoreApplication::translate("App", "Mark new"), "card_select_new", [this] { select("new"); });
  add(QCoreApplication::translate("App", "Clear marks"), "card_select_none", [this] { select("none"); });
  count_ = new QLabel(this); selection->addWidget(count_, 1); layout->addLayout(selection);
  auto* split = new QSplitter(this);
  list_ = new QTreeWidget(split); list_->setObjectName("card_items"); list_->setColumnCount(3);
  list_->setHeaderLabels({QCoreApplication::translate("App", "Copy"), QCoreApplication::translate("App", "Reject"), QCoreApplication::translate("App", "Photo / video on card")});
  list_->setIconSize({72, 48}); list_->setRootIsDecorated(false); list_->setUniformRowHeights(true);
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  auto* checks = new CardCheckDelegate(list_);
  list_->setItemDelegateForColumn(0, checks); list_->setItemDelegateForColumn(1, checks);
  for (auto key : {Qt::Key_Left, Qt::Key_PageUp, Qt::Key_Right, Qt::Key_PageDown}) {
    auto* shortcut = new QShortcut(QKeySequence(key), list_); shortcut->setContext(Qt::WidgetShortcut);
    connect(shortcut, &QShortcut::activated, this, [this, key] { navigate(key == Qt::Key_Left || key == Qt::Key_PageUp ? -1 : 1); });
  }
  list_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  for (auto key : {Qt::Key_Space, Qt::Key_Delete}) {
    auto* shortcut = new QShortcut(QKeySequence(key), list_);
    shortcut->setContext(Qt::WidgetShortcut); shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this, key] { toggle_mark(key == Qt::Key_Delete); });
  }
  list_->header()->setStretchLastSection(true);
  viewer_ = new ImageViewer(split); viewer_->setObjectName("card_preview");
  auto* preview_tools = new QWidget(viewer_);
  auto* preview_box = new QVBoxLayout(preview_tools); preview_box->setContentsMargins(0, 0, 0, 0);
  auto* preview_layout = new QHBoxLayout; preview_layout->setContentsMargins(0, 0, 0, 0);
  preview_box->addLayout(preview_layout);
  quality_ = new QComboBox(preview_tools); quality_->setObjectName("card_quality");
  quality_->addItem(QCoreApplication::translate("App", "Photos: original JPEG"), "jpeg");
  quality_->addItem(QCoreApplication::translate("App", "Photos: quick preview"), "quick");
  video_quality_ = new QComboBox(preview_tools); video_quality_->setObjectName("card_video_quality");
  video_quality_->addItem(QCoreApplication::translate("App", "Video: original on Play"), "original");
  video_quality_->addItem(QCoreApplication::translate("App", "Video: SDK stream"), "sdk");
  original_ = new QPushButton(QCoreApplication::translate("App", "Open original"), preview_tools); original_->setObjectName("card_original");
  original_->setToolTip(QCoreApplication::translate("App", "Load the selected file into a temporary cache. Video keeps its original quality; RAW uses its best embedded JPEG. Reopening reuses the cache or an imported copy."));
  preview_cancel_ = new QPushButton(QCoreApplication::translate("App", "Stop transfer"), preview_tools); preview_cancel_->setObjectName("card_preview_cancel"); preview_cancel_->setEnabled(false);
  preview_status_ = new QLabel(preview_tools); preview_status_->setObjectName("card_preview_status");
  preview_layout->addWidget(quality_); preview_layout->addWidget(video_quality_); preview_layout->addWidget(original_); preview_layout->addWidget(preview_cancel_); preview_layout->addWidget(preview_status_);
  preview_progress_ = new QProgressBar(preview_tools); preview_progress_->setObjectName("card_preview_progress");
  preview_progress_->setRange(0, 100); preview_progress_->setValue(0); preview_progress_->setTextVisible(true);
  preview_progress_->hide(); preview_box->addWidget(preview_progress_);
  qobject_cast<QVBoxLayout*>(viewer_->layout())->insertWidget(1, preview_tools);
  connect(quality_, &QComboBox::currentIndexChanged, this, [this] { original_displayed_.clear(); request_preview(); });
  connect(video_quality_, &QComboBox::currentIndexChanged, this, [this] { autoplay_original_.clear(); request_preview(); });
  connect(original_, &QPushButton::clicked, this, &CameraCardWidget::request_original);
  auto cancel_preview = [this] { pending_preview_.clear(); pending_original_ = false; autoplay_original_.clear(); card_->cancel(); };
  connect(preview_cancel_, &QPushButton::clicked, this, cancel_preview);
  connect(viewer_, &ImageViewer::movie_play_requested, this, &CameraCardWidget::play_movie);
  connect(viewer_, &ImageViewer::movie_pause_requested, this, [this] { card_->movie_control("pause"); });
  connect(viewer_, &ImageViewer::movie_seek_requested, this, [this](int seconds) { card_->movie_control("seek", seconds); });
  connect(viewer_, &ImageViewer::movie_stop_requested, this, [this] { if (card_->playing_video()) card_->cancel(); });
  connect(viewer_, &ImageViewer::movie_failed, this, [this] { if (card_->playing_video()) card_->cancel(); });
  connect(card_, &CameraCard::movie_failed, this, [this](const QString& message) { viewer_->stop_video(); viewer_->view()->show_notice(message); });
  connect(card_, &CameraCard::movie_state, this, [this](const QString& state) {
    if (auto* row = list_->currentItem()) viewer_->set_caption(row->data(2, Qt::UserRole).toString() + QCoreApplication::translate("App", " · streaming preview from camera"));
    viewer_->view()->show_notice(state == "paused" ? QCoreApplication::translate("App", "Video: paused") :
        state == "preparing" ? QCoreApplication::translate("App", "Preparing video stream…") : QCoreApplication::translate("App", "Video: playing"));
  });
  viewer_->set_navigation_enabled(true);
  viewer_->set_marking_enabled(true);
  connect(viewer_, &ImageViewer::mark_requested, this, &CameraCardWidget::toggle_mark);
  viewer_->setToolTip(QCoreApplication::translate("App", "Previous/next photo: ←/→ or Page Up/Page Down, including full screen"));
  connect(viewer_, &ImageViewer::navigate_requested, this, &CameraCardWidget::navigate);
  viewer_->set_caption(QCoreApplication::translate("App", "Select a photo on the card · ←/→ to browse"));
  split->addWidget(list_); split->addWidget(viewer_); split->setSizes({560, 550}); layout->addWidget(split, 1);
  auto* operations = new QHBoxLayout; tools = operations;
  const auto plan_tip = QCoreApplication::translate("App", "A new copy queue replaces the saved plan.");
  add(QCoreApplication::translate("App", "Copy marked"), "card_copy_selected", [this] { copy(CameraCard::CopyMode::Selected); })->setToolTip(plan_tip);
  add(QCoreApplication::translate("App", "Copy new"), "card_copy_new", [this] { copy(CameraCard::CopyMode::New); })->setToolTip(plan_tip);
  add(QCoreApplication::translate("App", "Copy all"), "card_copy_all", [this] { copy(CameraCard::CopyMode::All); })->setToolTip(
      QCoreApplication::translate("App", "Copy all originals again. Existing files are preserved; new copies receive suffixes.") + '\n' + plan_tip);
  add(QCoreApplication::translate("App", "Delete rejects…"), "card_delete", [this] { remove_rejected(); });
  cancel_ = new QPushButton(QCoreApplication::translate("App", "Stop transfer"), this); cancel_->setObjectName("card_cancel"); cancel_->setEnabled(false);
  operations->addWidget(cancel_); connect(cancel_, &QPushButton::clicked, this, cancel_preview);
  layout->addLayout(operations);
  auto* recovery = new QHBoxLayout;
  resume_ = new QPushButton(QCoreApplication::translate("App", "Resume import"), this); resume_->setObjectName("card_resume");
  resume_->setToolTip(QCoreApplication::translate("App", "Resume the saved queue. An incomplete file is transferred again from the beginning."));
  verify_ = new QPushButton(QCoreApplication::translate("App", "Verify local copies · SHA256"), this); verify_->setObjectName("card_verify");
  verify_->setToolTip(QCoreApplication::translate("App", "Compare contents with the checksum recorded at import. No camera is needed; files are not modified."));
  recovery->addWidget(resume_); recovery->addWidget(verify_); recovery->addStretch(); layout->addLayout(recovery);
  connect(resume_, &QPushButton::clicked, card_, &CameraCard::resume_copy);
  connect(verify_, &QPushButton::clicked, card_, &CameraCard::verify_copies);
  progress_ = new QProgressBar(this); progress_->setRange(0, 100); progress_->setValue(0); layout->addWidget(progress_);
  status_ = new QLabel(QCoreApplication::translate("App", "Connect a camera and refresh the card list"), this); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); layout->addWidget(status_);
  connect(search_, &QLineEdit::textChanged, this, &CameraCardWidget::filter);
  connect(format_, &QComboBox::currentIndexChanged, this, &CameraCardWidget::rebuild);
  connect(list_, &QTreeWidget::itemSelectionChanged, this, [this] {
    emit selected_path_changed();
    autoplay_original_.clear(); viewer_->view()->clear_notice(); update_marks(); request_preview();
  });
  connect(list_, &QTreeWidget::itemChanged, this, [this] { update_marks(); });
  connect(list_, &QTreeWidget::itemDoubleClicked, this, [this] { viewer_->toggle_fullscreen(); });
  connect(card_, &CameraCard::catalog_changed, this, &CameraCardWidget::rebuild);
  connect(card_, &CameraCard::item_status_changed, this, [this](const QString& key) {
    auto* row = rows_.value(key); if (!row) return;
    for (const auto& item : card_->items()) if (item.key() == key) { row->setText(2, item_caption(item)); break; }
    filter(); update_actions();
  });
  connect(card_, &CameraCard::status_changed, status_, &QLabel::setText);
  connect(card_, &CameraCard::status_changed, preview_status_, &QLabel::setToolTip);
  connect(card_, &CameraCard::busy_changed, this, [this](bool busy) {
    update_actions();
    list_->setEnabled(!busy || card_->previewing()); cancel_->setEnabled(busy);
    preview_cancel_->setEnabled(busy);
    preview_progress_->setVisible(busy && !card_->playing_video());
    if (busy) { preview_progress_->setRange(0, 0); preview_progress_->setFormat("%p%"); }
    if (!busy) preview_status_->clear();
    viewer_->set_video_controls_enabled(!busy || card_->playing_video());
    if (!busy && !pending_preview_.isEmpty()) QTimer::singleShot(0, this, &CameraCardWidget::request_preview);
  });
  connect(card_, &CameraCard::progress_changed, this, [this](int done, int total, int percent) {
    percent = std::clamp(percent, 0, 100);
    const int value = total > 0 ? int((qint64(std::clamp(done, 0, total)) * 100 + (done < total ? percent : 0)) / total) : percent;
    progress_->setValue(value); preview_progress_->setRange(0, 100); preview_progress_->setValue(value);
    if (card_->busy()) preview_status_->setText(QString::number(value) + "%");
    const auto format = total ? QString("%1 / %2 · %p%").arg(done).arg(total) : "%p%";
    progress_->setFormat(format); preview_progress_->setFormat(format);
  });
  connect(card_, &CameraCard::preview_ready, this, [this](const QString& key, const QImage& image, bool) {
    previews_.insert(key, new QImage(image), int((image.sizeInBytes() + 1023) / 1024));
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
      auto* row = list_->topLevelItem(i);
      if (row->data(0, Qt::UserRole).toString() != key) continue;
      row->setIcon(2, QPixmap::fromImage(image.scaled(72, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
      if (row == list_->currentItem() && original_displayed_ != key) {
        displayed_ = key;
        viewer_->set_image(image, preview_caption(row));
      }
      break;
    }
  });
  connect(card_, &CameraCard::original_ready, this, [this](const QString& key, const MediaResult& result) {
    auto* row = rows_.value(key);
    if (!row || row != list_->currentItem() || !pending_preview_.isEmpty()) return;
    original_displayed_ = displayed_ = key;
    row->setIcon(2, QPixmap::fromImage(result.image.scaled(72, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    if (result.video) viewer_->set_local_video(result.path); else viewer_->set_remote_video(false);
    const auto source = result.video ? QCoreApplication::translate("App", "Video · original") +
        QString(" · %1 × %2").arg(result.image.width()).arg(result.image.height()) : result.source;
    viewer_->set_image(result.image, row->data(2, Qt::UserRole).toString() + " · " + source);
    viewer_->view()->show_notice(source);
    if (result.video && autoplay_original_ == key) QTimer::singleShot(0, this, [this, key] {
      if (autoplay_original_ != key || original_displayed_ != key || card_->busy()) return;
      autoplay_original_.clear(); viewer_->play_video();
    });
  });
  connect(card_, &CameraCard::original_failed, this, [this](const QString& key, const QString& message) {
    const auto* row = rows_.value(key);
    if (!row || row != list_->currentItem() || !pending_preview_.isEmpty()) return;
    autoplay_original_.clear();
    viewer_->set_caption(row->data(2, Qt::UserRole).toString() + " · " + message);
    viewer_->view()->show_notice(message);
  });
  rebuild();
}
void CameraCardWidget::update_actions() {
  const bool busy = card_->busy();
  for (auto* action : actions_) action->setEnabled(!busy && card_->connected());
  resume_->setEnabled(!busy && card_->connected() && card_->pending_copies() > 0);
  resume_->setText(QCoreApplication::translate("App", "Resume import") + (card_->pending_copies() ? QString(" (%1)").arg(card_->pending_copies()) : ""));
  verify_->setEnabled(!busy && card_->has_imports());
  const auto* row = list_->currentItem();
  original_->setEnabled(!busy && card_->connected() && row && !row->isHidden());
  quality_->setEnabled(!busy || card_->previewing()); video_quality_->setEnabled(!busy || card_->previewing());
  if (row) for (const auto& item : card_->items()) if (item.key() == row->data(0, Qt::UserRole).toString()) {
    original_->setText(QString(QCoreApplication::translate("App", "Original · %1 MiB")).arg(item.preview_file().size / 1024.0 / 1024, 0, 'f', 1)); break;
  }
}
void CameraCardWidget::leave_fullscreen() { viewer_->leave_fullscreen(); }
QString CameraCardWidget::selected_path() const {
  const auto* row = list_->currentItem();
  if (!row || row->isHidden()) return {};
  const auto key = row->data(0, Qt::UserRole).toString();
  for (const auto& item : card_->items()) if (item.key() == key) return item.preview_file().path;
  return {};
}
void CameraCardWidget::activated() { if (card_->items().isEmpty() && !card_->busy()) card_->refresh(); }
void CameraCardWidget::deactivated() {
  viewer_->stop_video(); pending_preview_.clear(); pending_original_ = false; autoplay_original_.clear();
  if (card_->playing_video() || card_->loading_original()) card_->cancel();
}
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
  QSignalBlocker blocker(list_); rows_.clear(); list_->clear();
  for (const auto& item : card_->items()) {
    auto* row = new QTreeWidgetItem(list_);
    rows_.insert(item.key(), row);
    row->setData(0, Qt::UserRole, item.key());
    row->setCheckState(0, selected.contains(item.key()) ? Qt::Checked : Qt::Unchecked);
    if (item.deletable && !item.protected_file) row->setCheckState(1, rejected.contains(item.key()) ? Qt::Checked : Qt::Unchecked);
    const auto title = item.name() + " · " + item.format();
    row->setData(2, Qt::UserRole, title);
    QStringList formats;
    for (const auto& file : item.files) {
      if (file.format == 0xb982) formats << "video";
      if (file.format == 0xb101) formats << "raw" << "photo";
      if (file.format == 0x3801) formats << "jpeg" << "photo";
      if (file.format == 0xb110) formats << "photo";
    }
    row->setData(2, Qt::UserRole + 1, formats);
    row->setText(2, item_caption(item));
    QStringList paths;
    for (const auto& file : item.files) paths.append(file.path + " · " + QString::number(file.size) + QCoreApplication::translate("App", " bytes"));
    row->setToolTip(2, paths.join('\n'));
    if (auto* image = previews_.object(item.key())) row->setIcon(2, QPixmap::fromImage(image->scaled(72, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    if (item.key() == current) list_->setCurrentItem(row);
  }
  if (card_->items().isEmpty()) {
    previews_.clear(); displayed_.clear(); original_displayed_.clear(); pending_preview_.clear(); pending_original_ = false; viewer_->set_remote_video(false); viewer_->set_image({}, QCoreApplication::translate("App", "Select a photo on the card"));
  }
  filter();
  update_actions();
}
QString CameraCardWidget::item_caption(const CardItem& item) const {
  return item.name() + " · " + item.format() + QString(QCoreApplication::translate("App", "\nSlot %1 · %2 UTC\n")).arg(item.slot).arg(QString(item.stamp).replace('T', ' ')) +
      (card_->is_new(item, format_->currentData().toString()) ? QCoreApplication::translate("App", "New") : QCoreApplication::translate("App", "Copied")) +
      QString(item.protected_file ? QCoreApplication::translate("App", " · protected") : !item.deletable ? QCoreApplication::translate("App", " · deletion unavailable") : "");
}
void CameraCardWidget::filter() {
  int visible = 0;
  for (int i = 0; i < list_->topLevelItemCount(); ++i) {
    auto* row = list_->topLevelItem(i);
    const bool match = row->text(2).contains(search_->text().trimmed(), Qt::CaseInsensitive) &&
        (format_->currentData() == "all" || row->data(2, Qt::UserRole + 1).toStringList().contains(format_->currentData().toString()));
    row->setHidden(!match); if (match) ++visible;
  }
  count_->setText(QString(QCoreApplication::translate("App", "Showing %1 of %2")).arg(visible).arg(list_->topLevelItemCount()));
  update_marks();
  emit selected_path_changed();
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
  original_displayed_.clear(); viewer_->stop_video(); update_actions();
  if (card_->playing_video() || card_->loading_original()) { pending_preview_ = key; card_->cancel(); return; }
  const bool video = row->data(2, Qt::UserRole + 1).toStringList().contains("video");
  viewer_->set_remote_video(video);
  if (pending_original_ || (!video && quality_->currentData() == "jpeg" && row->data(2, Qt::UserRole + 1).toStringList().contains("jpeg"))) {
    if (card_->busy()) { if (card_->previewing()) { pending_preview_ = key; card_->cancel(); } return; }
    pending_preview_.clear(); pending_original_ = false;
    viewer_->set_image({}, row->data(2, Qt::UserRole).toString() + QCoreApplication::translate("App", " · loading original…"));
    card_->original(key); return;
  }
  if (auto* image = previews_.object(key)) {
    pending_preview_.clear(); displayed_ = key; viewer_->set_image(*image, preview_caption(row));
  } else {
    if (card_->busy()) { if (card_->previewing()) pending_preview_ = key; return; }
    pending_preview_.clear();
    viewer_->set_image({}, row->data(2, Qt::UserRole).toString() + QCoreApplication::translate("App", " · loading preview…"));
    card_->preview(key);
  }
}
void CameraCardWidget::request_original(bool autoplay) {
  const auto* row = list_->currentItem();
  if (!row || row->isHidden() || card_->busy()) return;
  autoplay_original_ = autoplay ? row->data(0, Qt::UserRole).toString() : QString();
  pending_original_ = true; request_preview();
}
void CameraCardWidget::play_movie() {
  if (card_->playing_video()) { card_->movie_control("play"); return; }
  auto* row = list_->currentItem();
  if (!row || row->isHidden() || card_->busy() || !row->data(2, Qt::UserRole + 1).toStringList().contains("video")) return;
  if (video_quality_->currentData() == "original") { request_original(true); return; }
  const auto socket = card_->movie_socket_path();
  if (viewer_->prepare_stream(socket)) card_->play_movie(row->data(0, Qt::UserRole).toString(), socket);
}
void CameraCardWidget::navigate(int step) {
  if (card_->busy() && !card_->previewing()) return;
  auto* current = list_->currentItem();
  int row = current && !current->isHidden() ? list_->indexOfTopLevelItem(current) + step : step > 0 ? 0 : list_->topLevelItemCount() - 1;
  for (; row >= 0 && row < list_->topLevelItemCount(); row += step) {
    auto* item = list_->topLevelItem(row);
    if (!item->isHidden()) { list_->setCurrentItem(item); list_->scrollToItem(item); return; }
  }
}
void CameraCardWidget::update_marks() {
  const auto* row = list_->currentItem();
  if (!row || row->isHidden()) { viewer_->view()->set_marks({}); return; }
  const auto copy = row->checkState(0) == Qt::Checked ? QCoreApplication::translate("App", "COPY: marked") : QCoreApplication::translate("App", "COPY: no");
  const auto reject = !row->data(1, Qt::CheckStateRole).isValid() ? QCoreApplication::translate("App", "REJECT: deletion unavailable") :
      row->checkState(1) == Qt::Checked ? QCoreApplication::translate("App", "REJECT: marked") : QCoreApplication::translate("App", "REJECT: no");
  viewer_->view()->set_marks(row->data(2, Qt::UserRole).toString() + "\n" + copy + "  ·  " + reject);
}
void CameraCardWidget::toggle_mark(bool rejected) {
  if (!isEnabled() || (card_->busy() && !card_->previewing())) {
    viewer_->view()->show_notice(QCoreApplication::translate("App", "Marking is unavailable during this operation")); return;
  }
  auto* row = list_->currentItem();
  const int column = rejected ? 1 : 0;
  if (!row || row->isHidden()) return;
  if (!row->data(column, Qt::CheckStateRole).isValid()) {
    viewer_->view()->show_notice(QCoreApplication::translate("App", "Cannot mark reject: file protected or deletion unavailable")); return;
  }
  const bool mark = row->checkState(column) != Qt::Checked;
  row->setCheckState(column, mark ? Qt::Checked : Qt::Unchecked);
  viewer_->view()->show_notice(rejected ? (mark ? QCoreApplication::translate("App", "Marked as reject · Delete to clear") : QCoreApplication::translate("App", "Reject mark cleared")) :
      (mark ? QCoreApplication::translate("App", "Marked for copying · Space to clear") : QCoreApplication::translate("App", "Copy mark cleared")));
}

void CameraCardWidget::remove_rejected() {
  const auto rejected = checked(true); if (rejected.isEmpty()) { status_->setText(QCoreApplication::translate("App", "First mark rejects in the separate column")); return; }
  QStringList names;
  int file_count = 0;
  for (const auto& item : card_->items()) if (rejected.contains(item.key())) {
    names.append(QString(QCoreApplication::translate("App", "Slot %1 · %2 · %3")).arg(item.slot).arg(item.name(), item.format())); file_count += item.files.size();
  }
  QMessageBox confirm(QMessageBox::Warning, QCoreApplication::translate("App", "Delete marked contents from camera card"), QCoreApplication::translate("App", "Permanently delete items: ") + QString::number(names.size()) +
      QCoreApplication::translate("App", " (files: ") + QString::number(file_count) + QCoreApplication::translate("App", ")?\n\nPhotos and clips will be deleted in full, including RAW+JPEG, video, proxies and related files. Card deletion cannot be undone. Local copies will be preserved."), QMessageBox::NoButton, this);
  confirm.setDetailedText(names.join('\n')); confirm.setTextFormat(Qt::PlainText);
  auto* erase = confirm.addButton(QCoreApplication::translate("App", "Delete from card"), QMessageBox::DestructiveRole);
  auto* cancel = confirm.addButton(QCoreApplication::translate("App", "Cancel"), QMessageBox::RejectRole); confirm.setDefaultButton(cancel);
  confirm.exec(); if (confirm.clickedButton() == erase) card_->remove(rejected);
}
