#pragma once
#include "AnalysisService.h"
#include <QWidget>
#include <memory>
namespace spacerad {
class WorkbenchWidget final:public QWidget {
 Q_OBJECT
public:
 explicit WorkbenchWidget(QWidget* parent=nullptr);
 ~WorkbenchWidget() override;
 void setRuntime(const RuntimeOptions&);
 void setProject(const Project&);
 Project project() const;
 void loadProject(const QString&);
 void saveProject(const QString&);
 void runTo(const QString& newDirectory);
 void cancel();
 QByteArray acceptedResult() const;
 AnalysisService* service() const;
 void selectComponent(const QString& id);
 QString selectedComponent() const;
signals:
 void resultReady(const QByteArray&);
 void projectChanged();
 void componentSelected(const QString& id);
private:
 struct Impl;std::unique_ptr<Impl> d;
 void refresh();void invalidate();void showError(const QString&);void rebuildModelTree(const Project&);
};
RuntimeOptions loadRuntimeFile(const QString& path);
}
