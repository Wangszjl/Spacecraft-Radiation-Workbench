#include "ShieldingScan.h"
#include "Geometry.h"
#include <QString>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace spacerad::engine {
namespace {
void require(bool b,const char* m){if(!b)throw std::runtime_error(std::string("Shielding scan: ")+m);}
Json physical(Json p){p.erase("provenance");return p;}
QString csvText(const std::string& s){auto value=QString::fromStdString(s);value.replace('"',"\"\"");return '"'+value+'"';}
QString csvNumber(const Json& j){return j.is_number()?QString::number(j.get<double>(),'g',17):QString();}
}
ShieldingScanOptions ShieldingScanOptions::fromJson(const Json& j){
    require(j.is_object()&&j.size()==4&&j.at("schema")=="scdat.radiation-shielding-scan/1","unsupported or missing/extra scan fields");
    ShieldingScanOptions o;o.shellId=j.at("shell_id").get<std::string>();require(!o.shellId.empty(),"shell_id is empty");
    o.batch=BatchOptions::fromJson(j.at("batch"));const auto& t=j.at("thicknesses_mm");require(t.is_array()&&t.size()>=2&&t.size()<=64,"require 2..64 thicknesses");
    double previous=-1;for(const auto& v:t){require(v.is_number(),"thickness must be numeric");double x=v.get<double>();
        require(std::isfinite(x)&&x>=0&&x<=10000&&x>previous,"thicknesses must be finite, strictly increasing and within 0..10000 mm");o.thicknesses.push_back(x);previous=x;}return o;
}
Json ShieldingScanOptions::toJson()const{return {{"schema","scdat.radiation-shielding-scan/1"},{"shell_id",shellId},{"thicknesses_mm",thicknesses},{"batch",batch.toJson()}};}
std::vector<ShieldingCase> planShieldingScan(const Project& base,const ShieldingScanOptions& raw){
    auto o=ShieldingScanOptions::fromJson(raw.toJson());base.validate();require(base.sourceSampling.is_null(),"Legacy batch shielding scans do not support importance sampling; run direct strata instead");auto batches=planBatches(base,o.batch);
    auto shell=std::find_if(base.volumes.begin(),base.volumes.end(),[&](const Volume& v){return v.id==o.shellId;});
    require(shell!=base.volumes.end()&&shell->shape=="box"&&shell->wall>0&&!shell->sensitive,"select a direct, non-sensitive hollow box volume");
    std::vector<std::string> particles;for(const auto& s:base.spectra){require(s.particle=="e-"||s.particle=="proton","only incident electrons and protons are supported");
        if(std::find(particles.begin(),particles.end(),s.particle)==particles.end())particles.push_back(s.particle);}
    std::sort(particles.begin(),particles.end());require(!particles.empty()&&particles.size()*o.thicknesses.size()*batches.size()<=10000,"scan exceeds 10000 total planned batches");
    std::vector<ShieldingCase> cases;auto cavity=shell->size;for(auto& d:cavity)d-=2*shell->wall;
    for(double t:o.thicknesses)for(const auto& particle:particles){Project p=base;
        auto v=std::find_if(p.volumes.begin(),p.volumes.end(),[&](const Volume& a){return a.id==o.shellId;});
        if(t==0)p.volumes.erase(v);else{v->wall=t;for(int d=0;d<3;++d)v->size[d]=cavity[d]+2*t;}
        p.spectra.erase(std::remove_if(p.spectra.begin(),p.spectra.end(),[&](const Spectrum& s){return s.particle!=particle;}),p.spectra.end());
        if(!p.materialSnapshot.is_null()){
            std::set<std::string> used;for(const auto& a:expandInstances(p).volumes)used.insert(a.material);
            auto& definitions=p.materialSnapshot["materials"];definitions.erase(std::remove_if(definitions.begin(),definitions.end(),[&](const Json& m){return !used.count(m.at("id").get<std::string>());}),definitions.end());
        }
        int index=int(cases.size());p.seed=1+(std::int64_t(base.seed)-1+std::int64_t(index)*batches.size())%2147483646;
        // Loader metadata includes the selected filename. Case identity must be
        // stable when the same raw input is reopened from its archived name.
        p.provenance["last_loaded_input"]=nullptr;
        p.provenance["shielding_scan"]={{"shell_id",o.shellId},{"index",index},{"thickness_mm",t},{"particle",particle},
            {"geometry_rule","Keep inner cavity/placement/material fixed; grow outside; thickness zero removes the shell"}};
        p.validate();cases.push_back({index,t,particle,std::move(p)});
    }return cases;
}
Json shieldingPlanJson(const Project& p,const ShieldingScanOptions& o){Json rows=Json::array();for(const auto& c:planShieldingScan(p,o))rows.push_back({
    {"index",c.index},{"thickness_mm",c.thickness},{"particle",c.particle},{"seed",c.project.seed},{"planned_histories",c.project.events},
    {"expected_primaries",c.project.expectedPrimaries()},{"project",c.project.toJson()}});
    return {{"schema","scdat.radiation-shielding-scan-plan/1"},{"cases",rows},{"options",o.toJson()}};
}
Json shieldingCurves(const std::vector<ShieldingCase>& plan,const std::vector<Json>& results){
    require(plan.size()==results.size(),"case/result slots differ");Json rows=Json::array();
    for(std::size_t first=0;first<plan.size();){std::size_t end=first;while(end<plan.size()&&plan[end].thickness==plan[first].thickness)++end;
        bool complete=true;for(auto i=first;i<end;++i){const auto& r=results[i];if(r.is_null()||r.at("status")!="completed"||r.at("histories")==0){complete=false;break;}
            require(physical(Project::fromJson(r.at("project")).toJson())==physical(plan[i].project.toJson()),"case result physical inputs differ");}
        if(complete){const auto& targets=results[first].at("targets");for(const auto& t:targets){const auto id=t.at("id").get<std::string>();Json components=Json::array(),zeros=Json::array();
            double dose=0,variance=0;bool converged=true;for(auto i=first;i<end;++i){const auto& r=results[i];const Json* found=nullptr;
                require(r.at("targets").size()==targets.size(),"case target sets differ");for(const auto& target:r.at("targets"))if(target.at("id")==id)found=&target;
                require(found&&found->at("mass_kg")==t.at("mass_kg"),"case target IDs or masses differ");const auto& target=*found;
                double d=target.at("tid_Gy"),se=target.at("tid_standard_error_Gy");require(std::isfinite(d)&&d>=0&&std::isfinite(se)&&se>=0,"nonfinite or negative dose/error");
                dose+=d;variance+=se*se;bool ok=false;for(const auto& a:r.at("batch").at("convergence").at("targets"))if(a.at("id")==id)ok=a.at("converged").get<bool>();
                if(target.at("relative_standard_error").is_null())zeros.push_back(plan[i].particle);converged=converged&&ok;
                components.push_back({{"particle",plan[i].particle},{"tid_Gy",d},{"tid_standard_error_Gy",se},{"relative_standard_error",target.at("relative_standard_error")},
                    {"nonzero_histories",target.at("nonzero_histories")},{"histories",r.at("histories")},{"converged",ok}});
            }
            require(std::isfinite(dose)&&std::isfinite(variance),"combined dose/error overflow");double error=std::sqrt(variance);
            auto geometry=geometrySnapshot(plan[first].project);std::string material;for(const auto& v:geometry.at("volumes"))if(v.at("id")==id)material=v.at("material").get<std::string>();
            rows.push_back({{"thickness_mm",plan[first].thickness},{"target_id",id},{"material",material},{"mass_kg",t.at("mass_kg")},
                {"tid_Gy",dose},{"tid_rad",dose*100},{"tid_standard_error_Gy",error},{"relative_standard_error",dose>0&&zeros.empty()?Json(error/dose):Json(nullptr)},
                {"converged",converged&&zeros.empty()},{"zero_hit_components",zeros},{"components",components}});
        }}first=end;
    }return rows;
}
QString shieldingCsv(const Json& result){
    QString csv="thickness_mm,target_id,material,component,tid_Gy,tid_rad,standard_error_Gy,relative_standard_error,converged,zero_hit_components\r\n";
    for(const auto& row:result.at("curves")){
        auto line=[&](const std::string& name,const Json& d){csv+=csvNumber(row.at("thickness_mm"))+','+csvText(row.at("target_id"))+','+csvText(row.at("material"))+','+csvText(name)+','+
            csvNumber(d.at("tid_Gy"))+','+QString::number(d.at("tid_Gy").get<double>()*100,'g',17)+','+csvNumber(d.at("tid_standard_error_Gy"))+','+
            csvNumber(d.at("relative_standard_error"))+','+(d.at("converged")==true?"true":"false")+','+csvText(row.at("zero_hit_components").dump())+"\r\n";};
        line("total",row);for(const auto& c:row.at("components"))line(c.at("particle"),c);
    }return csv;
}
}
