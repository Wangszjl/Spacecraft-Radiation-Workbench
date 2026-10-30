#include "Voxels.h"
#include "Geometry.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace spacerad::engine {
namespace {void require(bool b,const char* m){if(!b)throw std::runtime_error(std::string("Voxels: ")+m);}}
VoxelSpec voxelSpec(const Json& j){
    require(j.is_object()&&j.size()==5&&j.at("schema")=="scdat.voxel-scoring/1","unknown/missing/extra voxel settings");
    VoxelSpec s;s.volumeId=j.at("volume_id").get<std::string>();require(!s.volumeId.empty(),"empty target ID");
    const auto& n=j.at("counts");require(n.is_array()&&n.size()==3,"counts must be three integers");
    for(int i=0;i<3;++i){require(n[i].is_number_integer(),"counts must be integers");auto v=n[i].get<std::int64_t>();require(v>=1&&v<=32,"axis count outside 1..32");s.counts[i]=int(v);}
    require(s.size()<=4096,"grid exceeds 4096 cells");
    const auto& edges=j.at("edep_edges_MeV");require(edges.is_array()&&edges.size()>=2&&edges.size()<=129,"require 2..129 energy edges");
    double previous=-1;for(const auto& e:edges){require(e.is_number(),"energy edges must be numeric");double v=e.get<double>();require(std::isfinite(v)&&v>=0&&v>previous&&v<=1e9,"energy edges must be finite, increasing and within 0..1e9 MeV");s.edges.push_back(v);previous=v;}
    require(s.edges.front()==0,"deposited-energy histogram starts at zero");
    const auto& q=j.at("quality");require(q.is_object()&&q.size()==2&&q.at("min_nonzero_histories").is_number_integer()&&q.at("max_relative_error").is_number(),"invalid voxel quality criterion");
    auto hits=q.at("min_nonzero_histories").get<std::int64_t>();s.maxRse=q.at("max_relative_error");
    require(hits>=2&&hits<=100000000&&std::isfinite(s.maxRse)&&s.maxRse>=1e-9&&s.maxRse<=1,"invalid voxel quality thresholds");s.minNonzero=int(hits);return s;
}
void validateVoxels(const Project& p){if(p.voxelScoring.is_null())return;auto s=voxelSpec(p.voxelScoring);auto g=compileGeometry(p);const Volume* v=nullptr;
    for(const auto& row:g.volumes)if(row.id==s.volumeId)v=&row;
    require(v&&v->sensitive&&v->shape=="box"&&v->wall==0,"voxel target must be a homogeneous solid sensitive box");
    for(int i=0;i<3;++i)require(v->size[i]/s.counts[i]>=1e-4,"cell width must be at least 1e-4 mm");
}
Json voxelLayout(const Project& p){validateVoxels(p);auto s=voxelSpec(p.voxelScoring);auto g=compileGeometry(p);
    for(const auto& resolved:g.placements){const auto& v=g.volumes[resolved.index];if(v.id!=s.volumeId)continue;std::array<double,3> spacing{},origin{};
        for(int i=0;i<3;++i){spacing[i]=v.size[i]/s.counts[i];origin[i]=-v.size[i]/2;}
        return {{"volume_id",s.volumeId},{"counts",s.counts},{"spacing_mm",spacing},{"origin_local_mm",origin},
            {"world_center_mm",resolved.transform.translation},{"world_rotation",resolved.transform.rotation},{"material",v.material},
            {"cell_volume_mm3",spacing[0]*spacing[1]*spacing[2]},
            {"index_order","i + nx*(j + ny*k); local X fastest"}};
    }throw std::runtime_error("Voxel layout target missing");
}
void padZeroHistories(Moments& m,int count){require(count>=m.count&&count<=100000000,"invalid zero-padding count");int gap=count-m.count;if(!gap)return;
    if(m.count){m.m2+=m.mean*m.mean*double(m.count)*gap/count;m.mean*=double(m.count)/count;}m.count=count;
}
}
