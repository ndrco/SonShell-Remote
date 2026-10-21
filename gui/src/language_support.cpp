#include "language_support.h"
#include <QCoreApplication>
#include <QLibraryInfo>
#include <QVariant>

static void initialize_resources() { Q_INIT_RESOURCE(remote_resources); }
LanguageSupport::LanguageSupport() { initialize_resources(); }
LanguageSupport::~LanguageSupport() {
  QCoreApplication::removeTranslator(&translator_);
  QCoreApplication::removeTranslator(&qt_translator_);
}
QString LanguageSupport::current_language() {
  return QCoreApplication::instance()->property("ui_language").toString() == "en" ? "en" : "ru";
}
bool LanguageSupport::install(const QString& language) {
  QCoreApplication::removeTranslator(&translator_);
  QCoreApplication::removeTranslator(&qt_translator_);
  if (language != "ru" && language != "en") return false;
  if (language == "en" && (!translator_.load(":/i18n/sonshell_en.qm") || !QCoreApplication::installTranslator(&translator_))) return false;
  if (language == "ru" && qt_translator_.load("qtbase_ru", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
    QCoreApplication::installTranslator(&qt_translator_);
  QCoreApplication::instance()->setProperty("ui_language", language);
  return true;
}
