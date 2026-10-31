#include <QCoreApplication>
#include "settings_store.h"
#include <utility>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

SettingsStore::SettingsStore(QString path) : path_(std::move(path)) {
  if (path_.isEmpty()) path_ = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
      + "/SonShellRemote/settings.ini";
}

AppSettings SettingsStore::load() const {
  QSettings settings(path_, QSettings::IniFormat);
  AppSettings v;
  v.host = settings.value("host", v.host).toString();
  v.model = settings.value("model", v.model).toString();
  v.language = settings.value("language", "en").toString();
  if (v.language != "ru" && v.language != "en") v.language = "en";
  v.folder = settings.value("folder", QDir::homePath() + "/Pictures/Sony").toString();
  v.remember_credentials = settings.value("remember_credentials", false).toBool();
  if (v.remember_credentials) {
    v.user = settings.value("credentials/user").toString();
    v.password = settings.value("credentials/password").toString();
  }
  v.auto_sync = false; // Do not restore the old automatic original transfer.
  v.auto_liveview = settings.value("auto_liveview", true).toBool();
  v.auto_connect = settings.value("auto_connect", true).toBool();
  v.auto_reconnect = settings.value("auto_reconnect", true).toBool();
  v.card_original_video = settings.value("preview/card_original_video", true).toBool();
  v.card_full_jpeg = settings.value("preview/card_full_jpeg", true).toBool();
  v.studio_review = settings.value("studio/enabled", false).toBool();
  v.studio_full_jpeg = settings.value("studio/full_jpeg", false).toBool();
  v.review_seconds = std::clamp(settings.value("studio/seconds", 3).toInt(), 2, 10);
  v.sdk_terms_accepted = settings.value("sdk_terms/2026-10", false).toBool();
  v.geometry = settings.value("geometry").toByteArray();
  return v;
}

bool SettingsStore::save(const AppSettings& v) const {
  const QString directory = QFileInfo(path_).absolutePath();
  if (!QDir().mkpath(directory) ||
      !QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
    return false;
  // Create privately before QSettings writes any secret, including the first save.
  if (!QFileInfo::exists(path_)) {
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) || !file.commit()) return false;
  }
  if (!QFile::setPermissions(path_, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) return false;
  QSettings settings(path_, QSettings::IniFormat);
  settings.setValue("host", v.host);
  settings.setValue("model", v.model);
  settings.setValue("language", v.language == "ru" ? "ru" : "en");
  settings.setValue("folder", v.folder);
  settings.setValue("remember_credentials", v.remember_credentials);
  settings.remove("credentials");
  if (v.remember_credentials) {
    settings.setValue("credentials/user", v.user);
    settings.setValue("credentials/password", v.password);
  }
  settings.remove("auto_sync");
  settings.setValue("auto_liveview", v.auto_liveview);
  settings.setValue("auto_connect", v.auto_connect);
  settings.setValue("auto_reconnect", v.auto_reconnect);
  settings.setValue("preview/card_original_video", v.card_original_video);
  settings.setValue("preview/card_full_jpeg", v.card_full_jpeg);
  settings.setValue("studio/enabled", v.studio_review);
  settings.setValue("studio/full_jpeg", v.studio_full_jpeg);
  settings.setValue("studio/seconds", std::clamp(v.review_seconds, 2, 10));
  settings.setValue("geometry", v.geometry);
  settings.setValue("sdk_terms/2026-10", v.sdk_terms_accepted);
  settings.sync();
  return settings.status() == QSettings::NoError &&
      QFile::setPermissions(path_, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

bool SettingsStore::forget_credentials() const {
  auto values = load();
  values.remember_credentials = false;
  values.user.clear();
  values.password.clear();
  return save(values);
}
