#include "RadiationProject.h"
#include "runtime/BatchStatistics.h"
#include "runtime/ShieldingScan.h"
#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace spacerad::engine;
namespace {
int checks = 0;
void check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F&& f, const char* message) {
    bool failed = false; try { f(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}
bool near(double a, double b, double tolerance = 1e-12) {
    return std::isfinite(a) && std::isfinite(b) && std::abs(a-b) <= tolerance * std::max({1.0,std::abs(a),std::abs(b)});
}
Spectrum spectrum(const std::string& particle) {
    Spectrum s; s.particle = particle; s.energy = {1.0, 2.0}; s.values = {1.0, 1.0}; s.prepare(); return s;
}
Project projectFixture(int events = 20, long seed = 2147483645) {
    Project p = Project::demo();
    p.name = "Synthetic shielding scan fixture"; p.synthetic = true;
    p.events = events; p.seed = seed; p.radius = 100; p.duration = 17.0; p.cut = 0.02;
    p.volumes = {
        {"Shield", "G4_Al", {0,0,0}, {82,82,82}, 1, false},
        {"Target,\"Si\"", "G4_Si", {10,0,0}, {10,10,10}, 0, true},
        {"OutsidePart", "G4_Al", {45,0,0}, {4,4,4}, 0, false}
    };
    p.assemblies.clear(); p.templates.clear(); p.instances.clear();
    p.spectra = {spectrum("e-"), spectrum("proton")};
    p.validate(); return p;
}
BatchOptions batchOptions(int events=20, int perBatch=6) {
    BatchOptions b; b.historiesPerBatch=perBatch; b.maxHistories=events; b.maxParallel=2;
    b.maxSeconds=0; b.targetRelativeError=0.1; b.minNonzeroHistories=2; b.minCompletedBatches=2;
    return b;
}
ShieldingScanOptions scanOptions() { return {"Shield",{0,1,3},batchOptions()}; }
Json scanJson(const ShieldingScanOptions& o) { return o.toJson(); }
Json targetRow(double dose, double se, Json rse, int nonzero=20) {
    return {{"id","Target,\"Si\""},{"mass_kg",0.1},{"tid_Gy",dose},
        {"tid_standard_error_Gy",se},{"relative_standard_error",std::move(rse)},
        {"nonzero_histories",nonzero}};
}
Json caseResult(const ShieldingCase& c, double dose, double se, Json rse, bool converged=true, int nonzero=20) {
    return {{"status","completed"},{"histories",20},{"project",c.project.toJson()},
        {"targets",Json::array({targetRow(dose,se,std::move(rse),nonzero)})},
        {"batch",{{"convergence",{{"targets",Json::array({{{"id","Target,\"Si\""},{"converged",converged}}})}}}}}};
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc,argv);
    try {
        const auto base=projectFixture(); const auto options=scanOptions();
        const auto parsed=ShieldingScanOptions::fromJson(scanJson(options));
        check(parsed.shellId=="Shield" && parsed.thicknesses==std::vector<double>({0,1,3}) &&
              parsed.batch.maxHistories==20,"Strict options round-trip preserves shell, ordered thicknesses and batch settings");

        for (const auto& badThicknesses : std::vector<Json>{
                 Json::array({0,3,1}),Json::array({0,1,1}),Json::array({-1,1}),
                 Json::array({0,10001}),Json::array({0,true})}) {
            auto bad=scanJson(options); bad["thicknesses_mm"]=badThicknesses;
            rejects([&]{ShieldingScanOptions::fromJson(bad);},"Malformed thickness order/type/range must be rejected");
        }
        auto extra=scanJson(options);extra["unknown"]=1;
        rejects([&]{ShieldingScanOptions::fromJson(extra);},"Unknown scan option fields must be rejected");
        auto noSchema=scanJson(options);noSchema.erase("schema");
        rejects([&]{ShieldingScanOptions::fromJson(noSchema);},"Missing scan schema must be rejected");

        const auto plan=planShieldingScan(base,options);
        check(plan.size()==6,"Three thicknesses and two incident particles must produce six cases");
        std::set<int> caseSeeds;std::set<int> allBatchSeeds;std::set<std::string> particles;
        const auto batches=batchPlanJson(planBatches(base,batchOptions()));
        check(batches.size()==4 && batches[0].at("histories")==6 && batches[1].at("histories")==6 &&
              batches[2].at("histories")==6 && batches[3].at("histories")==2,
              "20 events with batch size 6 must plan as 6, 6, 6, 2");
        bool sawWrappedSeed=false;
        for (std::size_t i=0;i<plan.size();++i) {
            const auto& c=plan[i]; const auto& shell=c.project.volumes;
            check(c.index==int(i) && (c.particle=="e-"||c.particle=="proton") && c.project.events==20,
                  "Each case has a stable index, supported particle and unchanged history budget");
            check(caseSeeds.insert(c.project.seed).second,"Shielding cases must have unique base seeds");
            particles.insert(c.particle);
            for (const auto& b:planBatches(c.project,options.batch)) {
                check(allBatchSeeds.insert(b.seed).second,"All logical batch seeds must remain unique across scan cases");
                if (b.seed<100 && i>0) sawWrappedSeed=true;
            }
            const double thickness=c.thickness;
            const auto target=std::find_if(shell.begin(),shell.end(),[](const Volume& v){return v.id=="Target,\"Si\"";});
            check(target!=shell.end() && target->center==std::array<double,3>{10,0,0},
                  "Silicon target remains at its original location in every case");
            check(c.project.duration==base.duration && c.project.radius==base.radius && c.project.events==base.events &&
                  c.project.cut==base.cut && c.project.physics==base.physics,
                  "Environment and transport parameters remain unchanged across cases");
            check(c.project.spectra.size()==1 && c.project.spectra.front().particle==c.particle,
                  "Each case retains only its selected positive source spectrum");
            const auto shield=std::find_if(shell.begin(),shell.end(),[](const Volume& v){return v.id=="Shield";});
            if (thickness==0) {
                check(shield==shell.end(),"Zero thickness removes the shell while retaining the target");
            } else {
                check(shield!=shell.end() && !shield->sensitive && shield->wall==thickness &&
                      shield->size==std::array<double,3>{80+2*thickness,80+2*thickness,80+2*thickness},
                      "Positive thickness changes only the non-sensitive box-shell and preserves its 80 mm inner cavity");
            }
        }
        check(particles==std::set<std::string>({"e-","proton"}) && sawWrappedSeed,
              "Case and child batch seed allocation covers both particles and wraps the seed range");

        auto badSensitiveShell=base;badSensitiveShell.volumes[0].sensitive=true;
        rejects([&]{planShieldingScan(badSensitiveShell,options);},"Sensitive box shell cannot be scanned as shielding");
        auto badShape=base;badShape.volumes[0].shape="cylinder";
        rejects([&]{planShieldingScan(badShape,options);},"Only direct hollow boxes can be shielding scan parameters");
        auto badSolid=base;badSolid.volumes[0].wall=0;
        rejects([&]{planShieldingScan(badSolid,options);},"Solid box cannot be selected as a hollow shielding shell");
        auto wrongId=options;wrongId.shellId="not-a-volume";
        rejects([&]{planShieldingScan(base,wrongId);},"Unknown shielding shell ID must be rejected");
        rejects([&]{auto wrongSpectrum=base;wrongSpectrum.spectra.push_back(spectrum("alpha"));
                    wrongSpectrum.validate();planShieldingScan(wrongSpectrum,options);},
                "Unsupported incident particle in source spectra must be rejected");
        auto tooThick=options;tooThick.thicknesses={0,60};
        rejects([&]{planShieldingScan(base,tooThick);},"Shield growth outside the source sphere must be rejected");
        auto overlap=options;overlap.thicknesses={0,4};
        rejects([&]{planShieldingScan(base,overlap);},"Over-thick shield overlapping a neighboring solid must be rejected");
        auto tooMany=projectFixture(10002);auto manyOptions=scanOptions();manyOptions.thicknesses={0,1};manyOptions.batch=batchOptions(10002,2);
        rejects([&]{planShieldingScan(tooMany,manyOptions);},"Total planned batches above 10000 must be rejected");

        auto complete=std::vector<Json>();complete.reserve(plan.size());
        for(const auto& c:plan) complete.push_back(c.particle=="e-"?caseResult(c,2.0,0.2,0.1):caseResult(c,3.0,0.3,0.1));
        const auto curves=shieldingCurves(plan,complete);
        check(curves.is_array() && curves.size()==3,"Complete electron/proton pairs produce one curve row per thickness");
        const auto& first=curves.at(0);
        check(first.at("tid_Gy")==5.0 && near(first.at("tid_standard_error_Gy"),std::sqrt(0.13)) &&
              first.at("components").size()==2,"Total dose sums components and combines independent standard errors in quadrature");
        check(first.at("components").at(0).at("particle")=="e-" && first.at("components").at(1).at("particle")=="proton",
              "Component rows retain the source particle identity");

        auto incomplete=complete;incomplete[2]=nullptr;
        const auto partial=shieldingCurves(plan,incomplete);
        check(partial.is_array() && partial.size()==2,"A thickness with an incomplete particle case is omitted from the curve output");
        for(const auto& row:partial) check(row.at("thickness_mm")!=1.0,"Incomplete thickness group has no partial curve row");

        auto zero=complete;
        for(std::size_t i=0;i<plan.size();++i) if(plan[i].thickness==1.0 && plan[i].particle=="proton")
            zero[i]=caseResult(plan[i],0.0,0.0,nullptr,false,0);
        const auto zeroCurves=shieldingCurves(plan,zero);
        bool foundZero=false;
        for(const auto& row:zeroCurves) if(row.at("thickness_mm")==1.0) {
            foundZero=true;
            check(row.at("relative_standard_error").is_null() && row.at("converged")==false &&
                  row.at("zero_hit_components")==Json::array({"proton"}),
                  "A zero-hit component keeps null RSE and marks the combined curve unconverged");
        }
        check(foundZero,"One zero-hit component still yields a valid thickness row with an explicit warning field");

        auto wrongMass=complete;wrongMass[1]["targets"][0]["mass_kg"]=0.2;
        rejects([&]{shieldingCurves(plan,wrongMass);},"Particle cases with inconsistent target mass must be rejected");
        auto wrongProject=complete;wrongProject[1]["project"]["production_cut_mm"]=123;
        rejects([&]{shieldingCurves(plan,wrongProject);},"Case result whose physical project differs from its plan must be rejected");

        const QString csv=shieldingCsv(Json{{"curves",curves}});
        check(csv.startsWith("thickness_mm,target_id,material,component,tid_Gy,tid_rad,standard_error_Gy,relative_standard_error,converged,zero_hit_components\r\n"),
              "CSV output has the documented fields and CRLF header");
        check(csv.contains("\"Target,\"\"Si\"\"\"") && csv.contains("\"G4_Si\"") && csv.contains("\"e-\"") &&
              csv.contains("\"proton\"") && csv.contains("5") && csv.contains("0.2"),
              "CSV quotes comma/quote identifiers and emits total/component records");

        std::cout<<"Shielding scan contract: "<<checks<<" checks passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
