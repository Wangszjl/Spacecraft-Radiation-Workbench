#include "TransportState.h"
#include <G4PhysListFactory.hh>
#include <G4VModularPhysicsList.hh>
#include <G4SystemOfUnits.hh>
#include <stdexcept>
namespace spacerad::engine::transport {
G4VModularPhysicsList* makePhysics(const Project& p) {
    G4PhysListFactory factory;
    auto* physics=factory.GetReferencePhysList(p.physics);
    if(!physics) throw std::runtime_error("Physics list unavailable");
    physics->SetDefaultCutValue(p.cut*mm);
    return physics;
}
}
