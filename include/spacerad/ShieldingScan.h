#pragma once
#include "AnalysisService.h"

namespace spacerad {
struct ShieldingScanSettings {
    std::string shellId;
    std::vector<double> thicknesses;
    Json toJson() const;
    static ShieldingScanSettings fromJson(const Json&);
};
// Fixed histories per thickness/species; retains the engine's fixed-cavity rule.
Json planShieldingScan(const Project&, const ShieldingScanSettings&);
// Offline verification of a terminal scan and every completed child archive.
Json loadShieldingScan(const QString& directory);
QByteArray shieldingScanCsv(const Json& report);

class ShieldingScanService final : public QObject {
    Q_OBJECT
public:
    explicit ShieldingScanService(QObject* parent=nullptr);
    ~ShieldingScanService() override;
    void configure(const RuntimeOptions&);
    void start(const Project&, const ShieldingScanSettings&, const QString& newDirectory);
    void cancel();
    bool running() const;
    QString state() const;
    QString outputDirectory() const;
    QByteArray result() const;
signals:
    void stateChanged(const QString&);
    void progress(int completedCases, int totalCases, int histories, int totalHistories);
    void completed(const QByteArray&);
    void failed(const QString&);
    void logMessage(const QString&);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void next();
    void checkpoint();
    void finish(const QString& state, const QString& error={});
};
}
