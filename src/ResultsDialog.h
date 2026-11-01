#pragma once
#include <QDialog>
namespace spacerad {
// Owned by the shared widget; never changes its live project/service/result.
void showResultsDialog(QWidget* parent, const QString& baseline, const QString& candidate);
}
