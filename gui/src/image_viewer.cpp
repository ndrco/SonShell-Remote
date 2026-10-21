#include "image_viewer.h"
#include <QBoxLayout>
#include <QCloseEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QWheelEvent>
#include <cmath>

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
}

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
}
void ImageView::wheelEvent(QWheelEvent* event) {
  zoom(std::pow(1.0015, event->angleDelta().y()));
  event->accept();
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
  add("Вписать", [this] { view_->fit_image(); });
  add("100%", [this] { view_->actual_size(); });
  add("−", [this] { view_->zoom(1.0 / 1.25); });
  add("+", [this] { view_->zoom(1.25); });
  zoom_label_ = new QLabel(this);
  bar->addWidget(zoom_label_);
  add("Полный экран · F11", [this] { toggle_fullscreen(); });
  layout->addLayout(bar);
  layout->addWidget(view_, 1);
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
  fullscreen->setContext(Qt::WidgetWithChildrenShortcut);
  connect(fullscreen, &QShortcut::activated, this, &ImageViewer::toggle_fullscreen);
  auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  escape->setContext(Qt::WidgetWithChildrenShortcut);
  connect(escape, &QShortcut::activated, this, &ImageViewer::leave_fullscreen);
}
void ImageViewer::set_navigation_enabled(bool enabled) {
  for (auto* shortcut : navigation_) shortcut->setEnabled(enabled);
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
