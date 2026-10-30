#pragma once
#include "../PreviewMeshQuality.h"
#include <QByteArray>
#include <QString>
namespace spacerad::engine {
struct TriangleMeshReview {
    TriangleMesh mesh; // Normalized actor-local mm; no welding/repair.
    Transform placement;
    Json bundle,report;
};
TriangleMeshReview inspectTriangleMeshBytes(const QByteArray&,const QString& fileName,bool leafBvhRecheck=false);
TriangleMeshReview inspectTriangleMeshFile(const QString&,bool leafBvhRecheck=false);
// Writes only a new path after strict structural validation; bad topology can
// be exchanged for diagnosis. This never creates a radiation Project.
void writeTriangleMeshBundle(const QString&,const Json&);
Json makeTriangleMeshBundle(const TriangleMesh&,const std::string& id,const std::string& material,bool sensitive,
    const Vector3& center,const Vector3& angles,const Json& declaredSource);
}
