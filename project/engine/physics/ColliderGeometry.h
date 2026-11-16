#pragma once

#include "BoundsBroadPhase.h"
#include "PhysicsComponents.h"
#include "../math/types/Matrix4x4.h"
#include <array>
#include <limits>

namespace Engine::Physics::Geometry {

struct Point {
    std::array<double,3> values{};
    double& operator[](size_t axis) {return values[axis];}
    double operator[](size_t axis) const {return values[axis];}
};
inline Point ToPoint(Vector3 v) {return {{v.x,v.y,v.z}};}
inline Vector3 ToVector(Point v) {return {float(v[0]),float(v[1]),float(v[2])};}
inline Point operator+(Point a,Point b) {return {{a[0]+b[0],a[1]+b[1],a[2]+b[2]}};}
inline Point operator-(Point a,Point b) {return {{a[0]-b[0],a[1]-b[1],a[2]-b[2]}};}
inline Point operator*(Point a,double b) {return {{a[0]*b,a[1]*b,a[2]*b}};}
inline double Dot(Point a,Point b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline Point Cross(Point a,Point b) {
    return {{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}};
}

struct Rounded {
    Point halfAxis;
    double radius=0; // Zero identifies a box.
};

inline Rounded TransformRounded(const ColliderComponent& collider,const Matrix4x4& matrix)
{
    if (collider.shape==ColliderShape::Box) return {};
    double scale=0;
    for (size_t i=0;i<3;++i)
        scale=std::max(scale,std::hypot(double(matrix.m[i][0]),double(matrix.m[i][1]),double(matrix.m[i][2])));
    Rounded shape;
    shape.radius=scale*collider.radius;
    if (collider.shape==ColliderShape::Capsule) {
        for (size_t i=0;i<3;++i) shape.halfAxis[i]=double(matrix.m[1][i])*collider.halfSegment;
    }
    return shape;
}

struct ClosestPair {Point first,second;};
inline ClosestPair ClosestSegments(Point a,Point u,Point b,Point v)
{
    const auto offset=a-b;
    const double uu=Dot(u,u),vv=Dot(v,v),uv=Dot(u,v),ur=Dot(u,offset),vr=Dot(v,offset);
    double s=0,t=0;
    if (uu>0 && vv>0) {
        const double determinant=uu*vv-uv*uv;
        if (determinant>1e-14*uu*vv) s=std::clamp((uv*vr-vv*ur)/determinant,0.0,1.0);
        t=std::clamp((uv*s+vr)/vv,0.0,1.0);
        s=std::clamp((uv*t-ur)/uu,0.0,1.0);
    } else if (uu>0) s=std::clamp(-ur/uu,0.0,1.0);
    else if (vv>0) t=std::clamp(vr/vv,0.0,1.0);
    return {a+u*s,b+v*t};
}

// Squared distance to the box is quadratic between its six slab crossings.
inline ClosestPair ClosestBox(Point origin,Point delta,const BroadPhaseBounds& bounds)
{
    const Point low=ToPoint(bounds.min),high=ToPoint(bounds.max);
    std::array<double,8> cuts{0,1};
    size_t count=2;
    for (size_t axis=0;axis<3;++axis) {
        if (delta[axis]==0) continue;
        for (const double plane : {low[axis],high[axis]}) {
            const double t=(plane-origin[axis])/delta[axis];
            if (t>0 && t<1) cuts[count++]=t;
        }
    }
    std::sort(cuts.begin(),cuts.begin()+count);
    ClosestPair best{};
    double bestDistance=std::numeric_limits<double>::infinity();
    const auto check = [&](double t) {
        const Point from=origin+delta*t;
        Point to;
        for (size_t axis=0;axis<3;++axis) to[axis]=std::clamp(from[axis],low[axis],high[axis]);
        const double distance=Dot(to-from,to-from);
        if (distance<bestDistance) {bestDistance=distance;best={from,to};}
    };
    for (size_t i=1;i<count;++i) {
        const auto middle=origin+delta*((cuts[i-1]+cuts[i])*0.5);
        double curvature=0,slope=0;
        for (size_t axis=0;axis<3;++axis) {
            const auto edge=std::clamp(middle[axis],low[axis],high[axis]);
            if (middle[axis]==edge) continue;
            curvature+=delta[axis]*delta[axis];
            slope+=delta[axis]*(origin[axis]-edge);
        }
        check(curvature ? std::clamp(-slope/curvature,cuts[i-1],cuts[i]) : cuts[i-1]);
        if (bestDistance==0) break;
    }
    return best;
}

inline bool RoundedContact(const Rounded& first,Point a,const BroadPhaseBounds& aBounds,
    const Rounded& second,Point b,const BroadPhaseBounds& bBounds,Vector3& normal,float& penetration)
{
    const bool reverse=!first.radius;
    const auto& round=reverse?second:first;
    const auto center=reverse?b:a;
    const auto& box=reverse?aBounds:bBounds;
    const auto& roundBounds=reverse?bBounds:aBounds;
    const bool both=first.radius && second.radius;
    const auto pair=both ? ClosestSegments(a-first.halfAxis,first.halfAxis*2,b-second.halfAxis,second.halfAxis*2)
        : ClosestBox(center-round.halfAxis,round.halfAxis*2,box);
    Point axis=pair.second-pair.first;
    const double distance=std::sqrt(Dot(axis,axis));
    double depth=first.radius+second.radius-distance;
    if (!(depth>0)) return false;
    if (distance>0) axis=axis*(1/distance);
    else if (both) {
        axis=Cross(first.halfAxis,second.halfAxis);
        if (Dot(axis,axis)==0) {
            const auto line=Dot(first.halfAxis,first.halfAxis)>0 ? first.halfAxis : second.halfAxis;
            Point basis{{1,0,0}};
            if (std::abs(line[0])>std::abs(line[1])) basis={{0,1,0}};
            axis=Cross(line,basis);
            if (Dot(axis,axis)==0) axis={{1,0,0}};
        }
        axis=axis*(1/std::sqrt(Dot(axis,axis)));
    } else {
        // The centerline intersects the box. Choose the shortest separating
        // translation along the box axes, including full containment.
        depth=std::numeric_limits<double>::infinity();
        for (size_t i=0;i<3;++i) {
            const double negative=ToPoint(roundBounds.max)[i]-ToPoint(box.min)[i];
            const double positive=ToPoint(box.max)[i]-ToPoint(roundBounds.min)[i];
            if (std::min(negative,positive)<depth) {
                depth=std::min(negative,positive);
                axis={};
                axis[i]=negative<=positive ? 1.0 : -1.0;
            }
        }
    }
    if (!(depth>0) || depth>std::numeric_limits<float>::max()) return false;
    normal=ToVector(axis*(reverse?-1:1));
    penetration=static_cast<float>(depth);
    return true;
}

inline bool RayRounded(const Rounded& shape,Point center,Point start,Point unit,double maximum,
    double& distance,Vector3& normal)
{
    const auto a=center-shape.halfAxis, axis=shape.halfAxis*2;
    const double lengthSquared=Dot(axis,axis);
    const auto radial=[&](Point point) {
        return point-a-axis*(lengthSquared ? std::clamp(Dot(point-a,axis)/lengthSquared,0.0,1.0) : 0);
    };
    const double radiusSquared=shape.radius*shape.radius;
    const auto initial=radial(start);
    if (Dot(initial,initial)<=radiusSquared) {distance=0;normal={};return true;}
    double best=maximum;
    bool found=false;
    const auto roots=[&](Point offset,Point direction,const auto& accepts) {
        const double quadratic=Dot(direction,direction);
        const double linear=Dot(offset,direction);
        const double constant=Dot(offset,offset)-radiusSquared;
        const double discriminant=linear*linear-quadratic*constant;
        if (quadratic==0 || discriminant<0) return;
        for (double t : {(-linear-std::sqrt(discriminant))/quadratic,(-linear+std::sqrt(discriminant))/quadratic}) {
            if (t>=0 && t<=best && accepts(t)) {best=t;found=true;}
        }
    };
    const auto any=[](double){return true;};
    roots(start-a,unit,any);
    if (lengthSquared) {
        roots(start-(a+axis),unit,any);
        const double at=Dot(start-a,axis)/lengthSquared, slope=Dot(unit,axis)/lengthSquared;
        roots(start-a-axis*at,unit-axis*slope,[&](double t){return at+slope*t>=0 && at+slope*t<=1;});
    }
    if (!found) return false;
    distance=best;
    normal=ToVector(radial(start+unit*best)*(1/shape.radius));
    return true;
}

} // namespace Engine::Physics::Geometry
