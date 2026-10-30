#pragma once
#include <QByteArray>
#include <QString>
class QMainWindow;
class QDockWidget;
QDockWidget* openTKMSpaceRadDock(QMainWindow*,const QString& runtimeFile={},const QByteArray& projectJson={});
