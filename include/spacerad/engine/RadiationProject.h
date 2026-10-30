#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <string>
#include <vector>
#include <map>

namespace spacerad::engine {
using Json = nlohmann::json;
constexpr double pi = 3.14159265358979323846;
enum class SpectrumQuantity { Flux, Fluence };
std::string quantityName(SpectrumQuantity quantity);
std::string quantityUnit(SpectrumQuantity quantity);
Json defaultEnvironmentMetadata();
Json defaultProjectProvenance();
struct Spectrum {
    std::string particle;
    std::string interpolation = "linear"; // or bin_constant: energy edges, values per bin
    std::vector<double> energy, values, cumulative;
    double integral = 0; // quantity/area, with /s only for flux
    Json provenance = {{"source_file",nullptr},{"conversions",Json::array()}};
    void prepare();
    double sample(double uniform) const;
};
struct Volume {
    std::string id, material;
    std::array<double,3> center, size; // mm; box lengths, cylinder {D,D,L}, sphere {D,D,D}
    double wall = 0; // 0 = solid, >0 = hollow box
    bool sensitive = false;
    std::string parent; // assembly ID; empty means world
    std::array<double,3> rotation{}; // active Rz*Ry*Rx, degrees, local to parent
    bool visible = true; // preview only, never removes transport material
    std::string shape = "box"; // solid cylinder (local Z) or sphere; curved shells unsupported
};
struct Assembly {
    std::string id, parent;
    std::array<double,3> center{}, rotation{};
    bool visible = true;
};
struct ComponentTemplate {
    std::string id;
    std::vector<Assembly> assemblies;
    std::vector<Volume> volumes;
};
struct ComponentInstance {
    std::string id, templateId, parent;
    std::array<double,3> center{}, rotation{};
    bool visible = true;
    std::map<std::string,std::string> materialOverrides; // template-local volume ID -> NIST ID
};
struct Project {
    std::string name, environmentSource;
    bool synthetic = true;
    SpectrumQuantity quantity = SpectrumQuantity::Flux;
    Json environmentMetadata = defaultEnvironmentMetadata();
    Json provenance = defaultProjectProvenance();
    Json materialSnapshot = nullptr; // v4: pinned NIST definitions; legacy resolves at run
    Json voxelScoring = nullptr; // v7: exact homogeneous sensitive-box subdivision
    double duration = 86400, radius = 500, cut = 0.1;
    int events = 100000;
    long seed = 19780503;
    std::string physics = "FTFP_BERT_EMZ";
    std::vector<Volume> volumes;
    std::vector<Assembly> assemblies;
    std::vector<ComponentTemplate> templates;
    std::vector<ComponentInstance> instances;
    std::vector<Spectrum> spectra;
    double integratedSpectrum() const;
    double expectedPrimaries() const;
    Project convertedQuantity(SpectrumQuantity target) const;
    std::vector<std::string> missingEnvironmentMetadata() const;
    void validate() const;
    static Project fromJson(const Json& json);
    Json toJson() const;
    static Project demo();
    static Project assemblyDemo();
    static Project instanceDemo();
    static Project shapeDemo();
};
Spectrum readSpectrumCsv(const std::string& path, const std::string& particle,
                         bool differentialPerSteradian, SpectrumQuantity quantity=SpectrumQuantity::Flux);
Spectrum parseSpectrumCsv(const std::string& bytes,const std::string& particle,
                          bool differentialPerSteradian,SpectrumQuantity quantity);
Spectrum integralSpectrum(const std::vector<double>& energy,const std::vector<double>& aboveThreshold,
                          const std::string& particle,bool perSteradian,SpectrumQuantity quantity);
struct Moments {
    int count = 0, nonzero = 0;
    double mean = 0, m2 = 0;
    void add(double eventEnergy);
    double standardError() const;
};
} // namespace spacerad::engine
