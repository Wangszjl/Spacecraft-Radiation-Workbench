#include "Geometry.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace spacerad::engine {
namespace {
constexpr double tolerance=1e-8;
double dot(const Vector3& a,const Vector3& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
double norm(const Vector3& v){return std::hypot(v[0],v[1],v[2]);}
Vector3 minus(const Vector3& a,const Vector3& b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Vector3 localDirection(const Transform& t,const Vector3& d){Vector3 q{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)q[i]+=t.rotation[j][i]*d[j];return q;}
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
Vector3 support(const Volume& v,const Transform& t,const Vector3& direction){
    auto d=localDirection(t,direction);Vector3 q{};
    if(v.shape=="sphere"){double length=norm(d);for(int k=0;k<3;++k)q[k]=length>0?v.size[0]*0.5*d[k]/length:0;}
    else if(v.shape=="cylinder"){double radial=std::hypot(d[0],d[1]);for(int k=0;k<2;++k)q[k]=radial>0?v.size[0]*0.5*d[k]/radial:0;q[2]=d[2]>=0?v.size[2]/2:-v.size[2]/2;}
    else for(int k=0;k<3;++k)q[k]=d[k]>=0?v.size[k]/2:-v.size[k]/2;
    return t.apply(q);
}
// Closest point to the origin in a simplex. Enumerate its faces and solve
// affine barycentric coordinates, retaining only nonnegative weights.
// At most five candidate points; the retained face has at most four points.
Vector3 reduceSimplex(std::vector<Vector3>& simplex){
    double best=std::numeric_limits<double>::infinity();Vector3 closest{};std::vector<Vector3> retained;
    for(unsigned mask=1;mask<(1u<<simplex.size());++mask){
        std::vector<int> ids;for(int k=0;k<int(simplex.size());++k)if(mask&(1u<<k))ids.push_back(k);
        if(ids.size()>4)continue;int n=int(ids.size())-1;double matrix[3][4]{};Vector3 edges[3]{};double weights[4]={1,0,0,0};
        const auto& base=simplex[ids[0]];double scale=1;
        for(int i=0;i<n;++i){edges[i]=minus(simplex[ids[i+1]],base);scale=std::max(scale,norm(edges[i]));}
        for(int i=0;i<n;++i){for(int j=0;j<n;++j)matrix[i][j]=dot(edges[i],edges[j])/(scale*scale);matrix[i][n]=-dot(edges[i],base)/(scale*scale);}
        bool singular=false;
        for(int k=0;k<n;++k){int pivot=k;for(int j=k+1;j<n;++j)if(std::abs(matrix[j][k])>std::abs(matrix[pivot][k]))pivot=j;
            if(std::abs(matrix[pivot][k])<1e-15){singular=true;break;}for(int j=k;j<=n;++j)std::swap(matrix[k][j],matrix[pivot][j]);
            double divisor=matrix[k][k];for(int j=k;j<=n;++j)matrix[k][j]/=divisor;
            for(int i=0;i<n;++i)if(i!=k){double factor=matrix[i][k];for(int j=k;j<=n;++j)matrix[i][j]-=factor*matrix[k][j];}}
        if(singular)continue;bool interior=true;
        for(int k=0;k<n;++k){weights[k+1]=matrix[k][n];weights[0]-=weights[k+1];}
        Vector3 point{};for(int k=0;k<=n;++k){if(weights[k]<0)interior=false;for(int j=0;j<3;++j)point[j]+=weights[k]*simplex[ids[k]][j];}
        double distance=norm(point);if(interior&&distance<best){best=distance;closest=point;retained.clear();for(int k=0;k<=n;++k)if(weights[k]>1e-14)retained.push_back(simplex[ids[k]]);}
    }
    require(!retained.empty(),"Unable to resolve curved-solid intersection simplex");simplex=std::move(retained);return closest;
}
}
double materialVolume(const Volume& v){
    if(v.shape=="sphere")return pi*v.size[0]*v.size[0]*v.size[0]/6;
    if(v.shape=="cylinder")return pi*v.size[0]*v.size[0]*v.size[2]/4;
    require(v.shape=="box","Unknown analytic shape: "+v.shape);double volume=0;for(const auto& b:materialBoxes(v))volume+=b.size[0]*b.size[1]*b.size[2];return volume;
}
void validateVolumeShape(const Volume& v){
    require(v.shape=="box"||v.shape=="cylinder"||v.shape=="sphere","Unsupported shape: "+v.id+" / "+v.shape);
    for(double d:v.size)require(std::isfinite(d)&&d>0,"Invalid geometry dimension: "+v.id);
    require(std::isfinite(v.wall)&&v.wall>=0,"Invalid shell wall: "+v.id);
    if(v.shape=="box"){for(double d:v.size)require(v.wall==0||2*v.wall<d,"Shell wall closes its cavity: "+v.id);require(!(v.wall>0&&v.sensitive),"Shell scoring is not supported: "+v.id);}
    else{require(v.wall==0,"Curved shells are unsupported; use a solid cylinder or sphere: "+v.id);
        require(v.size[0]==v.size[1]&&(v.shape!="sphere"||v.size[0]==v.size[2]),"Cylinder requires {D,D,L}; sphere requires {D,D,D}: "+v.id);}
    double volume=materialVolume(v);require(std::isfinite(volume)&&volume>0,"Material volume overflow/underflow: "+v.id);
}
double surfaceArea(const Volume& v){
    if(v.shape=="sphere")return pi*v.size[0]*v.size[0];
    if(v.shape=="cylinder")return pi*v.size[0]*(v.size[2]+v.size[0]/2);
    return 2*(v.size[0]*v.size[1]+v.size[0]*v.size[2]+v.size[1]*v.size[2]);
}
std::array<double,6> worldBounds(const Volume& v,const Transform& t){
    std::array<double,6> bounds{};
    for(int k=0;k<3;++k){double reach=0;
        if(v.shape=="sphere")reach=v.size[0]/2;
        else if(v.shape=="cylinder")reach=v.size[0]*0.5*std::hypot(t.rotation[k][0],t.rotation[k][1])+v.size[2]*0.5*std::abs(t.rotation[k][2]);
        else for(int j=0;j<3;++j)reach+=std::abs(t.rotation[k][j])*v.size[j]/2;
        bounds[2*k]=t.translation[k]-reach;bounds[2*k+1]=t.translation[k]+reach;}
    return bounds;
}
double outerRadiusFromOrigin(const Volume& v,const Transform& t){
    if(v.shape=="sphere")return norm(t.translation)+v.size[0]/2;
    if(v.shape=="cylinder"){auto c=localDirection(t,t.translation);return std::hypot(std::hypot(c[0],c[1])+v.size[0]/2,std::abs(c[2])+v.size[2]/2);}
    double reach=0;for(int mask=0;mask<8;++mask){Vector3 p{};for(int k=0;k<3;++k)p[k]=(mask&(1<<k)?1:-1)*v.size[k]/2;reach=std::max(reach,norm(t.apply(p)));}return reach;
}
bool hasAnalyticGeometry(const Project& p){
    for(const auto& v:p.volumes)if(v.shape!="box")return true;
    for(const auto& t:p.templates)for(const auto& v:t.volumes)if(v.shape!="box")return true;
    return false;
}
// True means overlap OR a numerically unresolved configuration. Only an exact
// analytic distance or a separating support plane may accept a curved pair.
// This deliberately rejects ambiguous contact instead of silently admitting it.
bool convexOverlap(const Volume& a,const Transform& at,const Volume& b,const Transform& bt){
    if(a.shape=="sphere"||b.shape=="sphere"){
        if(b.shape=="sphere"&&a.shape!="sphere")return convexOverlap(b,bt,a,at);
        auto q=localDirection(bt,minus(at.translation,bt.translation));double distance;
        if(b.shape=="sphere")return norm(q)<(a.size[0]+b.size[0])/2-tolerance;
        if(b.shape=="box"){for(int k=0;k<3;++k)q[k]=std::max(std::abs(q[k])-b.size[k]/2,0.0);distance=norm(q);}
        else distance=std::hypot(std::max(std::hypot(q[0],q[1])-b.size[0]/2,0.0),std::max(std::abs(q[2])-b.size[2]/2,0.0));
        return distance<a.size[0]/2-tolerance;
    }
    auto minkowski=[&](Vector3 d){auto opposite=d;for(double& x:opposite)x=-x;return minus(support(a,at,d),support(b,bt,opposite));};
    Vector3 direction=minus(bt.translation,at.translation);if(norm(direction)==0)direction={1,0,0};
    std::vector<Vector3> simplex;Vector3 closest{};
    for(int iteration=0;iteration<128;++iteration){
        double length=norm(direction);if(length<1e-13)return true;for(double& x:direction)x/=length;
        auto point=minkowski(direction);if(dot(point,direction)<=tolerance)return false;
        bool duplicate=false;for(const auto& old:simplex)duplicate|=norm(minus(old,point))<1e-13;
        if(duplicate)return true;simplex.push_back(point);closest=reduceSimplex(simplex);
        direction=closest;for(double& x:direction)x=-x;
    }
    return true;
}
}
