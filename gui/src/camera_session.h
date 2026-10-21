#pragma once
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QJsonObject>

struct ConnectionOptions {
  QString binary, host, model, user, password, folder;
  bool auto_reconnect = true;
};

class CameraSession final : public QObject {
  Q_OBJECT
 public:
  enum class State { Disconnected, Connecting, Connected, RetryWaiting, Stopping, Error };
  Q_ENUM(State)
  explicit CameraSession(QObject* parent = nullptr);
  ~CameraSession() override;
  void start(const ConnectionOptions& options);
  void stop();
  bool send_command(const QString& command);
  void set_auto_reconnect(bool enabled);
  State state() const { return state_; }
  bool active() const { return desired_ || process_.state() != QProcess::NotRunning; }
  QString liveview_path() const;
  QString card_cache_path() const;
  void set_retry_interval(int ms) { retry_base_ms_ = ms; }
 signals:
  void state_changed(CameraSession::State state, const QString& message);
  void connected();
  void log_received(const QString& line);
  void liveview_changed(bool running);
  void download_completed(const QString& path, bool new_capture);
  void transfer_progress(const QString& path, int percent);
  void card_event(const QJsonObject& event);
  void controls_event(const QJsonObject& event);
  void controls_changed();
 private:
  void launch();
  void consume_output();
  void handle_line(const QByteArray& line);
  void process_finished();
  void set_state(State state, const QString& message);
  void finish_or_retry();
  void terminate_process();
  QProcess process_;
  QTemporaryDir frames_;
  QTimer retry_, connection_timeout_, terminate_timeout_, kill_timeout_;
  ConnectionOptions options_;
  QByteArray pending_;
  State state_ = State::Disconnected;
  bool desired_ = false;
  int attempts_ = 0;
  int retry_base_ms_ = 3000;
};
