#pragma once
#include "EntityGizmoTransform.h"
#include "EntityHistory.h"
#include <unordered_map>

namespace Engine::Editor {
inline bool GroupForestRoots(const Scene::EntityWorld& world,const EntitySelection& selection,
    std::vector<Scene::EntityId>& roots,std::string& error) {
    const auto snapshot=selection.Capture();Scene::EntityWorld checked;EntitySelection checkedSelection;
    if(snapshot.ids.empty()){error="Select Entities for group transform";return false;}
    if(!checked.Restore(world.Capture(),error) || !checkedSelection.Restore(checked,snapshot,error))return false;
    std::unordered_map<Scene::EntityId,const Scene::EntityRecord*> records;
    for(const auto& entity:world.GetEntities())records.emplace(entity.id,&entity);
    std::unordered_set<Scene::EntityId> selected(snapshot.ids.begin(),snapshot.ids.end()),top;
    for(const auto id:snapshot.ids) {
        bool covered=false;size_t depth=0;
        for(auto parent=records.at(id)->parent;parent;) {
            if(++depth>records.size() || !records.contains(parent)){error="Group ancestry is invalid";return false;}
            if(selected.contains(parent))covered=true;parent=records.at(parent)->parent;
        }
        if(!covered)top.insert(id);
    }
    roots.clear();for(const auto& entity:world.GetEntities())if(top.contains(entity.id))roots.push_back(entity.id);
    error.clear();return true;
}
inline Matrix4x4 GroupIdentity(){Matrix4x4 value{};for(int i=0;i<4;++i)value.m[i][i]=1;return value;}
inline bool GroupAffine(const Matrix4x4& value,std::string& error);
inline bool BuildGroupPivot(const Scene::EntityWorld& world,const EntitySelection& selection,bool localAxes,
    Matrix4x4& output,std::string& error) {
    std::vector<Scene::EntityId> roots;if(!GroupForestRoots(world,selection,roots,error))return false;
    auto pivot=GroupIdentity();double center[3]{};
    for(const auto id:roots){Matrix4x4 pose{};if(!world.GetWorldMatrix(id,pose)){error="Group World matrix unavailable";return false;}if(!GroupAffine(pose,error))return false;
        for(int axis=0;axis<3;++axis)center[axis]+=double(pose.m[3][axis])/double(roots.size());}
    if(localAxes) {
        size_t depth=0;
        for(auto id=selection.Primary();id;) {
            const auto* entity=world.Find(id);if(!entity || ++depth>world.GetEntities().size()){error="Group primary ancestry invalid";return false;}
            const auto r=entity->transform.rotation;const float cx=std::cos(r.x),sx=std::sin(r.x),cy=std::cos(r.y),sy=std::sin(r.y),cz=std::cos(r.z),sz=std::sin(r.z);
            // Authored rotation frame, independent of parent scale/shear/reflections.
            const Matrix4x4 rotation{{{cy*cz,cy*sz,-sy,0},{sx*sy*cz-cx*sz,sx*sy*sz+cx*cz,sx*cy,0},{cx*sy*cz+sx*sz,cx*sy*sz-sx*cz,cx*cy,0},{0,0,0,1}}};
            pivot=pivot*rotation;id=entity->parent;
        }
    }
    for(int axis=0;axis<3;++axis){if(!std::isfinite(center[axis]) || std::abs(center[axis])>(std::numeric_limits<float>::max)()){error="Group pivot exceeds float range";return false;}pivot.m[3][axis]=float(center[axis]);}
    output=pivot;error.clear();return true;
}
inline bool GroupAffine(const Matrix4x4& value,std::string& error) {
    for(const auto& row:value.m)for(const auto item:row)if(!std::isfinite(item)){error="Group matrix is not finite";return false;}
    if(std::abs(value.m[0][3])>1e-6 || std::abs(value.m[1][3])>1e-6 || std::abs(value.m[2][3])>1e-6 || std::abs(value.m[3][3]-1)>1e-6){error="Group matrix must be affine";return false;}
    return true;
}
inline bool ApplyGroupTransform(Scene::EntityWorld& world,const EntitySelection& selection,const Matrix4x4& from,
    const Matrix4x4& to,int operation,EntityHistory& history,std::string& error) {
    const auto reject=[&](const char* why){error=why;return false;};
    if(operation<0 || operation>2)return reject("Invalid group operation");
    if(!GroupAffine(from,error) || !GroupAffine(to,error))return false;
    std::vector<Scene::EntityId> roots;if(!GroupForestRoots(world,selection,roots,error))return false;
    double bound=0;for(int i=0;i<3;++i)for(int j=0;j<3;++j)bound=(std::max)(bound,std::abs(double(from.m[i][j])));
    if(bound==0)return reject("Group source pivot is singular");
    double a[3][3];for(int i=0;i<3;++i)for(int j=0;j<3;++j)a[i][j]=from.m[i][j]/bound;
    const double determinant=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
    if(std::abs(determinant)<1e-10)return reject("Group source pivot is singular or ill-conditioned");
    double inverse[4][4]{};inverse[3][3]=1;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){const int r1=(j+1)%3,r2=(j+2)%3,c1=(i+1)%3,c2=(i+2)%3;inverse[i][j]=(a[r1][c1]*a[r2][c2]-a[r1][c2]*a[r2][c1])/determinant/bound;}
    for(int j=0;j<3;++j)for(int k=0;k<3;++k)inverse[3][j]-=double(from.m[3][k])*inverse[k][j];
    double delta[4][4]{};for(int i=0;i<4;++i)for(int j=0;j<4;++j)for(int k=0;k<4;++k)delta[i][j]+=inverse[i][k]*double(to.m[k][j]);
    if(operation==0)for(int i=0;i<3;++i)for(int j=0;j<3;++j)if(std::abs(delta[i][j]-(i==j?1.0:0.0))>1e-6)return reject("Translation group delta changed rotation/scale");
    bool exactNoop=true;for(int i=0;i<4;++i)for(int j=0;j<4;++j)exactNoop=exactNoop && from.m[i][j]==to.m[i][j];
    if(exactNoop){error.clear();return true;}
    const auto before=world.Capture();auto candidate=world;
    for(const auto id:roots) {
        Matrix4x4 current{},desired{},parent=GroupIdentity();const auto* original=world.Find(id);
        if(!world.GetWorldMatrix(id,current) || (original->parent && !world.GetWorldMatrix(original->parent,parent)))return reject("Group hierarchy unavailable");
        for(int i=0;i<4;++i)for(int j=0;j<4;++j){double value=0;for(int k=0;k<4;++k)value+=double(current.m[i][k])*delta[k][j];if(!std::isfinite(value) || std::abs(value)>(std::numeric_limits<float>::max)())return reject("Group target exceeds float range");desired.m[i][j]=float(value);}
        Scene::TransformComponent converted;if(!GizmoWorldToLocal(desired,parent,original->transform,converted,error))return false;
        if(operation==0){const auto position=converted.position;converted=original->transform;converted.position=position;}
        candidate.Find(id)->transform=converted;Matrix4x4 rebuilt{};
        if(!candidate.GetWorldMatrix(id,rebuilt))return reject("Group candidate hierarchy unavailable");
        for(int i=0;i<4;++i)for(int j=0;j<4;++j)if(!std::isfinite(rebuilt.m[i][j]) || std::abs(double(rebuilt.m[i][j])-desired.m[i][j])>1e-4*(std::max)(1.0,std::abs(double(desired.m[i][j]))))return reject("Group target is not stably representable as local TRS");
    }
    if(!history.Record(before,candidate,selection.Capture(),selection.Capture(),error))return false;
    world=std::move(candidate);error.clear();return true;
}
}
