#pragma once
#include <QGraphicsView>
#include <QImage>
#include <QWidget>
#include <QPointer>
#include <QList>
class QGraphicsPixmapItem;
class QLabel;
class QShortcut;

class ImageView final : public QGraphicsView {
  Q_OBJECT
 public:
  explicit ImageView(QWidget* parent = nullptr);
  void set_image(const QImage& image);
  void fit_image();
  void actual_size();
  void zoom(double factor);
  double zoom_factor() const { return transform().m11(); }
  bool is_fit() const { return fit_; }
  void set_point_selection(bool enabled);
  QPointF image_position(const QPoint& viewport_position) const;
 signals:
  void zoom_changed();
  void point_selected(const QPointF& normalized);
 protected:
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
 private:
  QGraphicsPixmapItem* item_;
  bool fit_ = true;
  bool select_point_ = false;
  QPoint press_position_;
};

// Can be moved from its embedded host into a full-screen window and back.
class ImageViewer final : public QWidget {
  Q_OBJECT
 public:
  explicit ImageViewer(QWidget* parent = nullptr);
  void set_image(const QImage& image, const QString& caption = {});
  void set_caption(const QString& caption);
  void toggle_fullscreen();
  void leave_fullscreen();
  void set_navigation_enabled(bool enabled);
  void set_marking_enabled(bool enabled);
  ImageView* view() const { return view_; }
 signals:
  void fullscreen_changed(bool active);
  void navigate_requested(int step);
  void mark_requested(bool rejected);
 protected:
  void closeEvent(QCloseEvent* event) override;
 private:
  ImageView* view_;
  QLabel* caption_;
  QLabel* zoom_label_;
  QPointer<QWidget> embedded_parent_;
  QList<int> embedded_sizes_;
  QList<QShortcut*> navigation_;
  QList<QShortcut*> marking_;
  int embedded_index_ = -1;
  bool detached_ = false;
  bool was_maximized_ = false;
};
