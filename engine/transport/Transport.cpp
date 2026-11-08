#include "TransportState.h"
#include "Voxels.h"
#include <G4RunManager.hh>
#include <G4VUserDetectorConstruction.hh>
#include <G4VUserPrimaryGeneratorAction.hh>
#include <G4VModularPhysicsList.hh>
#include <G4UserEventAction.hh>
#include <G4UserSteppingAction.hh>
#include <Randomize.hh>
#include <memory>
#include <stdexcept>
namespace spacerad::engine::transport {
Json runTransport(const Project& p,const Json& fluence,const std::string& settingsHash) {
    p.validate();
    State state(p);
    if(!fluence.is_null()){
        state.fluence=TargetFluenceSpec::fromJson(fluence);
        state.fluence.volume(p);
        state.fluenceHash=settingsHash;
        state.fluenceMoments.resize(fluenceSpecies().size()*(state.fluence.edges.size()+1)+1);
    }
    G4Random::setTheSeed(p.seed);
    auto manager=std::make_unique<G4RunManager>(); // serial baseline
    manager->SetUserInitialization(makeGeometry(state));
    manager->SetUserInitialization(makePhysics(p));
    manager->SetUserAction(makeSource(state));
    manager->SetUserAction(makeEventAction(state));
    manager->SetUserAction(makeStepAction(state));
    manager->Initialize();manager->BeamOn(p.events);
    if(state.sourceAudit.mu.count!=p.events) throw std::runtime_error("Source history count mismatch");
    for(auto& cell:state.voxelMoments)for(auto& m:cell)padZeroHistories(m,p.events);
    for(const auto& entry:state.scores)
        if(entry.second.ionizing.count!=p.events) throw std::runtime_error("Run ended before all requested histories completed");
    for(auto& m:state.fluenceMoments)padZeroHistories(m,p.events);
    return buildScores(state);
}
}
