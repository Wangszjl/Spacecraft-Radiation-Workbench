#include <spacerad/engine/SourceSampling.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace spacerad::engine {
namespace {
void require(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
double finite(const Json& x) {
    require(x.is_number() && std::isfinite(x.get<double>()), "Invalid importance sampling number");
    return x.get<double>();
}
int count(const Json& x, int limit) {
    require(x.is_number_integer() && x >= 0 && x <= limit, "Invalid importance history count");
    return x.get<int>();
}
bool near(double a, double b) {
    return std::isfinite(a) && std::isfinite(b) &&
           std::abs(a-b) <= 1e-9 * std::max({std::abs(a),std::abs(b),1e-280});
}
double spectrumMeanEnergy(const Spectrum& spectrum) {
    double firstMoment=0;
    if(spectrum.interpolation=="bin_constant"){
        for(size_t i=0;i<spectrum.values.size();++i)
            firstMoment+=spectrum.values[i]*(spectrum.energy[i+1]*spectrum.energy[i+1]-spectrum.energy[i]*spectrum.energy[i])/2;
    }else{
        for(size_t i=0;i+1<spectrum.values.size();++i){
            const double low=spectrum.energy[i],width=spectrum.energy[i+1]-low;
            const double slope=(spectrum.values[i+1]-spectrum.values[i])/width;
            const double area=width*(spectrum.values[i]+spectrum.values[i+1])/2;
            firstMoment+=low*area+spectrum.values[i]*width*width/2+slope*width*width*width/3;
        }
    }
    return firstMoment/spectrum.integral;
}
double expectedPrimaryMeanEnergy(const Project& project) {
    double weighted=0;
    for(const auto& spectrum:project.spectra)
        weighted+=spectrum.integral/project.integratedSpectrum()*spectrumMeanEnergy(spectrum);
    return weighted;
}
}

ImportanceSampling ImportanceSampling::fromJson(const Json& j) {
    require(j.is_object() && (j.size()==3||j.size()==4) && j.at("schema")=="spacerad.source-importance/1",
            "Invalid source importance sampling declaration");
    for(const auto& row:j.items())
        require(row.key()=="schema"||row.key()=="physical_fraction"||row.key()=="tail_fraction"||
                row.key()=="proposal_species_fractions","Unknown source importance sampling setting");
    auto physical=finite(j.at("physical_fraction"));
    auto tail=finite(j.at("tail_fraction"));
    require(physical>=0.05 && physical<1 && tail>=1e-5 && tail<=0.5,
            "Importance sampling requires physical fraction [0.05,1) and tail fraction [1e-5,0.5]");
    ImportanceSampling result{physical,tail,{}};
    if(j.contains("proposal_species_fractions")){
        const auto& values=j.at("proposal_species_fractions");
        require(values.is_object()&&!values.empty(),"Importance species proposal must be a nonempty object");
        double total=0;
        for(const auto& [species,value]:values.items()){
            require(species=="e-"||species=="proton","Importance species proposal only supports e- and proton");
            const auto fraction=finite(value);
            require(fraction>0&&fraction<=1,"Species proposal fraction must be in (0,1]");
            result.proposalSpecies[species]=fraction;total+=fraction;
        }
        require(std::abs(total-1)<=1e-12,"Species proposal fractions must sum to one");
    }
    return result;
}

double ImportanceSampling::proposalTailFraction() const { return 1-physicalFraction*(1-tailFraction); }
double ImportanceSampling::bodyWeight() const { return 1/physicalFraction; }
double ImportanceSampling::tailWeight() const { return tailFraction/proposalTailFraction(); }

ImportanceSample ImportanceSampling::sample(double u) const {
    require(std::isfinite(u) && u>=0 && u<1, "Invalid source quantile");
    const double body=physicalFraction*(1-tailFraction);
    if (u<body) return {std::min(u/physicalFraction,std::nextafter(1-tailFraction,0.0)),bodyWeight(),false};
    const double q=1-tailFraction+(u-body)*tailWeight();
    return {std::min(q,std::nextafter(1.0,0.0)),tailWeight(),true};
}

Json ImportanceSampling::proposalSpeciesFractions(const Project& project) const {
    Json result=Json::object();
    for(const auto& spectrum:project.spectra){
        const auto configured=proposalSpecies.find(spectrum.particle);
        result[spectrum.particle]=configured==proposalSpecies.end()?spectrum.integral/project.integratedSpectrum():configured->second;
    }
    return result;
}

std::pair<size_t,double> ImportanceSampling::sampleSpecies(double u,const Project& project) const {
    require(std::isfinite(u)&&u>=0&&u<1&&!project.spectra.empty(),"Invalid weighted species selector");
    double cumulative=0;
    for(size_t i=0;i<project.spectra.size();++i){
        const auto& spectrum=project.spectra[i];
        const double physical=spectrum.integral/project.integratedSpectrum();
        const auto configured=proposalSpecies.find(spectrum.particle);
        const double proposal=configured==proposalSpecies.end()?physical:configured->second;
        cumulative+=proposal;
        if(u<cumulative||i+1==project.spectra.size())return {i,physical/proposal};
    }
    throw std::runtime_error("Importance species selector did not resolve");
}

double ImportanceSampling::speciesWeight(const std::string& particle,const Project& project) const {
    const auto spectrum=std::find_if(project.spectra.begin(),project.spectra.end(),[&](const Spectrum& s){return s.particle==particle;});
    require(spectrum!=project.spectra.end(),"Importance weight requested for unknown species");
    const auto configured=proposalSpecies.find(particle);
    const double physical=spectrum->integral/project.integratedSpectrum();
    const double proposal=configured==proposalSpecies.end()?physical:configured->second;
    return physical/proposal;
}

void validateImportanceResult(const Project& p, const Json& r) {
    if (p.sourceSampling.is_null()) {
        require(!r.contains("source_importance"),"Unexpected weighted result for nominal source");
        return;
    }
    auto cfg=ImportanceSampling::fromJson(p.sourceSampling);
    const auto& a=r.at("source_importance");
    const int n=p.events, tail=count(a.at("tail_histories"),n), body=count(a.at("body_histories"),n);
    const auto w=cfg.bodyWeight(), v=cfg.tailWeight();
    const auto& speciesAudit=r.at("source_diagnostics").at("species_histories");
    const auto& speciesTail=a.at("tail_histories_by_species");
    require(speciesAudit.is_object()&&speciesTail.is_object()&&
            speciesAudit.size()==p.spectra.size()&&speciesTail.size()==speciesAudit.size(),
            "Weighted incident species counts differ from source project");
    std::map<std::string,std::array<int,2>> speciesCounts;
    int tailSum=0,totalSpecies=0;
    double sum=0,sum2=0;
    const double spectrumTotal=p.integratedSpectrum();
    for(const auto& [particle,totalValue]:speciesAudit.items()){
        const int total=count(totalValue,n);
        const auto spectrum=std::find_if(p.spectra.begin(),p.spectra.end(),[&](const Spectrum& s){return s.particle==particle;});
        require(spectrum!=p.spectra.end(),"Weighted proposal diagnostics contain an unknown species");
        const double speciesProbability=spectrum->integral/spectrumTotal;
        const auto configured=cfg.proposalSpecies.find(particle);
        const double proposalSpecies=configured==cfg.proposalSpecies.end()?speciesProbability:configured->second;
        const double speciesError=std::sqrt(proposalSpecies*(1-proposalSpecies)/n);
        require(std::abs(double(total)/n-proposalSpecies)<=6*speciesError+1.0/n,
                "Importance proposal changed the physical species mixture");
        const int high=count(speciesTail.at(particle),total);
        if(total>0){
            const double allocation=double(high)/total;
            const double allocationError=std::sqrt(cfg.proposalTailFraction()*(1-cfg.proposalTailFraction())/total);
            require(std::abs(allocation-cfg.proposalTailFraction())<=6*allocationError+1.0/total,
                    "Weighted per-species tail allocation is inconsistent with proposal");
        }else require(high==0,"A zero-history species has tail events");
        speciesCounts[particle]={total-high,high};
        tailSum+=high;
        totalSpecies+=total;
        const double correction=speciesProbability/proposalSpecies;
        sum+=correction*((total-high)*w+high*v);
        sum2+=correction*correction*((total-high)*w*w+high*v*v);
    }
    require(tailSum==tail && totalSpecies==n,"Weighted total proposal counts differ from species diagnostics");
    require(a.at("schema")=="spacerad.source-importance-result/1" && a.at("configuration")==p.sourceSampling &&
            a.at("proposal_species_fractions")==cfg.proposalSpeciesFractions(p) &&
            tail+body==n && r.at("histories")==n &&
            near(finite(a.at("proposal_tail_fraction")),cfg.proposalTailFraction()) &&
            near(finite(a.at("sum_weights")),sum) && near(finite(a.at("sum_squared_weights")),sum2) &&
            near(finite(a.at("weight_ess")),sum*sum/sum2) &&
            a.at("target_support").is_object() &&
            near(finite(r.at("expected_primaries")),p.expectedPrimaries()),"Weighted source diagnostics differ from launch configuration");
    require(a.at("target_support").size()==r.at("targets").size(),
            "Missing weighted target support");
    for (const auto& target:r.at("targets")) {
        const auto& id=target.at("id").get_ref<const std::string&>();
        const auto& support=a.at("target_support").at(id);
        require(support.is_object()&&support.size()==speciesCounts.size(),"Weighted target species support differs");
        const double mass=finite(target.at("mass_kg"));
        require(mass>0,"Weighted target mass is invalid");
        const double factor=p.expectedPrimaries()*1.602176634e-13/mass;
        const auto& scores=r.at("score_moments").at(id);
        for (const auto& pair:{std::pair{"ionizing","tid_Gy"},std::pair{"total","total_absorbed_Gy"},std::pair{"nonionizing","nonionizing_Gy"}}) {
            const auto& m=scores.at(pair.first);
            const auto mean=finite(m.at("mean_MeV")), m2=finite(m.at("m2_MeV2"));
            const int hits=count(m.at("nonzero_histories"),n);
            const int channel=std::string(pair.first)=="ionizing"?0:std::string(pair.first)=="total"?1:2;
            double regionalMean=0,regionalM2=0,regionalFrequencySquares=0;
            int regionalHits=0;
            for(const auto& [particle,counts]:speciesCounts){
                const auto& regions=support.at(particle);
                for(int regionIndex=0;regionIndex<2;++regionIndex){
                    const auto& region=regions.at(regionIndex==0?"body":"tail");
                    const int selected=count(region.at("histories"),n);
                    require(selected==counts[regionIndex]&&
                            region.at("score_moments").size()==3,
                            "Weighted proposal region history count differs");
                    const auto& raw=region.at("score_moments").at(channel);
                    require(raw.at("count")==selected,"Weighted regional moment count differs");
                    const double regionMean=finite(raw.at("mean_MeV"));
                    const double regionM2=finite(raw.at("m2_MeV2"));
                    const int nz=count(raw.at("nonzero_histories"),selected);
                    require(regionMean>=0&&regionM2>=0&&(regionMean==0)==(nz==0)&&
                            (nz>0||regionM2==0),"Invalid weighted conditional event moments");
                    if(channel==0)require(count(region.at("nonzero_histories"),selected)==nz,
                                          "Weighted region hit summary differs from moments");
                    regionalMean+=double(selected)/n*regionMean;
                    regionalM2+=regionM2;
                    regionalFrequencySquares+=double(selected)*regionMean*regionMean;
                    regionalHits+=nz;
                }
            }
            const double pooledM2=regionalM2+regionalFrequencySquares-double(n)*mean*mean;
            require(m.at("count")==n && regionalHits==hits && mean>=0 && m2>=0 && (mean==0)==(hits==0) &&
                    (hits>0 || m2==0) && near(finite(target.at(pair.second)),mean*factor),
                    "Weighted dose differs from event moments");
            const double momentScale=std::max({std::abs(pooledM2),std::abs(m2),std::abs(regionalM2),
                regionalFrequencySquares+double(n)*mean*mean,1e-280});
            require(std::abs(pooledM2-m2)<=1e-8*momentScale,
                    "Weighted species/region moments do not reconstruct full moments");
            require(near(regionalMean,mean),"Weighted species/region means do not reconstruct full mean");
            if (std::string(pair.first)=="ionizing") {
                const auto se=std::sqrt(m2/(double(n)*(n-1)));
                require(count(target.at("nonzero_histories"),n)==hits &&
                        near(finite(target.at("tid_standard_error_Gy")),se*factor) &&
                        near(finite(target.at("tid_rad")),mean*factor*100) &&
                        (mean>0?near(finite(target.at("relative_standard_error")),se/mean):target.at("relative_standard_error").is_null()),
                        "Weighted TID uncertainty differs from event moments");
            }
        }
    }
    const auto& energy=r.at("source_importance").at("mean_physical_primary_energy_MeV");
    const auto& energySe=r.at("source_importance").at("mean_physical_primary_energy_standard_error_MeV");
    const double measured=finite(energy),error=finite(energySe),expected=expectedPrimaryMeanEnergy(p);
    require(error>=0 && std::abs(measured-expected)<=6*error+1e-9*std::max(expected,1e-280),
            "Weighted physical primary-energy mean differs from the declared spectrum");
    require(r.at("source_diagnostics").at("histories")==n,
            "Weighted source diagnostics history count differs");
}
}
