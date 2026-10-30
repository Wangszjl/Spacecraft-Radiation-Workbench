#include "TransportState.h"
#include <G4VUserPrimaryGeneratorAction.hh>
#include <G4ParticleGun.hh>
#include <G4ParticleTable.hh>
#include <G4SystemOfUnits.hh>
#include <Randomize.hh>
#include <algorithm>
#include <cmath>
namespace spacerad::engine::transport {
class Source final:public G4VUserPrimaryGeneratorAction {
    State& state;
    const Project& p;
    G4ParticleGun gun{1};
public:
    explicit Source(State& s):state(s),p(s.project) {}
    void GeneratePrimaries(G4Event* event) override {
        double selector=G4UniformRand()*p.integratedSpectrum();
        const Spectrum* chosen=&p.spectra.back();
        for(const auto& s:p.spectra) { selector-=s.integral; if(selector<0) { chosen=&s; break; } }
        gun.SetParticleDefinition(G4ParticleTable::GetParticleTable()->FindParticle(chosen->particle));
        double energy = chosen->sample(G4UniformRand());
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
