#pragma once
#include <QDialog>
class HelpDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit HelpDialog(const QString& language, QWidget* parent = nullptr);
};
