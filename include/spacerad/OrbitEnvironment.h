#pragma once
#include "AnalysisService.h"
namespace spacerad {
struct OrbitModelRuntime {QString python,toolsDirectory;int timeoutSeconds=300;};
OrbitModelRuntime loadOrbitModelRuntime(const QString&);
struct OrbitEnvironmentSettings {
    QString ephemeris,frame="GEO",positionUnit="km";
    double maxGapSeconds=60;
    std::array<double,2> electronBand{.1,1},protonBand{1,30};
    Json toJson() const;
    static OrbitEnvironmentSettings fromJson(const Json&);
};
// Independently reconstruct physical differential spectra from saved integral
// model output and timestamped ephemerides. No Python or Worker required.
Json loadOrbitEnvironment(const QString& modelDirectory);
Json loadOrbitModelJob(const QString& jobDirectory);
class OrbitEnvironmentService final:public QObject {
    Q_OBJECT
public:
    explicit OrbitEnvironmentService(QObject* parent=nullptr);
    ~OrbitEnvironmentService() override;
    void configure(const OrbitModelRuntime&);
    void start(const Project&,const OrbitEnvironmentSettings&,const QString& newDirectory);
    void cancel();
    bool running() const;QString state() const;QString outputDirectory() const;QByteArray result() const;
signals:
    void stateChanged(const QString&);
    void progress(int,int);
    void completed(const QByteArray&);
    void failed(const QString&);
private:
    struct Impl;std::unique_ptr<Impl> d;
    void finish(const QString&,const QString& error={});
};
}
