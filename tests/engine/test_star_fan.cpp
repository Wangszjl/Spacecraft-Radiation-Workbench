#include "runtime/NonconvexAssembly.h"
#include "runtime/StarFanSeparation.h"
#include <QCryptographicHash>
#include <array>
#include <cmath>
#include <cfenv>
#include <iostream>
#include <limits>
#include <stdexcept>
#if defined(_M_X64) || defined(__SSE2__)
#include <xmmintrin.h>
#endif

using namespace spacerad::engine;
namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
QByteArray bytes(const Json& j){return QByteArray::fromStdString(j.dump());}
MeshExperimentJob parse(const Json& j,bool blocked=false){return parseMeshExperiment(bytes(j),blocked);}
bool rejects(const Json& j,bool blocked=false){try{(void)parse(j,blocked);return false;}catch(const std::exception&){return true;}}
std::string digest(const Json& j){return QCryptographicHash::hash(bytes(j),QCryptographicHash::Sha256).toHex().toStdString();}
void rehash(Json& j,size_t i){j["origins"][i]["mesh_sha256"]=digest(j["parts"][i]);}
Json blockedAudit(const Json& j){auto job=parse(j,true);check(!job.separationAudit.at("separation_gate_passed").get<bool>()&&!job.separationAudit.at("transport_allowed").get<bool>(),"blocked inspection cannot authorize transport");return job.separationAudit;}
}

int main(){try{
    auto demo=starFanAssemblyDemo();
    check(demo.at("schema")=="scdat.mesh-experiment/6"&&demo.at("separation_contract")=="scdat.star-fan-separation/1","v6 star-fan contract");
    check(demo.at("covers").size()==4,"one explicit cover per part");
    auto job=parse(demo);
    check(job.nonconvexAssembly&&job.assembly&&job.parts.size()==4,"v6 parsed as a four-part assembly");
    check(job.separationAudit.at("schema")=="scdat.star-fan-separation/1"&&job.separationAudit.at("complete")==true&&job.separationAudit.at("separation_gate_passed")==true,"complete star-fan audit passes");
    check(job.separationAudit.at("pairs").size()==6,"all six pairs audited");
    const auto& notchPair=job.separationAudit.at("pairs")[1];
    check(notchPair.at("a")==job.parts[0].report.at("id")&&notchPair.at("b")==job.parts[2].report.at("id"),"notch silicon pair is identified");
    check(notchPair.at("status")=="separated"&&notchPair.at("method")=="cell_pairs"&&notchPair.at("cell_pairs_tested").get<size_t>()>0,"Si nested in the L notch is certified by cells");
    check(!job.separationAudit.at("pairs")[0].at("method").get<std::string>().empty(),"first L pair has a separation method");

    auto legacy=demo;legacy["schema"]="scdat.mesh-experiment/5";legacy.erase("covers");legacy["separation_contract"]="scdat.vertex-hull-separation/1";
    check(rejects(legacy),"v5 hull contract cannot authorize a v6 star-fan assembly");
    auto reference=demo;reference["solid"]="orthogonal_l_reference";
    check(!rejects(reference),"recognized orthogonal-L reference solid remains parseable");

    auto bad=demo;bad["covers"][0]["kernel_local_mm"]={5,5,0};
    check(rejects(bad),"kernel in the L notch is not a strict kernel");auto audit=blockedAudit(bad);
    check(!audit.at("cover_gate_passed").get<bool>(),"invalid kernel fails the cover gate");
    bad=demo;bad["covers"][0]["kernel_local_mm"]={0,-5,0};
    check(rejects(bad),"kernel on the L boundary is rejected");blockedAudit(bad);

    bad=demo;bad["parts"][2]["part"]["center"][1]=3;rehash(bad,2);
    check(rejects(bad),"silicon touching the L boundary is rejected");audit=blockedAudit(bad);
    check(audit.at("pairs")[1].at("status")!="separated","boundary contact has no strict separation certificate");
    bad=demo;bad["parts"][2]["part"]["center"]={-5,-5,0};rehash(bad,2);
    check(rejects(bad),"silicon nested in L material is rejected");audit=blockedAudit(bad);
    check(audit.at("pairs")[1].at("status")!="separated","nested silicon is not certified separated");

    bad=demo;bad["covers"][0]["extra"]=true;check(rejects(bad),"extra cover field rejected");
    bad=demo;bad["covers"][0]["part_id"]="wrong";check(rejects(bad),"cover part ID mismatch rejected");
    bad=demo;bad["covers"][0]["kernel_local_mm"][0]=std::numeric_limits<double>::quiet_NaN();check(rejects(bad),"nonfinite kernel coordinate rejected");
    bad=demo;bad["covers"][0].erase("kernel_local_mm");check(rejects(bad),"missing star-fan kernel rejected");
    bad=demo;bad["covers"].push_back(bad["covers"][0]);check(rejects(bad),"extra cover rejected");
    bad=demo;bad["covers"][2]["kind"]="vertex_hull";bad["covers"][2]["kernel_local_mm"]={0,0,0};check(rejects(bad),"vertex-hull cover rejects extra fields");

    StarFanBudget budget;budget.kernelFaces=0;
    audit=auditStarFanSeparation(job.parts,demo.at("covers"),budget);
    check(!audit.at("complete").get<bool>()&&!audit.at("cover_gate_passed").get<bool>()&&!audit.at("separation_gate_passed").get<bool>(),"zero kernel-face budget blocks audit");
    budget=StarFanBudget{};budget.cellPairs=0;
    audit=auditStarFanSeparation(job.parts,demo.at("covers"),budget);
    check(!audit.at("complete").get<bool>()&&!audit.at("separation_gate_passed").get<bool>(),"zero cell-pair budget blocks audit");
    budget=StarFanBudget{};budget.vertexProjections=0;
    audit=auditStarFanSeparation(job.parts,demo.at("covers"),budget);
    check(!audit.at("complete").get<bool>()&&!audit.at("separation_gate_passed").get<bool>(),"zero vertex-projection budget blocks audit");

#if defined(_M_X64) || defined(__SSE2__)
    const unsigned saved=_mm_getcsr();
    const unsigned hostile=(saved|0x8040u|0x2000u)&~0x4000u; // FTZ, DAZ and round upward.
    _mm_setcsr(hostile);
    audit=auditStarFanSeparation(job.parts,demo.at("covers"));
    const unsigned restored=_mm_getcsr();_mm_setcsr(saved);
    check(restored==hostile,"star-fan audit restores FTZ/DAZ and rounding controls");
    check(audit.at("separation_gate_passed").get<bool>(),"interval audit passes with hostile caller FP state");
#else
    const int savedRound=std::fegetround();
    check(std::fesetround(FE_UPWARD)==0,"set directed caller rounding mode");
    bool directedRejected=false;try{audit=auditStarFanSeparation(job.parts,demo.at("covers"));}catch(const std::exception&){directedRejected=true;}
    const int restoredRound=std::fegetround();std::fesetround(savedRound);
    check(restoredRound==FE_UPWARD,"fallback audit preserves caller rounding mode");
    check(directedRejected,"non-SSE fallback explicitly rejects directed caller rounding");
#endif

    std::cout<<checks<<" star-fan separation checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
