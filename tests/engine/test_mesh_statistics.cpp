#include "runtime/MeshExperimentStudy.h"
#include <QCryptographicHash>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace spacerad::engine;
namespace {
int checks=0;
void check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
std::string sha(const QByteArray& bytes){return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString();}
MeshExperimentJob makeJob(){auto d=meshExperimentDemo();d["run"]["histories"]=100;return parseMeshExperiment(QByteArray::fromStdString(d.dump()));}
Json makeResult(const MeshExperimentJob& job,int nz=50,double mean=1.0,double m2=100.0){
    const auto id=job.parts.front().report.at("id").get<std::string>();
    const double factor=1.602176634e-13*job.source.expectedPrimaries();
    const double dose=mean*factor;
    const double se=(m2==0?0:std::sqrt(m2/(100.0*99.0))*factor);
    const Json rse=dose==0?Json(nullptr):Json(se/dose);
    Json r={{"schema","scdat.mesh-experiment-result/1"},{"status","completed"},{"job",job.document},{"job_sha256",sha(job.bytes)},{"histories",100},{"expected_primaries",job.source.expectedPrimaries()},
      {"targets",Json::array({{{"id",id},{"mass_kg",1.0},{"nonzero_histories",nz},{"tid_Gy",dose},{"tid_rad",dose*100.0},{"tid_standard_error_Gy",se},{"relative_standard_error",rse}}})},
      {"score_moments",{{id,{{"ionizing",{{"count",100},{"nonzero_histories",nz},{"mean_MeV",mean},{"m2_MeV2",m2}}}}}}},
      {"worker_sha256",std::string(64,'a')}};
    return r;
}
bool rejects(const MeshExperimentJob& j,const Json& r,double goal=.2,int minNz=30){try{(void)assessMeshExperimentStatistics(j,r,goal,minNz);return false;}catch(const std::exception&){return true;}}
}
int main(){try{
    const auto job=makeJob();const auto id=job.parts.front().report.at("id").get<std::string>();
    auto r=makeResult(job);
    auto a=assessMeshExperimentStatistics(job,r,.2,30);
    check(a.at("schema")=="scdat.mesh-statistics-assessment/1","assessment schema");
    check(a.at("all_statistics_goals_met")==true,"sufficient result meets statistical goal");
    check(a.at("targets").size()==1&&a.at("targets")[0].at("id")==id,"one assessment per sensitive body");
    check(a.at("targets")[0].at("status")=="sufficient"&&a.at("targets")[0].at("statistics_goal_met")==true,"sufficient target status");
    check(a.at("geometry_convergence_accepted")==false&&a.at("external_physics_validation")==false,"statistics do not claim convergence or physics validation");
    check(a.at("targets")[0].at("recommended_histories").get<int>()==100,"recommendation never below observed histories");

    a=assessMeshExperimentStatistics(job,r,.05,30);
    check(a.at("all_statistics_goals_met")==false&&a.at("targets")[0].at("status")=="insufficient","stricter RSE threshold can fail");
    check(a.at("targets")[0].at("recommended_histories").get<int>()>=400,"recommendation scales with inverse squared RSE");
    r=makeResult(job,10,1.0,900.0);a=assessMeshExperimentStatistics(job,r,1.0,30);
    check(a.at("targets")[0].at("status")=="insufficient"&&a.at("targets")[0].at("statistics_goal_met")==false,"nonzero-history minimum enforced");
    check(a.at("targets")[0].at("recommended_histories").get<int>()>=300,"recommendation accounts for minimum nonzero histories");

    r=makeResult(job,0,0.0,0.0);a=assessMeshExperimentStatistics(job,r,.2,30);
    check(a.at("targets")[0].at("status")=="zero_hit"&&a.at("targets")[0].at("relative_standard_error").is_null(),"zero hit is indeterminate rather than zero risk");
    check(a.at("targets")[0].at("recommended_histories").is_null(),"zero hit has no extrapolated budget");
    r=makeResult(job,50,1.0,100.0);a=assessMeshExperimentStatistics(job,r,.001,30);
    check(a.at("targets")[0].at("recommended_histories").get<int>()==1000000&&a.at("targets")[0].at("recommendation_capped")==true,"recommendation cap");

    r=makeResult(job);
    auto bad=r;bad["score_moments"][id]["ionizing"]["count"]=99;check(rejects(job,bad),"moment count must equal run histories");
    bad=r;bad["targets"][0]["nonzero_histories"]=49;check(rejects(job,bad),"target nonzero count must match raw moments");
    bad=r;bad["targets"][0]["tid_Gy"]=0.0;check(rejects(job,bad),"dose recomputed from moments");
    bad=r;bad["targets"][0]["relative_standard_error"]=0.0;check(rejects(job,bad),"relative error recomputed from moments");
    bad=r;bad["job"]["run"]["seed"]=bad["job"]["run"]["seed"].get<int>()+1;check(rejects(job,bad),"embedded job must match parsed job");
    bad=r;bad["job_sha256"]=std::string(64,'b');check(rejects(job,bad),"job digest must match original bytes");
    bad=r;bad["histories"]=99;check(rejects(job,bad),"result history count must match job");
    bad=r;bad["schema"]="scdat.mesh-experiment-result/5";check(rejects(job,bad),"unsupported result schema rejected");
    bad=r;bad["status"]="failed";check(rejects(job,bad),"only completed results accepted");
    bad=r;bad["targets"][0]["mass_kg"]=-1.0;check(rejects(job,bad),"positive mass required");
    bad=r;bad["score_moments"][id]["ionizing"]["mean_MeV"]=-1.0;check(rejects(job,bad),"negative mean rejected");
    bad=r;bad["score_moments"][id]["ionizing"]["m2_MeV2"]=-1.0;check(rejects(job,bad),"negative M2 rejected");
    bad=r;bad["score_moments"][id]["ionizing"]["mean_MeV"]=std::numeric_limits<double>::infinity();check(rejects(job,bad),"nonfinite mean rejected");
    bad=makeResult(job,0,0.0,1.0);check(rejects(job,bad),"zero hit with positive M2 rejected");
    bad=makeResult(job,50,1.0,0.0);check(rejects(job,bad),"zero-valued moments cannot hide zero histories");
    bad=makeResult(job,50,1.0,50.0);check(rejects(job,bad),"M2 must account for omitted zero histories");
    bad=r;bad["targets"][0]["id"]="unknown";check(rejects(job,bad),"target IDs must match sensitive parts");
    bad=r;bad["targets"].push_back(bad["targets"][0]);check(rejects(job,bad),"duplicate target ID rejected");
    bad=r;bad["targets"].clear();check(rejects(job,bad),"missing target rejected");
    check(rejects(job,r,0.0,30),"zero target RSE rejected");check(rejects(job,r,1.1,30),"target RSE above one rejected");
    check(rejects(job,r,.2,1),"minimum nonzero history bound enforced");check(rejects(job,r,.2,1000001),"maximum nonzero history bound enforced");
    std::cout<<checks<<" mesh statistics checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Check "<<checks<<": "<<e.what()<<'\n';return 1;}}
