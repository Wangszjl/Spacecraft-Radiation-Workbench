#pragma once
#include "ShieldingScan.h"

namespace spacerad {
// Native fixed-budget study. Same settings schema as the Python research tool;
// native archives have their own explicitly versioned manifest and plan.
Json defaultPartitionStudySettings(const Project&, int fineBandHistories=5000);
Json planPartitionStudy(const Project&,const ShieldingScanSettings&,const Json& settings);
// Aggregate complete, already accepted ShieldingScan reports in plan order.
Json reviewPartitionStudy(const Json& plan,const std::vector<Json>& scans);
Json loadPartitionStudy(const QString& directory);
QByteArray partitionStudyCsv(const Json& report);

class PartitionStudyService final:public QObject {
    Q_OBJECT
public:
    explicit PartitionStudyService(QObject* parent=nullptr);
    ~PartitionStudyService() override;
    void configure(const RuntimeOptions&);
    void start(const Project&,const ShieldingScanSettings&,const Json&,const QString& newDirectory);
    void cancel();
    bool running() const;
    QString state() const;
    QString outputDirectory() const;
    QByteArray result() const;
signals:
    void stateChanged(const QString&);
    void progress(int completedScans,int totalScans,qint64 histories,qint64 totalHistories);
    void completed(const QByteArray&);
    void failed(const QString&);
private:
    struct Impl;std::unique_ptr<Impl> d;
    void next();void checkpoint();void finish(const QString&,const QString& error={});
};
}
