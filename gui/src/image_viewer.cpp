#include <QCoreApplication>
#include "image_viewer.h"
#include "video_preview.h"
#include <QSpinBox>
#include <QBoxLayout>
#include <QCloseEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QWheelEvent>
#include <cmath>
#include <algorithm>
#include <QApplication>
#include <QMouseEvent>

ImageView::ImageView(QWidget* parent) : QGraphicsView(parent) {
  auto* canvas = new QGraphicsScene(this);
  setScene(canvas);
  item_ = canvas->addPixmap({});
  setBackgroundBrush(QColor("#151922"));
  setFrameShape(QFrame::NoFrame);
  setDragMode(QGraphicsView::ScrollHandDrag);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorViewCenter);
  setRenderHint(QPainter::SmoothPixmapTransform);
  setMinimumSize(200, 140);
  marks_ = new QLabel(viewport()); marks_->setObjectName("selection_overlay");
  notice_ = new QLabel(viewport()); notice_->setObjectName("action_overlay");
  for (auto* label : {marks_, notice_}) {
    label->setTextFormat(Qt::PlainText); label->setWordWrap(true); label->hide();
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    label->setStyleSheet("QLabel { background: transparent; color: #f4f6f8; border: none; padding: 3px; font-size: 13px; font-weight: normal; }");
    auto* shadow = new QGraphicsDropShadowEffect(label);
    shadow->setBlurRadius(5); shadow->setOffset(0, 1); shadow->setColor(QColor(0, 0, 0, 230));
    label->setGraphicsEffect(shadow);
  }
  notice_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  notice_timer_.setSingleShot(true); notice_timer_.setInterval(2300);
  connect(&notice_timer_, &QTimer::timeout, notice_, &QWidget::hide);
}
void ImageView::position_overlays() {
  if (!marks_ || !notice_) return;
  const int width = std::max(40, std::min(560, viewport()->width() - 32));
  for (auto* label : {marks_, notice_}) {
    label->setMaximumWidth(width); label->setMinimumWidth(0); label->adjustSize(); label->raise();
  }
  marks_->move(16, 16);
  notice_->move(16, std::max(16, viewport()->height() - notice_->height() - 16));
}
void ImageView::set_marks(const QString& text) {
  marks_->setText(text); marks_->setVisible(!text.isEmpty()); position_overlays();
}
void ImageView::show_notice(const QString& text) {
  notice_->setText(text); notice_->show(); position_overlays(); notice_timer_.start();
}
void ImageView::clear_notice() { notice_timer_.stop(); notice_->hide(); }

void ImageView::set_image(const QImage& image) {
  const bool changed_size = item_->pixmap().size() != image.size();
  item_->setPixmap(QPixmap::fromImage(image));
  setSceneRect(item_->boundingRect());
  if (fit_ || changed_size) fit_image();
}

void ImageView::fit_image() {
  fit_ = true;
  if (!item_->pixmap().isNull()) fitInView(item_, Qt::KeepAspectRatio);
  emit zoom_changed();
}
void ImageView::actual_size() {
  fit_ = false;
  resetTransform();
  emit zoom_changed();
}
void ImageView::zoom(double factor) {
  const double next = zoom_factor() * factor;
  if (next < 0.01 || next > 16.0) return;
  fit_ = false;
  scale(factor, factor);
  emit zoom_changed();
}
void ImageView::resizeEvent(QResizeEvent* event) {
  QGraphicsView::resizeEvent(event);
  if (fit_) fit_image();
  position_overlays();
}
void ImageView::wheelEvent(QWheelEvent* event) {
  zoom(std::pow(1.0015, event->angleDelta().y()));
  event->accept();
}
void ImageView::set_point_selection(bool enabled) { select_point_ = enabled; }
QPointF ImageView::image_position(const QPoint& viewport_position) const {
  const auto rect = item_->boundingRect();
  const auto point = item_->mapFromScene(mapToScene(viewport_position));
  if (rect.isEmpty() || !rect.contains(point)) return {-1, -1};
  return {point.x() / rect.width(), point.y() / rect.height()};
}
void ImageView::mousePressEvent(QMouseEvent* event) {
  press_position_ = event->position().toPoint();
  QGraphicsView::mousePressEvent(event);
}
void ImageView::mouseReleaseEvent(QMouseEvent* event) {
  QGraphicsView::mouseReleaseEvent(event);
  if (!select_point_ || event->button() != Qt::LeftButton ||
      (event->position().toPoint() - press_position_).manhattanLength() >= QApplication::startDragDistance()) return;
  const auto point = image_position(event->position().toPoint());
  if (point.x() >= 0) emit point_selected(point);
}

ImageViewer::ImageViewer(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  auto* bar = new QHBoxLayout;
  caption_ = new QLabel(this);
  caption_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  caption_->setTextFormat(Qt::PlainText);
  caption_->setWordWrap(true);
  caption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  layout->addWidget(caption_);
  auto add = [this, bar](const QString& label, auto action) {
    auto* button = new QPushButton(label, this);
    connect(button, &QPushButton::clicked, this, action);
    bar->addWidget(button);
  };
  view_ = new ImageView(this);
  add(QCoreApplication::translate("App", "Вписать"), [this] { view_->fit_image(); });
  add("100%", [this] { view_->actual_size(); });
  add("−", [this] { view_->zoom(1.0 / 1.25); });
  add("+", [this] { view_->zoom(1.25); });
  zoom_label_ = new QLabel(this);
  bar->addWidget(zoom_label_);
  add(QCoreApplication::translate("App", "Полный экран · F11"), [this] { toggle_fullscreen(); });
  layout->addLayout(bar);
  layout->addWidget(view_, 1);
  video_ = new VideoPreview(this);
  connect(video_, &VideoPreview::frame_ready, view_, &ImageView::set_image);
  connect(video_, &VideoPreview::failed, this, [this](const QString& error) { view_->show_notice(error); emit movie_failed(); });
  video_bar_ = new QWidget(this); video_bar_->setObjectName("movie_controls");
  auto* video_layout = new QHBoxLayout(video_bar_); video_layout->setContentsMargins(0, 0, 0, 0);
  auto* play = new QPushButton(QCoreApplication::translate("App", "Воспроизвести"), video_bar_); play->setObjectName("movie_play");
  auto* pause = new QPushButton(QCoreApplication::translate("App", "Пауза"), video_bar_); pause->setObjectName("movie_pause");
  auto* stop = new QPushButton(QCoreApplication::translate("App", "Стоп"), video_bar_); stop->setObjectName("movie_stop");
  auto* seek = new QSpinBox(video_bar_); seek->setRange(0, 86400); seek->setSuffix(QCoreApplication::translate("App", " с"));
  auto* jump = new QPushButton(QCoreApplication::translate("App", "Перейти"), video_bar_);
  video_info_ = new QLabel(QCoreApplication::translate("App", "Видео · без звука"), video_bar_); video_info_->setObjectName("movie_info");
  connect(video_, &VideoPreview::frame_ready, video_info_, [this](const QImage& image) {
    video_info_->setText(QString(QCoreApplication::translate("App", "Видео · %1 × %2 · без звука")).arg(image.width()).arg(image.height()));
  });
  for (auto* widget : QList<QWidget*>{play, pause, stop, seek, jump, video_info_}) video_layout->addWidget(widget);
  connect(play, &QPushButton::clicked, this, &ImageViewer::play_video);
  connect(pause, &QPushButton::clicked, this, [this] { if (remote_video_) emit movie_pause_requested(); else video_->pause_local(true); });
  connect(stop, &QPushButton::clicked, this, [this] { if (remote_video_) emit movie_stop_requested(); else video_->stop(); });
  connect(jump, &QPushButton::clicked, this, [this, seek] {
    if (remote_video_) emit movie_seek_requested(seek->value()); else video_->play_local(video_path_, seek->value());
  });
  video_bar_->hide(); layout->addWidget(video_bar_);
  connect(view_, &ImageView::zoom_changed, this, [this] {
    zoom_label_->setText(QString::number(qRound(view_->zoom_factor() * 100)) + "%");
  });
  for (auto key : {Qt::Key_Left, Qt::Key_PageUp, Qt::Key_Right, Qt::Key_PageDown}) {
    auto* shortcut = new QShortcut(QKeySequence(key), this);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut); shortcut->setEnabled(false);
    navigation_.append(shortcut);
    const int step = key == Qt::Key_Left || key == Qt::Key_PageUp ? -1 : 1;
    connect(shortcut, &QShortcut::activated, this, [this, step] { emit navigate_requested(step); });
  }
  auto* fullscreen = new QShortcut(QKeySequence(Qt::Key_F11), this);
  for (auto key : {Qt::Key_Space, Qt::Key_Delete}) {
    auto* shortcut = new QShortcut(QKeySequence(key), this);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    shortcut->setEnabled(false); shortcut->setAutoRepeat(false); marking_.append(shortcut);
    connect(shortcut, &QShortcut::activated, this, [this, key] { emit mark_requested(key == Qt::Key_Delete); });
  }
  fullscreen->setContext(Qt::WidgetWithChildrenShortcut);
  connect(fullscreen, &QShortcut::activated, this, &ImageViewer::toggle_fullscreen);
  auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  escape->setContext(Qt::WidgetWithChildrenShortcut);
  connect(escape, &QShortcut::activated, this, &ImageViewer::leave_fullscreen);
}
void ImageViewer::play_video() {
  if (remote_video_) emit movie_play_requested();
  else if (video_->running()) video_->pause_local(false);
  else video_->play_local(video_path_);
}
void ImageViewer::stop_video() { video_->stop(); }
void ImageViewer::set_local_video(const QString& path) {
  stop_video(); remote_video_ = false; video_path_ = path; video_bar_->setVisible(!path.isEmpty());
  video_info_->setText(QCoreApplication::translate("App", "Видео · без звука"));
}
void ImageViewer::set_remote_video(bool enabled) {
  stop_video(); remote_video_ = enabled; video_path_.clear(); video_bar_->setVisible(enabled);
  video_info_->setText(QCoreApplication::translate("App", "Видео · без звука"));
}
bool ImageViewer::prepare_stream(const QString& socket_path) { return video_->listen(socket_path); }
void ImageViewer::set_video_controls_enabled(bool enabled) { video_bar_->setEnabled(enabled); }
void ImageViewer::set_navigation_enabled(bool enabled) {
  for (auto* shortcut : navigation_) shortcut->setEnabled(enabled);
}
void ImageViewer::set_marking_enabled(bool enabled) {
  for (auto* shortcut : marking_) shortcut->setEnabled(enabled);
}
void ImageViewer::set_image(const QImage& image, const QString& caption) {
  view_->set_image(image);
  if (!caption.isEmpty()) set_caption(caption);
}
void ImageViewer::set_caption(const QString& caption) {
  caption_->setText(caption);
  caption_->setToolTip(caption);
}
void ImageViewer::toggle_fullscreen() {
  if (isFullScreen()) { leave_fullscreen(); return; }
  was_maximized_ = isMaximized();
  if (!isWindow()) {
    embedded_parent_ = parentWidget();
    auto* host_layout = qobject_cast<QBoxLayout*>(embedded_parent_->layout());
    embedded_index_ = host_layout ? host_layout->indexOf(this) : -1;
    embedded_sizes_.clear();
    if (auto* splitter = qobject_cast<QSplitter*>(embedded_parent_)) {
      embedded_index_ = splitter->indexOf(this); embedded_sizes_ = splitter->sizes();
    }
    if (host_layout) host_layout->removeWidget(this);
    setParent(embedded_parent_, Qt::Window);
    detached_ = true;
  }
  showFullScreen();
  view_->setFocus();
  emit fullscreen_changed(true);
}
void ImageViewer::leave_fullscreen() {
  if (!isFullScreen() && !detached_) return;
  if (detached_) {
    hide();
    setWindowState(Qt::WindowNoState);
    setParent(embedded_parent_, Qt::Widget);
    if (auto* host_layout = qobject_cast<QBoxLayout*>(embedded_parent_->layout()))
      host_layout->insertWidget(embedded_index_, this, 1);
    if (auto* splitter = qobject_cast<QSplitter*>(embedded_parent_)) {
      splitter->insertWidget(embedded_index_, this); splitter->setSizes(embedded_sizes_);
    }
    detached_ = false;
    show();
  } else if (was_maximized_) showMaximized();
  else showNormal();
  emit fullscreen_changed(false);
}
void ImageViewer::closeEvent(QCloseEvent* event) {
  if (detached_) { leave_fullscreen(); event->ignore(); }
  else QWidget::closeEvent(event);
}
