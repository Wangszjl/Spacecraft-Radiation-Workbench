#pragma once
#include "RadiationProject.h"
#include "runtime/TargetFluence.h"
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
struct ImportanceSupport {
    int histories = 0;
    std::array<int,3> nonzero{};
    std::array<Moments,3> score;
};
struct SourceAudit {
    Moments z, mu, mu2, energy;
    Moments likelihoodWeightedEnergy;
    std::array<int,10> muBins{};
    std::map<std::string,int> species;
    double maximumPositionError = 0, maximumDirectionNormError = 0;
    double maximumCosineMappingError = 0;
    int outwardDirections = 0;
    int tailHistories = 0;
    std::map<std::string,int> tailHistoriesBySpecies;
};
struct State {
    const Project& project;
    std::map<std::string,Score> scores;
    std::map<std::string,std::map<std::string,std::array<ImportanceSupport,2>>> importanceSupport;
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
    TargetFluenceSpec fluence;
    std::string fluenceHash;
    std::map<size_t,double> fluenceEvent;
    std::vector<Moments> fluenceMoments;
    Moments fluenceTotal;
    int eventId = 0;
    double eventWeight = 1;
    bool tailEvent = false;
    std::string eventParticle;
    explicit State(const Project& p):project(p) {}
};

G4VUserDetectorConstruction* makeGeometry(State&);
G4VUserPrimaryGeneratorAction* makeSource(State&);
G4UserEventAction* makeEventAction(State&);
G4UserSteppingAction* makeStepAction(State&);
G4VModularPhysicsList* makePhysics(const Project&);
Json runTransport(const Project&,const Json& fluence=nullptr,const std::string& settingsHash={});
Json buildScores(const State&);
} // namespace spacerad::engine::transport
