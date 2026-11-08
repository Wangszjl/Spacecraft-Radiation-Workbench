#include "TransportState.h"
#include "Voxels.h"
#include <G4UserEventAction.hh>
#include <G4UserSteppingAction.hh>
#include <G4Step.hh>
#include <G4Event.hh>
#include <G4SystemOfUnits.hh>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <stdexcept>
namespace spacerad::engine::transport {
class Event final:public G4UserEventAction {
    State& s;
public:
    explicit Event(State& state):s(state) {}
    void BeginOfEventAction(const G4Event* event) override { s.event.clear(); s.voxelEvent.clear();s.fluenceEvent.clear();s.primaryEntries.clear(); s.eventId=event->GetEventID(); }
    void EndOfEventAction(const G4Event* event) override {
        if(s.voxelPhysical){auto spec=voxelSpec(s.project.voxelScoring);std::array<double,3> sums{};
            for(const auto& e:s.voxelEvent){for(int k=0;k<3;++k){auto& m=s.voxelMoments.at(e.first)[k];padZeroHistories(m,s.eventId);m.add(e.second[k]);sums[k]+=e.second[k];}}
            for(int k=0;k<3;++k){double whole=s.event[spec.volumeId][k];if(std::abs(sums[k]-whole)>1e-10*std::max({std::abs(sums[k]),std::abs(whole),1e-288}))throw std::runtime_error("Event cell/target energy conservation failure");}
        }
        for(auto& entry:s.scores) {
            if(s.primaryEntries.count(entry.first)) ++entry.second.primaryEntryHistories;
            auto energies=s.event[entry.first]; // includes zero-deposit histories
            // Split weighted full-history dose into disjoint proposal regions.
            if(!s.project.sourceSampling.is_null()){
                auto& region=s.importanceSupport[entry.first][s.eventParticle];
                for(int i=0;i<2;++i){
                    const bool selected=(i==1)==s.tailEvent;
                    auto& support=region[i];
                    if(selected)++support.histories;
                    for(int k=0;k<3;++k){
                        if(selected){if(energies[k]>0)++support.nonzero[k];support.score[k].add(energies[k]*s.eventWeight);}
                    }
                }
            }
            // Weight the complete primary history once, including all descendants.
            if(s.eventWeight!=1) for(auto& energy:energies) energy*=s.eventWeight;
            entry.second.ionizing.add(energies[0]);
            entry.second.total.add(energies[1]);
            entry.second.nonionizing.add(energies[2]);
            if(s.voxelPhysical){auto spec=voxelSpec(s.project.voxelScoring);auto& bins=s.edepBins[entry.first];if(bins.empty())bins.resize(spec.edges.size()-1,0);
                auto upper=std::upper_bound(spec.edges.begin(),spec.edges.end(),energies[0]);
                if(upper==spec.edges.end())++s.edepOverflow[entry.first];else ++bins[std::size_t(upper-spec.edges.begin()-1)];}
        }
        if(!s.fluenceMoments.empty()){
            double total=0;
            for(const auto& [index,length]:s.fluenceEvent){
                auto& m=s.fluenceMoments.at(index);
                padZeroHistories(m,s.eventId);
                m.add(length*s.eventWeight);
                total+=length;
            }
            s.fluenceTotal.add(total*s.eventWeight);
        }
        int n=event->GetEventID()+1;
        if(n%std::max(1,s.project.events/100)==0 || n==s.project.events)
            std::cout<<"SCDAT_PROGRESS "<<n<<" "<<s.project.events<<std::endl;
    }
};
class Step final:public G4UserSteppingAction {
    State& s;
public:
    explicit Step(State& state):s(state) {}
    void UserSteppingAction(const G4Step* step) override {
        auto* volume=step->GetPreStepPoint()->GetPhysicalVolume();
        if(!volume) return;
        std::string name=volume->GetName();
        int cellIndex=-1;
        if(volume==s.voxelPhysical){name=voxelSpec(s.project.voxelScoring).volumeId;cellIndex=step->GetPreStepPoint()->GetTouchableHandle()->GetCopyNumber();
            if(cellIndex<0||cellIndex>=int(s.voxelMoments.size()))throw std::runtime_error("Invalid parameterised voxel copy index");}
        if(!s.fluenceMoments.empty()&&name==s.fluence.target&&step->GetStepLength()>0){
            const auto& species=fluenceSpecies();
            const auto& particle=step->GetTrack()->GetDefinition()->GetParticleName();
            const auto it=std::find(species.begin(),species.end(),particle);
            size_t index=s.fluenceMoments.size()-1;
            if(it!=species.end()){
                const auto& edges=s.fluence.edges;
                const double energy=step->GetPreStepPoint()->GetKineticEnergy()/MeV;
                if(!std::isfinite(energy)||energy<0)throw std::runtime_error("Invalid track kinetic energy");
                index=(it-species.begin())*(edges.size()+1)+(std::upper_bound(edges.begin(),edges.end(),energy)-edges.begin());
            }
            s.fluenceEvent[index]+=step->GetStepLength()/mm*step->GetPreStepPoint()->GetWeight();
        }
        if(s.scores.count(name)) {
            if(step->GetTrack()->GetParentID()==0 && step->GetPreStepPoint()->GetStepStatus()==fGeomBoundary)
                s.primaryEntries.insert(name);
            double total=step->GetTotalEnergyDeposit()/MeV;
            double nonion=step->GetNonIonizingEnergyDeposit()/MeV;
            auto& score=s.event[name];
            score[0]+=std::max(0.0,total-nonion); score[1]+=total; score[2]+=nonion;
            if(cellIndex>=0){auto& cell=s.voxelEvent[cellIndex];cell[0]+=std::max(0.0,total-nonion);cell[1]+=total;cell[2]+=nonion;}
            else if(s.voxelPhysical&&name==voxelSpec(s.project.voxelScoring).volumeId&&total>0)throw std::runtime_error("Energy deposited in untiled voxel mother");
        }
        if(s.eventId<16 && s.segments.size()<8000) {
            auto a=step->GetPreStepPoint()->GetPosition()/mm;
            auto b=step->GetPostStepPoint()->GetPosition()/mm;
            s.segments.push_back({{"event",s.eventId},{"track",step->GetTrack()->GetTrackID()},
                {"particle",step->GetTrack()->GetDefinition()->GetParticleName()},
                {"a_mm",{a.x(),a.y(),a.z()}},{"b_mm",{b.x(),b.y(),b.z()}}});
        }
    }
};

G4UserEventAction* makeEventAction(State& s) { return new Event(s); }
G4UserSteppingAction* makeStepAction(State& s) { return new Step(s); }
}
