#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QTimer>
class QComboBox;
class QDial;
class QLabel;
class QPushButton;

class ExposureControl final : public QWidget {
  Q_OBJECT
 public:
  explicit ExposureControl(const QString& name, QWidget* parent = nullptr);
  void set_snapshot(const QJsonObject& property);
  void set_ready(bool ready);
  void cancel_edit();
  bool editing() const;
 signals:
  void value_requested(const QString& code, const QString& value);
  void editing_finished();
 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
 private:
  void apply();
  QString normalized(QString text) const;
  QComboBox* editor_;
  QDial* dial_;
  QPushButton* apply_;
  QLabel* error_;
  QTimer settle_;
  QString code_, current_;
  QJsonObject snapshot_;
  bool writable_ = false, ready_ = false, edited_ = false;
};
