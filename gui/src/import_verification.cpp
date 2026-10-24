#include "import_verification.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

void verify_imports(QPromise<ImportVerification>& promise, const QString& directory, QJsonObject ledger) {
  ImportVerification result; result.ledger = ledger;
  const auto root_path = QFileInfo(directory).canonicalFilePath(); const QDir root(root_path);
  static const QRegularExpression digest_pattern("^[0-9a-f]{64}$");
  promise.setProgressRange(0, 1000);
  int done = 0;
  for (auto it = ledger.constBegin(); it != ledger.constEnd(); ++it) {
    if (promise.isCanceled()) return;
    auto entry = it.value().toObject(); const auto path = entry.value("path").toString();
    const QFileInfo before(path); const auto canonical = before.canonicalFilePath();
    const auto relative = root.relativeFilePath(canonical);
    const bool safe = !root_path.isEmpty() && !canonical.isEmpty() && !before.isSymLink() && relative != ".." && !relative.startsWith("../");
    const auto expected_size = entry.value("size").toString().toLongLong();
    const auto expected_digest = entry.value("sha256").toString();
    QString state;
    if (!safe || !before.isFile()) { state = "missing"; ++result.missing; }
    else if (before.size() != expected_size) { state = "changed"; ++result.changed; }
    else if (!digest_pattern.match(expected_digest).hasMatch()) { state = "legacy"; ++result.legacy; }
    else {
      QFile file(path); QCryptographicHash hash(QCryptographicHash::Sha256); qint64 bytes = 0;
      bool readable = file.open(QIODevice::ReadOnly);
      while (readable && !file.atEnd()) {
        if (promise.isCanceled()) return;
        const auto block = file.read(1024 * 1024);
        if (block.isEmpty()) { readable = false; break; }
        hash.addData(block); bytes += block.size();
        promise.setProgressValue(int((done + (expected_size ? double(bytes) / expected_size : 0)) * 1000 / ledger.size()));
      }
      const QFileInfo after(path);
      const bool stable = after.canonicalFilePath() == canonical && !after.isSymLink() && after.size() == before.size() &&
          after.lastModified() == before.lastModified() && bytes == expected_size;
      if (readable && stable && QString::fromLatin1(hash.result().toHex()) == expected_digest) {
        state = "ok"; ++result.good;
        entry.insert("mtime", QString::number(after.lastModified().toMSecsSinceEpoch()));
      } else { state = "changed"; ++result.changed; }
    }
    if (state != "legacy") entry.insert("valid", state == "ok");
    entry.insert("verification", state); result.ledger.insert(it.key(), entry);
    promise.setProgressValue(++done * 1000 / ledger.size());
  }
  if (!promise.isCanceled()) promise.addResult(result);
}
