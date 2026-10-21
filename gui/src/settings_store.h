#pragma once
#include <QString>

struct AppSettings {
  QString host = "10.0.1.5";
  QString model = "a6700";
  QString user, password, folder;
  bool remember_credentials = false;
  bool auto_sync = true;
  bool auto_liveview = true;
  bool auto_reconnect = true;
  bool studio_review = false;
  int review_seconds = 3;
  bool sdk_terms_accepted = false;
  QByteArray geometry;
};

class SettingsStore final {
 public:
  explicit SettingsStore(QString path = {});
  AppSettings load() const;
  bool save(const AppSettings& values) const;
  bool forget_credentials() const;
  QString path() const { return path_; }
 private:
  QString path_;
};
