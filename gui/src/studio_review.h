#pragma once
#include "media_reader.h"
#include <QObject>
#include <QTimer>
class StudioReview final : public QObject {
  Q_OBJECT
 public:
  explicit StudioReview(QObject* parent = nullptr);
  void configure(bool enabled, int seconds);
  void present(const MediaResult& result);
  void hold(bool held);
  void cancel();
  bool active() const { return active_; }
  bool held() const { return held_; }
 signals:
  void photo_presented(const MediaResult& result);
  void returned_to_liveview();
 private:
  QTimer timer_;
  int duration_ms_ = 3000;
  bool enabled_ = false, active_ = false, held_ = false;
};
