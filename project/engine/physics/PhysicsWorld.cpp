#include "PhysicsWorld.h"
#include "BoundsBroadPhase.h"
#include "ColliderGeometry.h"
#include "../scene/entity/EntityTransformMatrix.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <new>

namespace Engine::Physics {
namespace {

using Bounds = BroadPhaseBounds;

bool IsFinite(const Vector3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

double InverseMass(const Scene::EntityRecord& entity)
{
    if (!entity.hasRigidBody || !entity.rigidBody.dynamic ||
        !std::isfinite(entity.rigidBody.mass) || entity.rigidBody.mass <= 0.0f) return 0.0;
    return 1.0 / static_cast<double>(entity.rigidBody.mass);
}

bool FloatVector(double x,double y,double z,Vector3& result)
{
    const double limit=std::numeric_limits<float>::max();
    if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || std::abs(x)>limit || std::abs(y)>limit || std::abs(z)>limit) return false;
    result={static_cast<float>(x),static_cast<float>(y),static_cast<float>(z)};return true;
}

bool ColliderHalf(const Matrix4x4& matrix,Vector3 half,Vector3& result)
{
    if(!IsFinite(half) || half.x<0 || half.y<0 || half.z<0)return false;
    return FloatVector(std::abs(double(matrix.m[0][0]))*half.x+std::abs(double(matrix.m[1][0]))*half.y+std::abs(double(matrix.m[2][0]))*half.z,
        std::abs(double(matrix.m[0][1]))*half.x+std::abs(double(matrix.m[1][1]))*half.y+std::abs(double(matrix.m[2][1]))*half.z,
        std::abs(double(matrix.m[0][2]))*half.x+std::abs(double(matrix.m[1][2]))*half.y+std::abs(double(matrix.m[2][2]))*half.z,result);
}

bool BuildCollider(const ColliderComponent& component,const Matrix4x4& matrix,
    Vector3& half,Geometry::Rounded& round)
{
    if (!component.HasValidShape()) return false;
    if (component.shape==ColliderShape::Box) return ColliderHalf(matrix,component.halfExtents,half);
    round=Geometry::TransformRounded(component,matrix);
    return FloatVector(round.radius+std::abs(round.halfAxis[0]),round.radius+std::abs(round.halfAxis[1]),
        round.radius+std::abs(round.halfAxis[2]),half);
}

bool InverseLinear(const Matrix4x4& matrix,double inverse[3][3])
{
    for(const auto& row:matrix.m) for(float value:row) if(!std::isfinite(value)) return false;
    if(std::abs(matrix.m[0][3])>1e-6 || std::abs(matrix.m[1][3])>1e-6 ||
        std::abs(matrix.m[2][3])>1e-6 || std::abs(matrix.m[3][3]-1)>1e-6) return false;
    double bound=0;for(int i=0;i<3;++i) for(int j=0;j<3;++j) bound=std::max(bound,std::abs(double(matrix.m[i][j])));
    if(bound==0) return false;
    double a[3][3];for(int i=0;i<3;++i) for(int j=0;j<3;++j) a[i][j]=matrix.m[i][j]/bound;
    const double determinant=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-
        a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
    if(!std::isfinite(determinant) || std::abs(determinant)<1e-10) return false;
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) {
        const int r1=(j+1)%3,r2=(j+2)%3,c1=(i+1)%3,c2=(i+2)%3;
        inverse[i][j]=(a[r1][c1]*a[r2][c2]-a[r1][c2]*a[r2][c1])/determinant/bound;
        if(!std::isfinite(inverse[i][j])) return false;
    }
    return true;
}

struct Node {
    Matrix4x4 world{};
    double inverse[3][3]{};
    Vector3 start{},position{},velocity{},half{};
    Geometry::Rounded round{};
    size_t parent=0,owner=0;
    double inverseMass=0;
    bool valid=false,collider=false,grounded=false;
};

bool Center(const std::vector<Node>& nodes,size_t index,Vector3& center)
{
    const auto& node=nodes[index];
    if(node.owner>=nodes.size()) {center=node.start;return true;}
    const auto& owner=nodes[node.owner];
    return FloatVector(double(node.start.x)+owner.position.x-owner.start.x,
        double(node.start.y)+owner.position.y-owner.start.y,double(node.start.z)+owner.position.z-owner.start.z,center);
}

bool MakeBounds(const std::vector<Node>& nodes,size_t index,Bounds& bounds,Vector3& center)
{
    const auto& node=nodes[index];
    if(!node.valid || !node.collider || !Center(nodes,index,center)) return false;
    return FloatVector(double(center.x)-node.half.x,double(center.y)-node.half.y,double(center.z)-node.half.z,bounds.min) &&
        FloatVector(double(center.x)+node.half.x,double(center.y)+node.half.y,double(center.z)+node.half.z,bounds.max);
}

bool BuildFrame(const Scene::EntityWorld& world,std::vector<Node>& nodes,std::string& diagnostic)
{
    const auto& entities=world.GetEntities();const auto count=entities.size();nodes.resize(count);
    std::unordered_map<Scene::EntityId,size_t> indices;
    for(size_t i=0;i<count;++i) if(!entities[i].id || !indices.emplace(entities[i].id,i).second) {
        diagnostic="Physics rejected duplicate/zero Entity IDs";return false;
    }
    std::vector<bool> relevant(count,false);
    for(size_t i=0;i<count;++i) if(entities[i].hasRigidBody || entities[i].hasCollider) {
        std::vector<size_t> chain;auto cursor=i;bool active=true;
        while(cursor<count && chain.size()<=count) {
            chain.push_back(cursor);if(!entities[cursor].active) {active=false;break;}
            if(!entities[cursor].parent) break;
            const auto parent=indices.find(entities[cursor].parent);if(parent==indices.end()) break;cursor=parent->second;
        }
        if(active) for(const auto index:chain) relevant[index]=true;
    }
    for(size_t i=0;i<count;++i) {
        auto& node=nodes[i];const auto& entity=entities[i];node.parent=node.owner=count;
        if(!relevant[i]) continue;
        const auto parent=indices.find(entity.parent);if(entity.parent && parent!=indices.end()) node.parent=parent->second;
        auto cursor=i;size_t visited=0;bool active=true,validHierarchy=true;
        while(cursor<count) {
            if(++visited>count) {validHierarchy=false;break;}
            if(!entities[cursor].active) active=false;
            if(node.owner==count && entities[cursor].hasRigidBody) node.owner=cursor;
            if(!entities[cursor].parent) break;
            const auto ancestor=indices.find(entities[cursor].parent);
            if(ancestor==indices.end()) {validHierarchy=false;break;}
            cursor=ancestor->second;
        }
        if(!validHierarchy) {if(diagnostic.empty()) diagnostic="Physics skipped invalid hierarchy for Entity "+std::to_string(entity.id);continue;}
        if(!active) continue;
        if(!world.GetWorldMatrix(entity.id,node.world) || !InverseLinear(node.world,node.inverse)) {
            if(diagnostic.empty()) diagnostic="Physics skipped singular/nonfinite World transform for Entity "+std::to_string(entity.id);continue;
        }
        node.start=node.position={node.world.m[3][0],node.world.m[3][1],node.world.m[3][2]};
        node.valid=true;node.velocity=entity.rigidBody.velocity;node.inverseMass=InverseMass(entity);
        if(entity.hasCollider) {
            node.collider=BuildCollider(entity.collider,node.world,node.half,node.round);
        }
    }
    // A shape belongs to the nearest explicit RigidBody, including static ones.
    // Nested bodies own independent world poses; body-less child shapes form a
    // translation-only compound. Invalid owner/parent frames never participate.
    for(size_t pass=0;pass<count;++pass) {
        bool changed=false;
        for(auto& node:nodes) if(node.valid && ((node.owner<count && !nodes[node.owner].valid) ||
            (node.parent<count && !nodes[node.parent].valid))) {node.valid=false;changed=true;}
        if(!changed) break;
    }
    return true;
}

bool ApplyVelocityChange(Vector3& velocity, const Vector3& direction, double change)
{
    const double x=static_cast<double>(velocity.x)+direction.x*change;
    const double y=static_cast<double>(velocity.y)+direction.y*change;
    const double z=static_cast<double>(velocity.z)+direction.z*change;
    const double limit=std::numeric_limits<float>::max();
    if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        std::abs(x)>limit || std::abs(y)>limit || std::abs(z)>limit) return false;
    velocity={static_cast<float>(x),static_cast<float>(y),static_cast<float>(z)};return true;
}

double MaterialCoefficient(float coefficient)
{
    // Public mutable records can bypass authored validation. Do not let a bad
    // direct edit poison velocity; absent/nonfinite coefficients act as zero.
    return std::isfinite(coefficient) ? std::clamp(static_cast<double>(coefficient),0.0,1.0) : 0.0;
}

struct QueryAbort {PhysicsQueryResult result;};
void QueryWork(PhysicsQueryBudget& budget,size_t count=1)
{
    if(budget.exhausted || budget.work>PhysicsQueryBudget::MaxWork || count>PhysicsQueryBudget::MaxWork-budget.work){budget.exhausted=true;throw QueryAbort{PhysicsQueryResult::Limit};}
    budget.work+=count;
}
template<class Visitor>
void QueryColliders(const Scene::EntityWorld& world,PhysicsQueryFilter filter,std::span<const PhysicsQueryOverride> overrides,PhysicsQueryBudget& budget,Visitor visit)
{
    const auto& entities=world.GetEntities();const auto count=entities.size();
    if(budget.exhausted || budget.queries>=PhysicsQueryBudget::MaxQueries || count>PhysicsQueryBudget::MaxEntities || overrides.size()>PhysicsQueryBudget::MaxOverrides){budget.exhausted=true;throw QueryAbort{PhysicsQueryResult::Limit};}
    ++budget.queries;QueryWork(budget,count*3+overrides.size());
    std::unordered_map<Scene::EntityId,size_t> indices;
    for(size_t i=0;i<count;++i)if(!entities[i].id || entities[i].id>Scene::EntityWorld::MaximumEntityId || !indices.emplace(entities[i].id,i).second)throw QueryAbort{PhysicsQueryResult::Invalid};
    std::vector<const PhysicsQueryOverride*> replacements(count,nullptr);
    for(const auto& value:overrides){const auto found=indices.find(value.id);if(found==indices.end() || replacements[found->second])throw QueryAbort{PhysicsQueryResult::Invalid};replacements[found->second]=&value;}
    std::vector<Matrix4x4> locals(count);std::vector<bool> valid(count),active(count);
    for(size_t i=0;i<count;++i){
        const auto& transform=replacements[i]?replacements[i]->transform:entities[i].transform;
        valid[i]=IsFinite(transform.position) && IsFinite(transform.rotation) && IsFinite(transform.scale);
        active[i]=replacements[i]?replacements[i]->active:entities[i].active;
        if(valid[i])locals[i]=Scene::EntityLocalMatrix(transform);
    }
    std::vector<bool> relevant(count,false),framesValid(count,false);std::vector<Matrix4x4> matrices(count);
    for(size_t i=0;i<count;++i)if(entities[i].hasCollider && entities[i].id!=filter.ignore && (filter.includeTriggers || !entities[i].collider.isTrigger)){
        auto cursor=i;
        while(!relevant[cursor]){
            QueryWork(budget);relevant[cursor]=true;if(!entities[cursor].parent)break;
            const auto parent=indices.find(entities[cursor].parent);if(parent==indices.end())break;cursor=parent->second;
        }
    }
    // Exact left-to-right composition, not child*(cached parent World): float
    // multiplication is not associative. Only relevant shape ancestry is built.
    for(size_t i=0;i<count;++i)if(relevant[i]){
        auto matrix=locals[i];auto cursor=i;size_t depth=0;bool usable=true;
        for(;;){
            QueryWork(budget);
            if(!valid[cursor] || !active[cursor] || ++depth>count){usable=false;break;}
            if(!entities[cursor].parent)break;
            const auto parent=indices.find(entities[cursor].parent);if(parent==indices.end()){usable=false;break;}
            cursor=parent->second;if(!valid[cursor]){usable=false;break;}matrix=matrix*locals[cursor];
        }
        double inverse[3][3];framesValid[i]=usable && InverseLinear(matrix,inverse);matrices[i]=matrix;
    }
    for(size_t i=0;i<count;++i){
        const auto& entity=entities[i];if(!entity.hasCollider || entity.id==filter.ignore || (!filter.includeTriggers && entity.collider.isTrigger) || !framesValid[i])continue;
        bool usable=true;auto cursor=i;
        for(;;){QueryWork(budget);if(!framesValid[cursor]){usable=false;break;}if(!entities[cursor].parent)break;const auto found=indices.find(entities[cursor].parent);if(found==indices.end()){usable=false;break;}cursor=found->second;}
        if(!usable)continue;
        const auto& matrix=matrices[i];Vector3 half{};Bounds bounds{};
        Geometry::Rounded round;
        if(!BuildCollider(entity.collider,matrix,half,round))continue;
        const Vector3 center{matrix.m[3][0],matrix.m[3][1],matrix.m[3][2]};
        if(!FloatVector(double(center.x)-half.x,double(center.y)-half.y,double(center.z)-half.z,bounds.min) ||
            !FloatVector(double(center.x)+half.x,double(center.y)+half.y,double(center.z)+half.z,bounds.max))continue;
        QueryWork(budget);visit(entity,bounds,round,center);
    }
}

} // namespace

PhysicsQueryResult PhysicsWorld::Raycast(const Scene::EntityWorld& world,Vector3 origin,Vector3 direction,float maximumDistance,
    PhysicsRayHit& output,PhysicsQueryFilter filter,std::span<const PhysicsQueryOverride> overrides,PhysicsQueryBudget* shared) noexcept
{
    if(!IsFinite(origin) || !IsFinite(direction) || !std::isfinite(maximumDistance) || maximumDistance<0 || filter.ignore>Scene::EntityWorld::MaximumEntityId)return PhysicsQueryResult::Invalid;
    const double length=std::hypot(double(direction.x),double(direction.y),double(direction.z));if(length==0)return PhysicsQueryResult::Invalid;
    const double start[]{origin.x,origin.y,origin.z},unit[]{direction.x/length,direction.y/length,direction.z/length};
    PhysicsQueryBudget local;auto& budget=shared?*shared:local;
    try{
        PhysicsRayHit hit;double closest=maximumDistance;
        QueryColliders(world,filter,overrides,budget,[&](const Scene::EntityRecord& entity,const Bounds& bounds,
            const Geometry::Rounded& round,Vector3 center){
            const double low[]{bounds.min.x,bounds.min.y,bounds.min.z},high[]{bounds.max.x,bounds.max.y,bounds.max.z};
            double near=0,far=maximumDistance;Vector3 normal{};bool inside=true;
            for(size_t axis=0;axis<3;++axis)inside=inside && start[axis]>=low[axis] && start[axis]<=high[axis];
            for(size_t axis=0;axis<3;++axis){
                if(unit[axis]==0){if(start[axis]<low[axis] || start[axis]>high[axis])return;continue;}
                double first=(low[axis]-start[axis])/unit[axis],last=(high[axis]-start[axis])/unit[axis];
                const float sign=unit[axis]>0?-1.0f:1.0f;if(first>last)std::swap(first,last);
                if(first>near){near=first;normal={};if(axis==0)normal.x=sign;else if(axis==1)normal.y=sign;else normal.z=sign;}
                far=std::min(far,last);if(near>far)return;
            }
            if (round.radius) {
                QueryWork(budget,3);
                if (!Geometry::RayRounded(round,Geometry::ToPoint(center),{{start[0],start[1],start[2]}},
                    {{unit[0],unit[1],unit[2]}},closest,near,normal)) return;
                inside=near==0;
            }
            if(hit.entity && (near>closest || (near==closest && entity.id>hit.entity)))return;
            Vector3 point{};if(!FloatVector(start[0]+unit[0]*near,start[1]+unit[1]*near,start[2]+unit[2]*near,point))throw QueryAbort{PhysicsQueryResult::Invalid};
            closest=near;hit={entity.id,static_cast<float>(near),point,inside?Vector3{}:normal,entity.collider.isTrigger};
        });
        output=hit;return PhysicsQueryResult::Success;
    }catch(const QueryAbort& failure){return failure.result;}catch(const std::bad_alloc&){return PhysicsQueryResult::AllocationFailure;}catch(...){return PhysicsQueryResult::Invalid;}
}

PhysicsQueryResult PhysicsWorld::OverlapBox(const Scene::EntityWorld& world,Vector3 center,Vector3 half,std::vector<Scene::EntityId>& output,
    PhysicsQueryFilter filter,std::span<const PhysicsQueryOverride> overrides,PhysicsQueryBudget* shared) noexcept
{
    Bounds target{};
    if(!IsFinite(center) || !IsFinite(half) || half.x<0 || half.y<0 || half.z<0 || filter.ignore>Scene::EntityWorld::MaximumEntityId ||
        !FloatVector(double(center.x)-half.x,double(center.y)-half.y,double(center.z)-half.z,target.min) ||
        !FloatVector(double(center.x)+half.x,double(center.y)+half.y,double(center.z)+half.z,target.max))return PhysicsQueryResult::Invalid;
    PhysicsQueryBudget local;auto& budget=shared?*shared:local;
    try{
        std::vector<Scene::EntityId> hits;
        QueryColliders(world,filter,overrides,budget,[&](const Scene::EntityRecord& entity,const Bounds& bounds,
            const Geometry::Rounded& round,Vector3 shapeCenter){
            if(!BoundsBroadPhase::Overlap(target,bounds))return;
            if (round.radius) {
                QueryWork(budget,8);
                const auto pair=Geometry::ClosestBox(Geometry::ToPoint(shapeCenter)-round.halfAxis,
                    round.halfAxis*2,target);
                const auto separation=pair.second-pair.first;
                if (Geometry::Dot(separation,separation)>round.radius*round.radius) return;
            }
            if(hits.size()==PhysicsQueryBudget::MaxHits){budget.exhausted=true;throw QueryAbort{PhysicsQueryResult::Limit};}hits.push_back(entity.id);
        });
        std::sort(hits.begin(),hits.end());output.swap(hits);return PhysicsQueryResult::Success;
    }catch(const QueryAbort& failure){return failure.result;}catch(const std::bad_alloc&){return PhysicsQueryResult::AllocationFailure;}catch(...){return PhysicsQueryResult::Invalid;}
}

void PhysicsWorld::Step(Scene::EntityWorld& world, float deltaSeconds)
{
    StepInternal(world,deltaSeconds,nullptr);
}

bool PhysicsWorld::StepInternal(Scene::EntityWorld& world,float deltaSeconds,std::vector<TriggerPair>* triggers)
{
    events_.clear();
    diagnostic_.clear();
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
        return false;
    }
    deltaSeconds = std::min(deltaSeconds, 0.1f);

    auto& entities = world.GetEntities();
    std::vector<Node> nodes;
    if(!BuildFrame(world,nodes,diagnostic_)) return false;
    for(size_t i=0;i<nodes.size();++i) {
        auto& node=nodes[i];const auto& entity=entities[i];
        if(!node.valid || !entity.hasRigidBody || node.inverseMass==0) continue;
        if(entity.rigidBody.useGravity && !FloatVector(double(node.velocity.x)+gravity_.x*double(deltaSeconds),
            double(node.velocity.y)+gravity_.y*double(deltaSeconds),double(node.velocity.z)+gravity_.z*double(deltaSeconds),node.velocity)) node.velocity={};
        if(!IsFinite(node.velocity)) node.velocity={};
        if(!FloatVector(double(node.position.x)+node.velocity.x*double(deltaSeconds),double(node.position.y)+node.velocity.y*double(deltaSeconds),
            double(node.position.z)+node.velocity.z*double(deltaSeconds),node.position)) {
            diagnostic_="Physics integration exceeds finite World coordinates";events_.clear();return false;
        }
    }
    BoundsBroadPhase broadPhase;
    std::vector<BoundsBroadPhase::Proxy> proxies;
    std::vector<std::vector<size_t>> ownedShapes(nodes.size());
    for(size_t i=0;i<nodes.size();++i) if(nodes[i].valid && nodes[i].collider) {
        Bounds bounds{};Vector3 center{};
        const bool enabled=MakeBounds(nodes,i,bounds,center);
        proxies.push_back({i,enabled ? bounds : Bounds{},enabled});
        if(nodes[i].owner<nodes.size()) ownedShapes[nodes[i].owner].push_back(i);
    }
    broadPhase.Build(proxies,nodes.size());
    const auto refitOwner=[&](size_t owner) {
        if(owner>=nodes.size()) return;
        for(const auto shape:ownedShapes[owner]) {
            Bounds bounds{};Vector3 center{};const bool enabled=MakeBounds(nodes,shape,bounds,center);
            broadPhase.Update(shape,bounds,enabled);
        }
    };
    for (size_t i = 0; i < entities.size(); ++i) {
        auto& first = entities[i];
        if(!nodes[i].valid || !nodes[i].collider) continue;
        const auto queryAfter=[&](size_t firstIndex) {
            Bounds bounds{};Vector3 center{};
            return MakeBounds(nodes,i,bounds,center) ? broadPhase.Query(bounds,firstIndex) : std::vector<size_t>{};
        };
        auto candidates=queryAfter(i+1);size_t candidateCursor=0;
        while(candidateCursor<candidates.size()) {
            const auto j=candidates[candidateCursor++];
            auto& second = entities[j];
            if(!nodes[j].valid || !nodes[j].collider || (nodes[i].owner<nodes.size() && nodes[i].owner==nodes[j].owner)) continue;
            Bounds a{},b{};Vector3 firstCenter{},secondCenter{};
            if(!MakeBounds(nodes,i,a,firstCenter) || !MakeBounds(nodes,j,b,secondCenter)) continue;
            const float overlapX = std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x);
            const float overlapY = std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y);
            const float overlapZ = std::min(a.max.z, b.max.z) - std::max(a.min.z, b.min.z);
            if (!std::isfinite(overlapX) || !std::isfinite(overlapY) || !std::isfinite(overlapZ) ||
                overlapX <= 0.0f || overlapY <= 0.0f || overlapZ <= 0.0f) {
                continue;
            }

            Vector3 normal{0.0f, 1.0f, 0.0f};
            float penetration = overlapY;
            if (overlapX < penetration) { penetration = overlapX; normal = {1.0f, 0.0f, 0.0f}; }
            if (overlapZ < penetration) { penetration = overlapZ; normal = {0.0f, 0.0f, 1.0f}; }
            if ((normal.x != 0.0f && secondCenter.x < firstCenter.x) || (normal.y != 0.0f && secondCenter.y < firstCenter.y) ||
                (normal.z != 0.0f && secondCenter.z < firstCenter.z)) {
                normal *= -1.0f;
            }
            if ((nodes[i].round.radius || nodes[j].round.radius) &&
                !Geometry::RoundedContact(nodes[i].round,Geometry::ToPoint(firstCenter),a,
                    nodes[j].round,Geometry::ToPoint(secondCenter),b,normal,penetration)) continue;
            const bool trigger = first.collider.isTrigger || second.collider.isTrigger;
            events_.push_back({first.id, second.id, normal, penetration, trigger});
            if (trigger) {
                continue;
            }

            auto& firstBody=nodes[nodes[i].owner<nodes.size() ? nodes[i].owner : i];
            auto& secondBody=nodes[nodes[j].owner<nodes.size() ? nodes[j].owner : j];
            const double firstInverseMass = nodes[i].owner<nodes.size() ? firstBody.inverseMass : 0.0;
            const double secondInverseMass = nodes[j].owner<nodes.size() ? secondBody.inverseMass : 0.0;
            const double totalInverseMass = firstInverseMass + secondInverseMass;
            if (totalInverseMass == 0.0) {
                continue;
            }
            const double firstWeight = firstInverseMass / totalInverseMass;
            const double secondWeight = secondInverseMass / totalInverseMass;
            auto firstPosition=firstBody.position,secondPosition=secondBody.position;
            if((firstInverseMass>0 && !ApplyVelocityChange(firstPosition,normal,-penetration*firstWeight)) ||
                (secondInverseMass>0 && !ApplyVelocityChange(secondPosition,normal,penetration*secondWeight))) {
                if(diagnostic_.empty()) diagnostic_="Physics correction exceeds finite World coordinates";continue;
            }
            if(firstInverseMass>0) firstBody.position=firstPosition;
            if(secondInverseMass>0) secondBody.position=secondPosition;
            // A response can create a new later contact or move other compound
            // shapes. Refit both owners, then resume the original j ordering.
            refitOwner(nodes[i].owner);refitOwner(nodes[j].owner);
            candidates=queryAfter(j+1);candidateCursor=0;

            const Vector3 firstVelocity = firstInverseMass > 0.0 ? firstBody.velocity : Vector3{};
            const Vector3 secondVelocity = secondInverseMass > 0.0 ? secondBody.velocity : Vector3{};
            const double relativeNormalVelocity =
                (static_cast<double>(secondVelocity.x) - firstVelocity.x) * normal.x +
                (static_cast<double>(secondVelocity.y) - firstVelocity.y) * normal.y +
                (static_cast<double>(secondVelocity.z) - firstVelocity.z) * normal.z;
            const double restitution=relativeNormalVelocity < -RestitutionVelocityThreshold ?
                std::max(MaterialCoefficient(first.collider.restitution),MaterialCoefficient(second.collider.restitution)) : 0.0;
            // Mass-weighted normal impulse followed by a Coulomb-limited
            // tangential impulse. Never change separating/trigger contacts.
            if (relativeNormalVelocity < 0.0) {
                auto firstCandidate=firstVelocity,secondCandidate=secondVelocity;
                const double closingChange=-(1.0+restitution)*relativeNormalVelocity;
                if((firstInverseMass>0.0 && !ApplyVelocityChange(firstCandidate,normal,-closingChange*firstWeight)) ||
                    (secondInverseMass>0.0 && !ApplyVelocityChange(secondCandidate,normal,closingChange*secondWeight))) continue;
                const double tx=(static_cast<double>(secondVelocity.x)-firstVelocity.x)-relativeNormalVelocity*normal.x;
                const double ty=(static_cast<double>(secondVelocity.y)-firstVelocity.y)-relativeNormalVelocity*normal.y;
                const double tz=(static_cast<double>(secondVelocity.z)-firstVelocity.z)-relativeNormalVelocity*normal.z;
                const double tangentSpeed=std::hypot(tx,ty,tz);
                const double friction=std::sqrt(MaterialCoefficient(first.collider.friction)*MaterialCoefficient(second.collider.friction));
                if(tangentSpeed>0.0 && friction>0.0) {
                    const Vector3 tangent{static_cast<float>(tx/tangentSpeed),static_cast<float>(ty/tangentSpeed),static_cast<float>(tz/tangentSpeed)};
                    const double tangentChange=std::min(tangentSpeed,friction*closingChange);
                    if((firstInverseMass>0.0 && !ApplyVelocityChange(firstCandidate,tangent,tangentChange*firstWeight)) ||
                        (secondInverseMass>0.0 && !ApplyVelocityChange(secondCandidate,tangent,-tangentChange*secondWeight))) continue;
                }
                if(firstInverseMass>0.0) firstBody.velocity=firstCandidate;
                if(secondInverseMass>0.0) secondBody.velocity=secondCandidate;
            }
            if (relativeNormalVelocity <= 0.0 && restitution==0.0) {
                if (firstInverseMass > 0.0 && normal.y < -0.5f) firstBody.grounded = true;
                if (secondInverseMass > 0.0 && normal.y > 0.5f) secondBody.grounded = true;
            }
        }
    }
    // Calculate every local result before publishing any body. Parent linear
    // transforms do not change in this translation-only solver; parent centers
    // include their nearest body's final displacement, independent of order.
    std::vector<Vector3> localPositions(nodes.size());
    for(size_t i=0;i<nodes.size();++i) {
        const auto& node=nodes[i];if(!node.valid || !entities[i].hasRigidBody) continue;
        if(node.parent>=nodes.size()) {localPositions[i]=node.position;continue;}
        Vector3 parentCenter{};
        if(!Center(nodes,node.parent,parentCenter)) {diagnostic_="Physics parent center exceeds finite coordinates";events_.clear();return false;}
        if(node.position.x==node.start.x && node.position.y==node.start.y && node.position.z==node.start.z &&
            parentCenter.x==nodes[node.parent].start.x && parentCenter.y==nodes[node.parent].start.y && parentCenter.z==nodes[node.parent].start.z) {
            localPositions[i]=entities[i].transform.position;continue;
        }
        const double delta[3]={double(node.position.x)-parentCenter.x,double(node.position.y)-parentCenter.y,double(node.position.z)-parentCenter.z};
        const auto& inverse=nodes[node.parent].inverse;double local[3]{};
        for(int j=0;j<3;++j) for(int k=0;k<3;++k) local[j]+=delta[k]*inverse[k][j];
        if(!FloatVector(local[0],local[1],local[2],localPositions[i])) {
            diagnostic_="Physics cannot publish finite parent-local positions; frame retained";events_.clear();return false;
        }
    }
    for(size_t i=0;i<nodes.size();++i) if(entities[i].hasRigidBody) {
        entities[i].rigidBody.grounded=false;
        if(!nodes[i].valid) continue;
        entities[i].transform.position=localPositions[i];
        if(nodes[i].inverseMass>0) {entities[i].rigidBody.velocity=nodes[i].velocity;entities[i].rigidBody.grounded=nodes[i].grounded;}
    }
    if (!triggers) return true;
    // Resolve sensor overlap against the final poses from the solver.
    for (size_t i=0; i<nodes.size(); ++i) {
        if (!nodes[i].valid || !nodes[i].collider || !entities[i].collider.isTrigger) continue;
        Bounds a{};Vector3 center{};
        if (!MakeBounds(nodes,i,a,center)) continue;
        for (const auto j : broadPhase.Query(a,0)) {
            if (i==j || (entities[j].collider.isTrigger && j<i) ||
                (nodes[i].owner<nodes.size() && nodes[i].owner==nodes[j].owner)) continue;
            Bounds b{};Vector3 other{};
            if (!MakeBounds(nodes,j,b,other) ||
                std::min(a.max.x,b.max.x)<=std::max(a.min.x,b.min.x) ||
                std::min(a.max.y,b.max.y)<=std::max(a.min.y,b.min.y) ||
                std::min(a.max.z,b.max.z)<=std::max(a.min.z,b.min.z)) continue;
            if (nodes[i].round.radius || nodes[j].round.radius) {
                Vector3 normal{};
                float depth=0;
                if (!Geometry::RoundedContact(nodes[i].round,Geometry::ToPoint(center),a,
                    nodes[j].round,Geometry::ToPoint(other),b,normal,depth)) continue;
            }
            if (triggers->size() == MaximumFrameEvents) {
                diagnostic_ = "Physics trigger overlap limit exceeded";
                return false;
            }
            triggers->emplace_back(std::min(entities[i].id,entities[j].id),std::max(entities[i].id,entities[j].id));
        }
    }
    std::sort(triggers->begin(),triggers->end());
    return true;
}

void PhysicsWorld::Advance(Scene::EntityWorld& world,float frameSeconds,const std::function<void(float)>& beforeStep,
    const std::function<void(std::span<const TriggerEvent>)>& onTriggers)
{
    advanceUsage_={};events_.clear();
    if(!std::isfinite(frameSeconds) || frameSeconds<=0) {
        advanceUsage_.remainingSeconds=accumulator_;return;
    }
    // Accumulate only bounded time. Drop whole excess ticks, retain the fraction
    // so recovery never queues an unbounded catch-up across following frames.
    constexpr double maximumAcceptedSeconds=0.25;
    const double accepted=std::min(double(frameSeconds),maximumAcceptedSeconds);
    advanceUsage_.droppedSeconds=double(frameSeconds)-accepted;
    accumulator_+=accepted;
    constexpr double epsilon=1e-9;
    const auto available=static_cast<size_t>(std::floor((accumulator_+epsilon)/FixedStepSeconds));
    const auto count=std::min(available,MaximumFrameSteps);
    const auto dropped=available-count;
    advanceUsage_.droppedSeconds+=double(dropped)*FixedStepSeconds;
    accumulator_=std::max(0.0,accumulator_-double(available)*FixedStepSeconds);
    if(!count){advanceUsage_.remainingSeconds=accumulator_;return;}
    std::vector<CollisionEvent> collected;std::string firstDiagnostic;
    for(size_t index=0;index<count;++index) {
        if(beforeStep)beforeStep(static_cast<float>(FixedStepSeconds));
        std::vector<TriggerPair> triggers;
        const bool snapshotValid=StepInternal(world,static_cast<float>(FixedStepSeconds),onTriggers ? &triggers : nullptr);
        ++advanceUsage_.steps;advanceUsage_.consumedSeconds+=FixedStepSeconds;
        if(firstDiagnostic.empty() && !diagnostic_.empty())firstDiagnostic=diagnostic_;
        for(const auto& event:events_) {
            if(collected.size()<MaximumFrameEvents)collected.push_back(event);
            else ++advanceUsage_.discardedEvents;
        }
        if (onTriggers && snapshotValid) {
            std::vector<TriggerEvent> edges;
            for (const auto& [a,b] : activeTriggers_) {
                if (!std::binary_search(triggers.begin(),triggers.end(),TriggerPair{a,b})) edges.push_back({a,b,false});
            }
            for (const auto& [a,b] : triggers) {
                if (!std::binary_search(activeTriggers_.begin(),activeTriggers_.end(),TriggerPair{a,b})) edges.push_back({a,b,true});
            }
            activeTriggers_.swap(triggers);
            if (!edges.empty()) onTriggers(edges);
        }
    }
    events_.swap(collected);diagnostic_=std::move(firstDiagnostic);
    advanceUsage_.remainingSeconds=accumulator_;
}

} // namespace Engine::Physics
