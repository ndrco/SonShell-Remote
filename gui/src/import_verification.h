#pragma once
#include <QJsonObject>
#include <QPromise>
#include <QString>

struct ImportVerification {
  QJsonObject ledger;
  int good = 0, changed = 0, missing = 0, legacy = 0;
};
void verify_imports(QPromise<ImportVerification>& promise, const QString& directory, QJsonObject ledger);
