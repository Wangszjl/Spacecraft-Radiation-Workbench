#pragma once
#include "Project.h"
#include <QByteArray>
#include <QString>

namespace spacerad {
// Offline consistency checks; no Worker, datasets or original absolute paths required.
// Hashes bind the saved pair, but do not authenticate the producer of an archive.
class ArchivedRun {
public:
    const QString& directory() const { return directory_; }
    const Project& project() const { return project_; }
    const Json& result() const { return result_; }
    Json summary() const;
private:
    QString directory_;
    Project project_;
    Json result_;
    std::string resultHash_, projectHash_;
    friend ArchivedRun loadArchivedRun(const QString& directory);
};
ArchivedRun loadArchivedRun(const QString& directory);
// Reports conservative exact-setting comparisons. A shared seed does not prove
// independent estimates; no uncertainty/significance for the difference is inferred.
Json compareRuns(const ArchivedRun& baseline, const ArchivedRun& candidate);
QByteArray comparisonCsv(const Json& comparison);
}
