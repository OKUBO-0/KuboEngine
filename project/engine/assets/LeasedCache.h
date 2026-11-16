#pragma once
#include <vector>
#include <string>
#include <cstddef>
namespace Engine::Assets {
// Owner-thread only. Wait once before deletion, then recheck leases because the
// wait callback may allow new owners to acquire an entry.
template<class Map, class Set, class Wait, class Release>
size_t CollectUnusedLeasedEntries(Map& entries, Set& eligible, Wait wait, Release release) {
    std::vector<std::string> candidates;
    for (const auto& key : eligible) {
        const auto found = entries.find(key);
        if (found != entries.end() && found->second.use_count() == 1) candidates.push_back(key);
    }
    if (candidates.empty()) return 0;
    wait();
    size_t removed = 0;
    for (const auto& key : candidates) {
        const auto found = entries.find(key);
        if (found == entries.end() || !eligible.contains(key) || found->second.use_count() != 1) continue;
        release(found->second.get());
        entries.erase(found); eligible.erase(key); ++removed;
    }
    return removed;
}
}
