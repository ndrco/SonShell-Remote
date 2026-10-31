#include "help_dialog.h"
#include <QDialogButtonBox>
#include <QFile>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QCoreApplication>

HelpDialog::HelpDialog(const QString& language, QWidget* parent) : QDialog(parent) {
  setObjectName("help_dialog"); setWindowTitle(QCoreApplication::translate("App", "Help · SonShell Remote")); resize(840, 680);
  auto* layout = new QVBoxLayout(this);
  auto* browser = new QTextBrowser(this); browser->setObjectName("help_content"); browser->setOpenExternalLinks(true);
  QFile file(":/help/" + (language == "en" ? QString("en") : QString("ru")) + ".md");
  if (file.open(QIODevice::ReadOnly)) browser->setMarkdown(QString::fromUtf8(file.readAll()));
  layout->addWidget(browser);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close); layout->addWidget(buttons);
}
