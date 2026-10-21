#include "studio_review.h"
#include <algorithm>
StudioReview::StudioReview(QObject* parent) : QObject(parent) {
  timer_.setSingleShot(true);
  connect(&timer_, &QTimer::timeout, this, &StudioReview::cancel);
}
void StudioReview::configure(bool enabled, int seconds) {
  enabled_ = enabled; duration_ms_ = std::clamp(seconds, 2, 10) * 1000;
  if (!enabled_) cancel(); else if (active_ && !held_) timer_.start(duration_ms_);
}
void StudioReview::present(const MediaResult& result) {
  if (!enabled_ || result.image.isNull()) return;
  active_ = true;
  emit photo_presented(result);
  if (!held_) timer_.start(duration_ms_);
}
void StudioReview::hold(bool held) {
  held_ = held;
  if (held_) timer_.stop(); else if (active_) timer_.start(duration_ms_);
}
void StudioReview::cancel() {
  timer_.stop(); held_ = false;
  const bool was_active = active_; active_ = false;
  if (was_active) emit returned_to_liveview();
}
