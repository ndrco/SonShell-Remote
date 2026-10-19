#include "settings_store.h"
#include <utility>
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
  v.folder = settings.value("folder", QDir::homePath() + "/Pictures/Sony").toString();
  v.remember_credentials = settings.value("remember_credentials", false).toBool();
  if (v.remember_credentials) {
    v.user = settings.value("credentials/user").toString();
    v.password = settings.value("credentials/password").toString();
  }
  v.auto_sync = settings.value("auto_sync", true).toBool();
  v.auto_liveview = settings.value("auto_liveview", true).toBool();
  v.auto_reconnect = settings.value("auto_reconnect", true).toBool();
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
  settings.setValue("folder", v.folder);
  settings.setValue("remember_credentials", v.remember_credentials);
  settings.remove("credentials");
  if (v.remember_credentials) {
    settings.setValue("credentials/user", v.user);
    settings.setValue("credentials/password", v.password);
  }
  settings.setValue("auto_sync", v.auto_sync);
  settings.setValue("auto_liveview", v.auto_liveview);
  settings.setValue("auto_reconnect", v.auto_reconnect);
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
