#include <spacerad/WorkbenchWidget.h>
#include <QApplication>
#include <QDialog>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>
#include <iostream>
int main(int argc,char** argv){QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());QApplication app(argc,argv);auto* w=new spacerad::WorkbenchWidget;w->show();app.processEvents();auto* b=w->findChild<QPushButton*>("spaceradDeviceResponse");if(!b)return 1;b->click();app.processEvents();auto* d=w->findChild<QDialog*>("spaceradDeviceResponseDialog");if(!d||!d->isVisible())return 1;std::cout<<"device response UI contract passed\n";d->close();delete w;return 0;}
