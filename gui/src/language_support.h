#pragma once
#include <QTranslator>

// Qt TS/QM catalogs, embedded for identical build-tree and installed behavior.
// Install before constructing widgets. A running camera session is not restarted.
class LanguageSupport final {
 public:
  LanguageSupport();
  ~LanguageSupport();
  bool install(const QString& language);
  static QString current_language();
 private:
  QTranslator translator_;
  QTranslator qt_translator_;
};
