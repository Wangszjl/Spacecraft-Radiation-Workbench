#pragma once
#include "RadiationProject.h"
#include <map>
#include <set>

class G4VUserDetectorConstruction;
class G4VUserPrimaryGeneratorAction;
class G4UserEventAction;
class G4UserSteppingAction;
class G4VModularPhysicsList;
class G4VPhysicalVolume;

namespace spacerad::engine::transport {
struct Score {
    Moments ionizing, total, nonionizing;
    double mass = 0;
    int primaryEntryHistories = 0;
};
struct SourceAudit {
    Moments z, mu, mu2, energy;
    std::array<int,10> muBins{};
    std::map<std::string,int> species;
    double maximumPositionError = 0, maximumDirectionNormError = 0;
    double maximumCosineMappingError = 0;
    int outwardDirections = 0;
};
struct State {
    const Project& project;
    std::map<std::string,Score> scores;
    std::map<std::string,std::array<double,3>> event;
    std::set<std::string> primaryEntries;
    SourceAudit sourceAudit;
    Json segments = Json::array();
    Json geometryAudit;
    Json materialAudit;
    G4VPhysicalVolume* voxelPhysical=nullptr;
    Json voxelAudit;
    std::vector<std::array<Moments,3>> voxelMoments;
    std::map<int,std::array<double,3>> voxelEvent;
    std::map<std::string,std::vector<std::int64_t>> edepBins;
    std::map<std::string,std::int64_t> edepOverflow;
    int eventId = 0;
    explicit State(const Project& p):project(p) {}
};

G4VUserDetectorConstruction* makeGeometry(State&);
G4VUserPrimaryGeneratorAction* makeSource(State&);
G4UserEventAction* makeEventAction(State&);
G4UserSteppingAction* makeStepAction(State&);
G4VModularPhysicsList* makePhysics(const Project&);
Json runTransport(const Project&);
Json buildScores(const State&);
} // namespace spacerad::engine::transport
