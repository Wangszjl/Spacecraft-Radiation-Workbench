#include "VoxelExport.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace spacerad::engine {
namespace {
constexpr qint64 fileLimit = 32LL * 1024 * 1024;
constexpr qint64 totalLimit = 128LL * 1024 * 1024;
using Vec3 = std::array<double, 3>;
using Mat3 = std::array<Vec3, 3>;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string("Voxel export: ") + message);
}

const Json& member(const Json& object, const char* name) {
    require(object.is_object() && object.contains(name), "missing required field");
    return object.at(name);
}

double finiteNumber(const Json& value, const char* message) {
    require(value.is_number(), message);
    const double number = value.get<double>();
    require(std::isfinite(number), message);
    return number;
}

std::size_t integer(const Json& value, const char* message) {
    require(value.is_number_unsigned() || value.is_number_integer(), message);
    if (value.is_number_integer()) require(value.get<std::int64_t>() >= 0, message);
    const auto number = value.get<std::uint64_t>();
    require(number <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()), message);
    return static_cast<std::size_t>(number);
}

Vec3 vector3(const Json& value, const char* message) {
    require(value.is_array() && value.size() == 3, message);
    return {finiteNumber(value[0], message), finiteNumber(value[1], message), finiteNumber(value[2], message)};
}

Mat3 matrix3(const Json& value) {
    require(value.is_array() && value.size() == 3, "world_rotation must be a 3x3 matrix");
    Mat3 result{};
    for (std::size_t row = 0; row < 3; ++row) result[row] = vector3(value[row], "world_rotation must be finite");
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            double dot = 0.0;
            for (std::size_t k = 0; k < 3; ++k) dot += result[row][k] * result[column][k];
            require(std::abs(dot - (row == column ? 1.0 : 0.0)) <= 1e-8,
                    "world_rotation must be orthonormal");
        }
    }
    const double determinant =
        result[0][0] * (result[1][1] * result[2][2] - result[1][2] * result[2][1]) -
        result[0][1] * (result[1][0] * result[2][2] - result[1][2] * result[2][0]) +
        result[0][2] * (result[1][0] * result[2][1] - result[1][1] * result[2][0]);
    require(std::abs(determinant - 1.0) <= 1e-8, "world_rotation must be a proper rotation");
    return result;
}

struct Field {
    Vec3 center{};
    Vec3 origin{};
    Vec3 spacing{};
    Mat3 rotation{};
    std::array<std::size_t, 3> counts{};
    std::size_t histories = 0;
    double cellMass = 0.0;
    const Json* rows = nullptr;
};

bool close(double a, double b, double relative = 1e-9) {
    return std::abs(a - b) <= relative * std::max({1e-288, std::abs(a), std::abs(b)});
}

Field validate(const Json& field) {
    require(field.is_object(), "voxel_field must be an object");
    require(member(field, "schema").is_string() && member(field, "schema") == "scdat.radiation-voxel-field/1",
            "unsupported voxel field schema");
    const auto& volumeId = member(field, "volume_id");
    require(volumeId.is_string() && !volumeId.get<std::string>().empty(), "volume_id must be nonempty text");
    Field parsed;
    parsed.center = vector3(member(field, "world_center_mm"), "world_center_mm must be finite");
    parsed.origin = vector3(member(field, "origin_local_mm"), "origin_local_mm must be finite");
    parsed.rotation = matrix3(member(field, "world_rotation"));
    parsed.spacing = vector3(member(field, "spacing_mm"), "spacing_mm must be positive and finite");
    for (double spacing : parsed.spacing) require(spacing > 0.0, "spacing_mm must be positive and finite");

    const auto& counts = member(field, "counts");
    require(counts.is_array() && counts.size() == 3, "counts must contain nx, ny and nz");
    std::size_t cellCount = 1;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        parsed.counts[axis] = integer(counts[axis], "counts must be positive integers");
        require(parsed.counts[axis] >= 1 && parsed.counts[axis] <= 32, "counts must be in [1,32]");
        cellCount *= parsed.counts[axis];
    }
    require(cellCount <= 4096, "voxel field exceeds 4096 cells");
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double expectedOrigin = -0.5 * static_cast<double>(parsed.counts[axis]) * parsed.spacing[axis];
        require(close(parsed.origin[axis], expectedOrigin), "origin_local_mm must center the voxel grid");
    }

    parsed.histories = integer(member(field, "histories"), "histories must be a positive integer");
    require(parsed.histories >= 2 && parsed.histories <= 100000000, "voxel histories must be in [2,100000000]");
    const double expectedPrimaries = finiteNumber(member(field, "expected_primaries"), "expected_primaries must be positive and finite");
    require(expectedPrimaries > 0.0, "expected_primaries must be positive");
    parsed.cellMass = finiteNumber(member(field, "cell_mass_kg"), "cell_mass_kg must be positive and finite");
    require(parsed.cellMass > 0.0, "cell_mass_kg must be positive and finite");
    const auto& qualityCriteria = member(field, "quality_criteria");
    require(qualityCriteria.is_object() && qualityCriteria.size() == 2 &&
            qualityCriteria.contains("min_nonzero_histories") && qualityCriteria.contains("max_relative_error"),
            "quality_criteria must contain min_nonzero_histories and max_relative_error");
    const auto minNonzero = integer(qualityCriteria.at("min_nonzero_histories"), "minimum nonzero histories must be an integer");
    const double maxRelativeError = finiteNumber(qualityCriteria.at("max_relative_error"), "maximum relative error must be finite");
    require(minNonzero >= 2 && minNonzero <= 100000000 && maxRelativeError >= 1e-9 && maxRelativeError <= 1.0,
            "quality_criteria values are outside their allowed ranges");
    const auto& rows = member(field, "rows");
    require(rows.is_array() && rows.size() == cellCount, "rows must contain exactly one row per voxel");
    parsed.rows = &rows;

    for (std::size_t index = 0; index < cellCount; ++index) {
        const auto& row = rows[index];
        require(row.is_object(), "voxel row must be an object");
        require(integer(member(row, "index"), "row index must be an integer") == index,
                "rows must be complete and in x-fastest order");
        const std::size_t nx = parsed.counts[0], ny = parsed.counts[1];
        const std::array<std::size_t, 3> expectedIJK{index % nx, (index / nx) % ny, index / (nx * ny)};
        const auto& ijk = member(row, "ijk");
        require(ijk.is_array() && ijk.size() == 3, "row ijk must contain three integers");
        for (std::size_t axis = 0; axis < 3; ++axis)
            require(integer(ijk[axis], "row ijk must contain integers") == expectedIJK[axis],
                    "row index and ijk do not match x-fastest order");

        const double mass = finiteNumber(member(row, "mass_kg"), "row mass must be positive and finite");
        require(mass > 0.0 && close(mass, parsed.cellMass), "voxel rows must have the declared uniform cell mass");
        const double dose = finiteNumber(member(row, "tid_Gy"), "dose must be finite and nonnegative");
        const double error = finiteNumber(member(row, "tid_standard_error_Gy"), "standard error must be finite and nonnegative");
        require(dose >= 0.0 && error >= 0.0, "dose and standard error must be nonnegative");
        const std::size_t nonzero = integer(member(row, "nonzero_histories"), "nonzero_histories must be an integer");
        require(nonzero <= parsed.histories, "nonzero_histories cannot exceed histories");

        const auto& quality = member(row, "quality");
        require(quality.is_string(), "quality must be text");
        const auto qualityName = quality.get<std::string>();
        require(qualityName == "zero_hit" || qualityName == "insufficient" || qualityName == "sufficient",
                "unknown voxel quality");
        const auto& rse = member(row, "relative_standard_error");
        if (!rse.is_null()) require(finiteNumber(rse, "relative_standard_error must be null or nonnegative") >= 0.0,
                                    "relative_standard_error must be nonnegative");
        require((nonzero == 0) == (qualityName == "zero_hit"), "zero_hit quality must match hit count");
        if (nonzero == 0) require(rse.is_null() && dose == 0.0 && error == 0.0,
                                  "zero-hit cells must have zero scores and unknown relative error");
        const auto& moments = member(row, "score_moments");
        require(moments.is_object() && moments.size() == 3 && moments.contains("ionizing") &&
                moments.contains("total") && moments.contains("nonionizing"), "score_moments must contain the three score channels");
        for (const char* channel : {"ionizing", "total", "nonionizing"}) {
            const auto& score = member(moments, channel);
            require(score.is_object() && score.size() == 4 && score.contains("count") && score.contains("mean_MeV") &&
                    score.contains("m2_MeV2") && score.contains("nonzero_histories"), "score moment channel has invalid fields");
            require(integer(member(score, "count"), "score count must be an integer") == parsed.histories,
                    "score moment count must equal histories");
            const double mean = finiteNumber(member(score, "mean_MeV"), "score mean must be finite and nonnegative");
            const double m2 = finiteNumber(member(score, "m2_MeV2"), "score M2 must be finite and nonnegative");
            require(mean >= 0.0 && m2 >= 0.0, "score moments must be nonnegative");
            require(integer(member(score, "nonzero_histories"), "score nonzero_histories must be an integer") <= parsed.histories,
                    "score nonzero_histories cannot exceed histories");
        }
        require(integer(member(moments.at("ionizing"), "nonzero_histories"), "ionizing nonzero count must be an integer") == nonzero,
                "row hit count must match ionizing score moments");
        const auto& ionizing = moments.at("ionizing");
        const double mean = finiteNumber(member(ionizing, "mean_MeV"), "ionizing mean must be finite and nonnegative");
        const double m2 = finiteNumber(member(ionizing, "m2_MeV2"), "ionizing M2 must be finite and nonnegative");
        const double expectedDose = mean * 1.602176634e-13 * expectedPrimaries / mass;
        const double expectedError = std::sqrt(m2 / (static_cast<double>(parsed.histories) * (parsed.histories - 1))) *
                                     1.602176634e-13 * expectedPrimaries / mass;
        require(close(dose, expectedDose, 1e-8) && close(error, expectedError, 1e-8),
                "voxel dose or standard error disagrees with raw ionizing moments");
        Json expectedRse = mean > 0.0
            ? Json(std::sqrt(m2 / (static_cast<double>(parsed.histories) * (parsed.histories - 1))) / mean)
            : Json(nullptr);
        if (expectedRse.is_null()) require(rse.is_null(), "zero-mean voxel must have unknown relative error");
        else require(!rse.is_null() && close(finiteNumber(rse, "relative_standard_error must be finite"), expectedRse.get<double>(), 1e-8),
                     "relative_standard_error disagrees with raw moments");
        const bool sufficient = nonzero >= minNonzero && !expectedRse.is_null() && expectedRse.get<double>() <= maxRelativeError;
        const std::string expectedQuality = nonzero == 0 ? "zero_hit" : sufficient ? "sufficient" : "insufficient";
        require(qualityName == expectedQuality, "quality label disagrees with quality_criteria and score moments");
    }
    return parsed;
}

std::string number(double value) {
    require(std::isfinite(value), "cannot serialize a non-finite number");
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(17) << value;
    return stream.str();
}

double worldCoordinate(const Field& field, const Vec3& local, std::size_t axis) {
    double result = field.center[axis];
    for (std::size_t k = 0; k < 3; ++k) result += field.rotation[axis][k] * local[k];
    return result;
}

QString fromUtf8(const std::string& value) { return QString::fromUtf8(value.data(), static_cast<int>(value.size())); }

QByteArray bytes(const QString& value) { return value.toUtf8(); }

void writeNewFile(const QString& path, const QByteArray& content) {
    require(!content.isEmpty() && content.size() <= fileLimit, "export file is empty or exceeds 32 MiB");
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly), "cannot create staged export file");
    require(file.write(content) == content.size() && file.commit(), "cannot commit staged export file");
}

QByteArray sha256(const QByteArray& content) {
    return QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex();
}
} // namespace

QString voxelVti(const Json& field) {
    const Field parsed = validate(field);
    const Vec3 imageOrigin{
        worldCoordinate(parsed, parsed.origin, 0),
        worldCoordinate(parsed, parsed.origin, 1),
        worldCoordinate(parsed, parsed.origin, 2)};
    std::ostringstream xml;
    xml.imbue(std::locale::classic());
    xml << "<?xml version=\"1.0\"?>\n"
        << "<VTKFile type=\"ImageData\" version=\"1.0\" byte_order=\"LittleEndian\">\n"
        << "  <ImageData WholeExtent=\"0 " << parsed.counts[0] << " 0 " << parsed.counts[1] << " 0 " << parsed.counts[2]
        << "\" Origin=\"" << number(imageOrigin[0]) << ' ' << number(imageOrigin[1]) << ' ' << number(imageOrigin[2])
        << "\" Spacing=\"" << number(parsed.spacing[0]) << ' ' << number(parsed.spacing[1]) << ' ' << number(parsed.spacing[2])
        << "\" Direction=\"";
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            xml << (row || column ? " " : "") << number(parsed.rotation[row][column]);
    xml << "\">\n    <Piece Extent=\"0 " << parsed.counts[0] << " 0 " << parsed.counts[1] << " 0 " << parsed.counts[2]
        << "\">\n      <CellData Scalars=\"TID_Gy\">\n";

    auto emitArray = [&](const char* name, const char* type, auto valueAt) {
        xml << "        <DataArray type=\"" << type << "\" Name=\"" << name << "\" format=\"ascii\">\n          ";
        for (std::size_t i = 0; i < parsed.rows->size(); ++i) {
            if (i) xml << ' ';
            xml << valueAt(parsed.rows->at(i));
        }
        xml << "\n        </DataArray>\n";
    };
    emitArray("TID_Gy", "Float64", [](const Json& row) { return number(row.at("tid_Gy").get<double>()); });
    emitArray("StandardError_Gy", "Float64", [](const Json& row) { return number(row.at("tid_standard_error_Gy").get<double>()); });
    emitArray("RSE", "Float64", [](const Json& row) {
        return row.at("relative_standard_error").is_null() ? std::string("-1") : number(row.at("relative_standard_error").get<double>());
    });
    emitArray("RSE_validMask", "UInt8", [](const Json& row) { return row.at("relative_standard_error").is_null() ? 0 : 1; });
    emitArray("NonzeroHistories", "UInt64", [](const Json& row) { return row.at("nonzero_histories").dump(); });
    emitArray("QualityCode", "UInt8", [](const Json& row) {
        const auto quality = row.at("quality").get<std::string>();
        return quality == "zero_hit" ? 0 : quality == "insufficient" ? 1 : 2;
    });
    emitArray("CellMass_kg", "Float64", [](const Json& row) { return number(row.at("mass_kg").get<double>()); });
    xml << "      </CellData>\n      <PointData/>\n    </Piece>\n  </ImageData>\n</VTKFile>\n";
    const auto output = xml.str();
    return QString::fromUtf8(output.data(), static_cast<int>(output.size()));
}

QString voxelCsv(const Json& field) {
    const Field parsed = validate(field);
    std::ostringstream csv;
    csv.imbue(std::locale::classic());
    csv << "index,i,j,k,localcenter_x_mm,localcenter_y_mm,localcenter_z_mm,worldcenter_x_mm,worldcenter_y_mm,worldcenter_z_mm,tid_Gy,tid_standard_error_Gy,relative_standard_error,quality,mass_kg\n";
    for (std::size_t index = 0; index < parsed.rows->size(); ++index) {
        const auto& row = parsed.rows->at(index);
        const auto& ijk = row.at("ijk");
        Vec3 local{};
        for (std::size_t axis = 0; axis < 3; ++axis)
            local[axis] = parsed.origin[axis] + (static_cast<double>(ijk[axis].get<std::size_t>()) + 0.5) * parsed.spacing[axis];
        const Vec3 world{worldCoordinate(parsed, local, 0), worldCoordinate(parsed, local, 1), worldCoordinate(parsed, local, 2)};
        csv << index;
        for (const auto& axis : ijk) csv << ',' << axis.get<std::size_t>();
        for (double coordinate : local) csv << ',' << number(coordinate);
        for (double coordinate : world) csv << ',' << number(coordinate);
        csv << ',' << number(row.at("tid_Gy").get<double>())
            << ',' << number(row.at("tid_standard_error_Gy").get<double>()) << ',';
        if (!row.at("relative_standard_error").is_null()) csv << number(row.at("relative_standard_error").get<double>());
        csv << ',' << row.at("quality").get<std::string>() << ',' << number(row.at("mass_kg").get<double>()) << '\n';
    }
    const auto result = csv.str();
    return QString::fromUtf8(result.data(), static_cast<int>(result.size()));
}

void exportVoxelFieldFiles(const Json& result, const QString& newDirectory) {
    require(result.is_object() && result.contains("voxel_field"), "result has no voxel_field");
    const Json& field = result.at("voxel_field");
    (void)validate(field);
    require(!newDirectory.isEmpty(), "destination directory is empty");
    const QFileInfo destinationInfo(newDirectory);
    const QString target = destinationInfo.absoluteFilePath();
    require(!QFileInfo::exists(target), "refusing to overwrite an existing destination");
    const QString parentPath = QFileInfo(target).absolutePath();
    require(QDir().mkpath(parentPath), "cannot create destination parent");

    QTemporaryDir staging(parentPath + "/.voxel-export-XXXXXX");
    require(staging.isValid(), "cannot create staging directory");

    const QByteArray jsonFile = QByteArray::fromStdString(field.dump(2));
    const QByteArray vtiFile = bytes(voxelVti(field));
    const QByteArray csvFile = bytes(voxelCsv(field));
    require(jsonFile.size() <= fileLimit && vtiFile.size() <= fileLimit && csvFile.size() <= fileLimit,
            "an export file exceeds 32 MiB");
    const qint64 payloadSize = jsonFile.size() + vtiFile.size() + csvFile.size();
    require(payloadSize <= totalLimit, "voxel export exceeds 128 MiB");

    writeNewFile(staging.path() + "/field.json", jsonFile);
    writeNewFile(staging.path() + "/field.vti", vtiFile);
    writeNewFile(staging.path() + "/field.csv", csvFile);
    Json manifest = {
        {"schema", "scdat.radiation-voxel-export/1"},
        {"files", Json::array({
            {{"path", "field.json"}, {"size_bytes", jsonFile.size()}, {"sha256", sha256(jsonFile).toStdString()}},
            {{"path", "field.vti"}, {"size_bytes", vtiFile.size()}, {"sha256", sha256(vtiFile).toStdString()}},
            {{"path", "field.csv"}, {"size_bytes", csvFile.size()}, {"sha256", sha256(csvFile).toStdString()}}
        })}
    };
    const QByteArray manifestFile = QByteArray::fromStdString(manifest.dump(2));
    require(manifestFile.size() <= fileLimit && payloadSize + manifestFile.size() <= totalLimit,
            "voxel export manifest exceeds size limits");
    writeNewFile(staging.path() + "/manifest.json", manifestFile);

    require(!QFileInfo::exists(target), "refusing to overwrite an existing destination");
    QDir parent(parentPath);
    require(parent.rename(QFileInfo(staging.path()).fileName(), destinationInfo.fileName()),
            "cannot atomically publish staged voxel export");
}
} // namespace spacerad::engine
