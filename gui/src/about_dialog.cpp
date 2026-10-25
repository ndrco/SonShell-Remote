#include "about_dialog.h"
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
  setObjectName("about_dialog");
  setWindowTitle(QCoreApplication::translate("App", "О программе"));
  setWindowIcon(QIcon(":/branding/ndrco.png"));
  setMinimumWidth(580);
  auto* layout = new QVBoxLayout(this); layout->setContentsMargins(24, 24, 24, 20); layout->setSpacing(20);
  auto* header = new QHBoxLayout; header->setSpacing(24);
  auto* logo = new QLabel(this); logo->setObjectName("about_logo");
  const auto ratio = devicePixelRatioF();
  auto pixmap = QPixmap(":/branding/ndrco.png").scaled(qRound(112 * ratio), qRound(112 * ratio), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  pixmap.setDevicePixelRatio(ratio); logo->setPixmap(pixmap); logo->setFixedSize(112, 112);
  logo->setAccessibleName("NDRco"); header->addWidget(logo, 0, Qt::AlignTop);
  auto* introduction = new QVBoxLayout; introduction->setSpacing(8);
  auto* name = new QLabel("SonShell Remote", this);
  auto font = name->font(); font.setPointSizeF(font.pointSizeF() + 5); font.setBold(true); name->setFont(font);
  introduction->addWidget(name);
  auto* version = new QLabel(QCoreApplication::translate("App", "Версия %1").arg(QCoreApplication::applicationVersion()), this);
  version->setObjectName("about_version"); version->setTextFormat(Qt::PlainText); introduction->addWidget(version);
  auto* description = new QLabel(QCoreApplication::translate("App", "Графическое управление камерами Sony: съёмка, Live View, отбор и передача фото и видео."), this);
  description->setWordWrap(true); introduction->addWidget(description); header->addLayout(introduction, 1); layout->addLayout(header);
  auto* links = new QLabel(this); links->setObjectName("about_links"); links->setTextFormat(Qt::RichText);
  links->setWordWrap(true); links->setOpenExternalLinks(true); links->setTextInteractionFlags(Qt::TextBrowserInteraction);
  links->setText(QCoreApplication::translate("App",
      "<p>Разработка и сопровождение: <a href=\"https://github.com/ndrco\">NDRco</a></p>"
      "<p>Код и развитие проекта:<br><a href=\"https://github.com/ndrco/SonShell-Remote\">github.com/ndrco/SonShell-Remote</a></p>"
      "<p>На основе <a href=\"https://github.com/goudvuur/sonshell\">SonShell</a> и "
      "<a href=\"https://support.d-imaging.sony.co.jp/app/sdk/en/\">Sony Camera Remote SDK</a>.</p>"
      "<p>Независимый проект для камер Sony. Код приложения: Apache-2.0; условия Sony SDK действуют отдельно.</p>"));
  layout->addWidget(links);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close); layout->addWidget(buttons);
}
