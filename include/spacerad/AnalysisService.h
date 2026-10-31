#pragma once
#include "Project.h"
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <memory>
namespace spacerad {
struct RuntimeOptions { QString worker,dataRoot; QStringList libraryDirectories; };
// Relative paths resolve from the runtime file, without changing host state.
RuntimeOptions loadRuntimeFile(const QString& path);
class AnalysisService final : public QObject {
 Q_OBJECT
public:
 explicit AnalysisService(QObject* parent=nullptr);
 ~AnalysisService() override;
 void configure(const RuntimeOptions&);
 RuntimeOptions runtime() const;
 void start(const Project&,const QString& newDirectory);
 void cancel();
 bool running() const;
 QString state() const;
 QByteArray result() const;
 QString outputDirectory() const;
 static QProcessEnvironment childEnvironment(const RuntimeOptions&);
signals:
 void stateChanged(const QString&);
 void progress(int completed,int total);
 void completed(const QByteArray&);
 void failed(const QString&);
 void logMessage(const QString&);
private:
 struct Impl;std::unique_ptr<Impl> d;
};
}
