#include "TransportState.h"
#include <spacerad/engine/SourceSampling.h>
#include <G4VUserPrimaryGeneratorAction.hh>
#include <G4ParticleGun.hh>
#include <G4ParticleTable.hh>
#include <G4SystemOfUnits.hh>
#include <Randomize.hh>
#include <algorithm>
#include <cmath>
#include <optional>
namespace spacerad::engine::transport {
class Source final:public G4VUserPrimaryGeneratorAction {
    State& state;
    const Project& p;
    G4ParticleGun gun{1};
    std::optional<ImportanceSampling> sampling;
public:
    explicit Source(State& s):state(s),p(s.project) {
        if(!p.sourceSampling.is_null()) {
            sampling=ImportanceSampling::fromJson(p.sourceSampling);
            for(const auto& spectrum:p.spectra){
                state.sourceAudit.tailHistoriesBySpecies[spectrum.particle]=0;
                state.sourceAudit.species[spectrum.particle]=0;
            }
        }
    }
    void GeneratePrimaries(G4Event* event) override {
        double selector=G4UniformRand();
        const Spectrum* chosen=&p.spectra.back();
        if(sampling){
            const auto selected=sampling->sampleSpecies(selector,p);
            chosen=&p.spectra.at(selected.first);
            state.eventWeight=selected.second;
        }else{
            selector*=p.integratedSpectrum();
            for(const auto& s:p.spectra) { selector-=s.integral; if(selector<0) { chosen=&s; break; } }
        }
        gun.SetParticleDefinition(G4ParticleTable::GetParticleTable()->FindParticle(chosen->particle));
        double quantile=G4UniformRand();
        if(sampling){
            const auto sample=sampling->sample(quantile);
            quantile=sample.physicalQuantile;
            state.eventWeight*=sample.weight;
            state.tailEvent=sample.tail;
            if(sample.tail) ++state.sourceAudit.tailHistories;
        }
        double energy = chosen->sample(quantile);
        state.eventParticle=chosen->particle;
        if(sampling)state.sourceAudit.tailHistoriesBySpecies.try_emplace(chosen->particle,0);
        if(state.tailEvent)++state.sourceAudit.tailHistoriesBySpecies[chosen->particle];
        gun.SetParticleEnergy(energy*MeV);
        // Uniform sphere position; inward Lambert cosine direction: mu=sqrt(U).
        double z=2*G4UniformRand()-1, az=2*pi*G4UniformRand();
        double radial=std::sqrt(std::max(0.0,1-z*z));
        G4ThreeVector outward(radial*std::cos(az),radial*std::sin(az),z);
        G4ThreeVector inward=-outward;
        auto tangent=inward.orthogonal().unit(); auto second=inward.cross(tangent);
        double mu=std::sqrt(G4UniformRand()), phi=2*pi*G4UniformRand();
        auto direction=mu*inward+std::sqrt(1-mu*mu)*(std::cos(phi)*tangent+std::sin(phi)*second);
        gun.SetParticlePosition(p.radius*mm*outward);
        gun.SetParticleMomentumDirection(direction);
        // Diagnostics must never draw random numbers: preserve the P0 stream.
        auto& audit=state.sourceAudit;
        audit.likelihoodWeightedEnergy.add(state.eventWeight*energy);
        const auto actualPosition=gun.GetParticlePosition()/(p.radius*mm);
        const auto actualDirection=gun.GetParticleMomentumDirection();
        double actualMu=-actualDirection.dot(actualPosition.unit());
        audit.z.add(actualPosition.z()/actualPosition.mag());audit.mu.add(actualMu);
        audit.mu2.add(actualMu*actualMu);audit.energy.add(energy);
        ++audit.muBins[std::clamp(int(actualMu*10),0,9)];++audit.species[chosen->particle];
        audit.maximumPositionError=std::max(audit.maximumPositionError,std::abs(actualPosition.mag()-1.0));
        audit.maximumDirectionNormError=std::max(audit.maximumDirectionNormError,std::abs(actualDirection.mag()-1.0));
        audit.maximumCosineMappingError=std::max(audit.maximumCosineMappingError,std::abs(actualMu-mu));
        if(actualDirection.dot(actualPosition)>=0) ++audit.outwardDirections;
        gun.GeneratePrimaryVertex(event);
    }
};

G4VUserPrimaryGeneratorAction* makeSource(State& s) { return new Source(s); }
}
