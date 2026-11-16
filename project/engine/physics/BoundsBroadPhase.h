#pragma once

#include "../math/types/Vector3.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Engine::Physics {

struct BroadPhaseBounds { Vector3 min{}, max{}; };

// Step-local spatial BVH. Topology is balanced once; changed leaves refit their
// ancestors without losing contacts created by a preceding solver correction.
// Query results retain Entity storage order, not spatial traversal order.
class BoundsBroadPhase final {
public:
    struct Proxy { size_t index; BroadPhaseBounds bounds; bool enabled = true; };
    void Build(const std::vector<Proxy>& proxies, size_t entityCount) {
        branches_.clear(); leaves_.assign(entityCount, Missing); root_ = Missing;
        if (proxies.empty()) return;
        auto ordered = proxies;
        for (auto& proxy : ordered) if (!Valid(proxy.bounds)) { proxy.bounds = {}; proxy.enabled = false; }
        branches_.reserve(ordered.size() * 2 - 1);
        root_ = BuildRange(ordered, 0, ordered.size(), Missing);
    }
    void Update(size_t index, const BroadPhaseBounds& bounds, bool enabled) {
        if (index >= leaves_.size() || leaves_[index] == Missing) return;
        auto cursor = leaves_[index];
        branches_[cursor].bounds = bounds;
        branches_[cursor].enabled = enabled && Valid(bounds);
        while (branches_[cursor].parent != Missing) {
            cursor = branches_[cursor].parent; Refit(cursor);
        }
    }
    std::vector<size_t> Query(const BroadPhaseBounds& bounds, size_t firstIndex,
        size_t* visited = nullptr) const {
        if (visited) *visited = 0;
        std::vector<size_t> result;
        if (root_ == Missing || !Valid(bounds)) return result;
        QueryBranch(root_, bounds, firstIndex, result, visited);
        std::sort(result.begin(), result.end());
        return result;
    }
    static bool Overlap(const BroadPhaseBounds& a, const BroadPhaseBounds& b) {
        // Inclusive pruning: exact touch is retained for narrow-phase rejection.
        return a.min.x <= b.max.x && b.min.x <= a.max.x &&
            a.min.y <= b.max.y && b.min.y <= a.max.y &&
            a.min.z <= b.max.z && b.min.z <= a.max.z;
    }
private:
    static constexpr size_t Missing = size_t(-1);
    struct Branch {
        BroadPhaseBounds bounds{};
        size_t left = Missing, right = Missing, parent = Missing;
        size_t index = Missing, maxIndex = 0;
        bool enabled = false;
    };
    std::vector<Branch> branches_;
    std::vector<size_t> leaves_;
    size_t root_ = Missing;
    static bool Valid(const BroadPhaseBounds& b) {
        return std::isfinite(b.min.x) && std::isfinite(b.min.y) && std::isfinite(b.min.z) &&
            std::isfinite(b.max.x) && std::isfinite(b.max.y) && std::isfinite(b.max.z) &&
            b.min.x <= b.max.x && b.min.y <= b.max.y && b.min.z <= b.max.z;
    }
    static double Center(const BroadPhaseBounds& b, int axis) {
        if (axis == 0) return double(b.min.x) + b.max.x;
        if (axis == 1) return double(b.min.y) + b.max.y;
        return double(b.min.z) + b.max.z;
    }
    void Refit(size_t index) {
        auto& branch = branches_[index];
        const auto& a = branches_[branch.left]; const auto& b = branches_[branch.right];
        branch.enabled = a.enabled || b.enabled;
        branch.maxIndex = std::max(a.maxIndex, b.maxIndex);
        if (!a.enabled) { branch.bounds = b.bounds; return; }
        if (!b.enabled) { branch.bounds = a.bounds; return; }
        branch.bounds = {{std::min(a.bounds.min.x,b.bounds.min.x),std::min(a.bounds.min.y,b.bounds.min.y),std::min(a.bounds.min.z,b.bounds.min.z)},
            {std::max(a.bounds.max.x,b.bounds.max.x),std::max(a.bounds.max.y,b.bounds.max.y),std::max(a.bounds.max.z,b.bounds.max.z)}};
    }
    size_t BuildRange(std::vector<Proxy>& proxies, size_t begin, size_t end, size_t parent) {
        const auto index = branches_.size(); branches_.push_back({}); branches_[index].parent = parent;
        if (end - begin == 1) {
            const auto& proxy = proxies[begin]; auto& leaf = branches_[index];
            leaf.index = leaf.maxIndex = proxy.index; leaf.bounds = proxy.bounds;
            leaf.enabled = proxy.enabled && Valid(proxy.bounds);
            if (proxy.index < leaves_.size()) leaves_[proxy.index] = index;
            return index;
        }
        double low[3] = {}, high[3] = {};
        for (int axis = 0; axis < 3; ++axis) {
            low[axis] = high[axis] = Center(proxies[begin].bounds, axis);
            for (size_t i = begin + 1; i < end; ++i) {
                const auto center = Center(proxies[i].bounds, axis);
                low[axis] = std::min(low[axis], center); high[axis] = std::max(high[axis], center);
            }
        }
        int axis = 0; for (int i = 1; i < 3; ++i) if (high[i]-low[i] > high[axis]-low[axis]) axis = i;
        const auto middle = begin + (end - begin) / 2;
        std::nth_element(proxies.begin()+begin, proxies.begin()+middle, proxies.begin()+end,
            [axis](const Proxy& a,const Proxy& b) {
                const auto x = Center(a.bounds,axis), y = Center(b.bounds,axis);
                return x < y || (x == y && a.index < b.index);
            });
        const auto left = BuildRange(proxies,begin,middle,index);
        const auto right = BuildRange(proxies,middle,end,index);
        branches_[index].left = left; branches_[index].right = right; Refit(index);
        return index;
    }
    void QueryBranch(size_t index, const BroadPhaseBounds& bounds, size_t firstIndex,
        std::vector<size_t>& result, size_t* visited) const {
        if (visited) ++*visited;
        const auto& branch = branches_[index];
        if (!branch.enabled || branch.maxIndex < firstIndex || !Overlap(bounds,branch.bounds)) return;
        if (branch.index != Missing) { result.push_back(branch.index); return; }
        QueryBranch(branch.left,bounds,firstIndex,result,visited);
        QueryBranch(branch.right,bounds,firstIndex,result,visited);
    }
};

} // namespace Engine::Physics
