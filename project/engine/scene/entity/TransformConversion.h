#pragma once
#include "EntityWorld.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>

namespace Engine::Scene {
// Authored entities store local S * Rx * Ry * Rz; shared by reparenting and gizmos.
// Do not silently approximate shear or accept an unstable parent inverse.
inline bool WorldToLocalTransform(const Matrix4x4& world, const Matrix4x4& parent,
    const Scene::TransformComponent& reference, Scene::TransformComponent& output, std::string& error) {
    const auto reject=[&](const char* why) { error=why; return false; };
    for (const auto* matrix : {&world,&parent}) {
        for (const auto& row : matrix->m) for (float v : row) if (!std::isfinite(v)) return reject("Transform matrix is not finite");
        if (std::abs(matrix->m[0][3])>1e-6 || std::abs(matrix->m[1][3])>1e-6 ||
            std::abs(matrix->m[2][3])>1e-6 || std::abs(matrix->m[3][3]-1)>1e-6)
            return reject("Transform matrix must be affine");
    }
    double bound=0;
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) bound=(std::max)(bound,std::abs(double(parent.m[i][j])));
    if (bound==0) return reject("Parent transform is singular");
    double a[3][3];
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) a[i][j]=parent.m[i][j]/bound;
    const double determinant=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-
        a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
    if (std::abs(determinant)<1e-10) return reject("Parent transform is singular or ill-conditioned");
    double inverse[3][3];
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
        const int r1=(j+1)%3,r2=(j+2)%3,c1=(i+1)%3,c2=(i+2)%3;
        inverse[i][j]=(a[r1][c1]*a[r2][c2]-a[r1][c2]*a[r2][c1])/determinant/bound;
    }
    double local[4][3]{};
    for (int i=0;i<4;++i) for (int j=0;j<3;++j) for (int k=0;k<3;++k)
        local[i][j]+=(double(world.m[i][k])-(i==3 ? parent.m[3][k] : 0.0))*inverse[k][j];
    const double signs[3]={reference.scale.x<0 ? -1.0:1.0,reference.scale.y<0 ? -1.0:1.0,reference.scale.z<0 ? -1.0:1.0};
    double scale[3],r[3][3];
    for (int i=0;i<3;++i) {
        scale[i]=std::sqrt(local[i][0]*local[i][0]+local[i][1]*local[i][1]+local[i][2]*local[i][2])*signs[i];
        if (!std::isfinite(scale[i]) || std::abs(scale[i])<1e-6) return reject("Local scale is degenerate");
        for (int j=0;j<3;++j) r[i][j]=local[i][j]/scale[i];
    }
    for (int i=0;i<3;++i) for (int j=i+1;j<3;++j) {
        double dot=0; for (int k=0;k<3;++k) dot+=r[i][k]*r[j][k];
        if (std::abs(dot)>1e-4) return reject("Transform result contains shear; local TRS cannot represent it");
    }
    const double handed=r[0][0]*(r[1][1]*r[2][2]-r[1][2]*r[2][1])-
        r[0][1]*(r[1][0]*r[2][2]-r[1][2]*r[2][0])+r[0][2]*(r[1][0]*r[2][1]-r[1][1]*r[2][0]);
    if (handed<0) { scale[0]=-scale[0]; for (double& v : r[0]) v=-v; }
    double y=std::asin(std::clamp(-r[0][2],-1.0,1.0)),x,z;
    const bool regularRotation=std::abs(std::cos(y))>1e-5;
    if (regularRotation) { x=std::atan2(r[1][2],r[2][2]); z=std::atan2(r[0][1],r[0][0]); }
    else { x=reference.rotation.x; z=std::atan2(-r[1][0],r[1][1])+(y>0 ? x:-x); }
    const auto closestAngle=[](double angle,double previous) { return previous+std::remainder(angle-previous,2*std::numbers::pi); };
    x=closestAngle(x,reference.rotation.x); y=closestAngle(y,reference.rotation.y); z=closestAngle(z,reference.rotation.z);
    if (regularRotation) {
        // Rx(x) Ry(y) Rz(z) == Rx(x+pi) Ry(pi-y) Rz(z+pi).
        // Unwrap each equivalent branch around the current authored angles,
        // then use the nearest one instead of always choosing asin's range.
        const double alternateX=closestAngle(x+std::numbers::pi,reference.rotation.x),
            alternateY=closestAngle(std::numbers::pi-y,reference.rotation.y),
            alternateZ=closestAngle(z+std::numbers::pi,reference.rotation.z);
        const auto distance=[&](double ax,double ay,double az) {
            const double dx=ax-reference.rotation.x,dy=ay-reference.rotation.y,dz=az-reference.rotation.z;
            return dx*dx+dy*dy+dz*dz;
        };
        if (distance(alternateX,alternateY,alternateZ)<distance(x,y,z)) { x=alternateX; y=alternateY; z=alternateZ; }
    }
    for(double value:{local[3][0],local[3][1],local[3][2],x,y,z,scale[0],scale[1],scale[2]})
        if(!std::isfinite(value) || std::abs(value)>(std::numeric_limits<float>::max)()) return reject("Local TRS exceeds finite float range");
    Scene::TransformComponent candidate{{float(local[3][0]),float(local[3][1]),float(local[3][2])},
        {float(x),float(y),float(z)},{float(scale[0]),float(scale[1]),float(scale[2])}};
    for (const auto value : {candidate.position,candidate.rotation,candidate.scale})
        if (!std::isfinite(value.x)||!std::isfinite(value.y)||!std::isfinite(value.z)) return reject("Local TRS exceeds finite float range");
    // Verify the rotation convention/reconstruction, including near-gimbal cases.
    const double cx=std::cos(candidate.rotation.x),sx=std::sin(candidate.rotation.x),
        cy=std::cos(candidate.rotation.y),sy=std::sin(candidate.rotation.y),cz=std::cos(candidate.rotation.z),sz=std::sin(candidate.rotation.z);
    const double rebuilt[3][3]={{cy*cz,cy*sz,-sy},{sx*sy*cz-cx*sz,sx*sy*sz+cx*cz,sx*cy},
        {cx*sy*cz+sx*sz,cx*sy*sz-sx*cz,cx*cy}};
    const double storedScale[3]={candidate.scale.x,candidate.scale.y,candidate.scale.z};
    for (int i=0;i<3;++i) for (int j=0;j<3;++j)
        if (std::abs(rebuilt[i][j]*storedScale[i]-local[i][j])>1e-4*(std::max)(1.0,std::abs(scale[i])))
            return reject("Local TRS reconstruction is unstable");
    output=candidate; error.clear(); return true;
}
}
