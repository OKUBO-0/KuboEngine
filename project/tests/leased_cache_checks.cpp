#include "../engine/assets/LeasedCache.h"
#include <map>
#include <unordered_set>
#include <memory>
#include <iostream>
#include <stdexcept>
int main() {
    const auto require = [](bool value) { if (!value) throw std::runtime_error("Leased cache invariant failed"); };
    try {
        std::map<std::string, std::shared_ptr<int>> entries;
        std::unordered_set<std::string> eligible;
        entries["pinned"] = std::make_shared<int>(1);
        entries["live"] = std::make_shared<int>(2); eligible.insert("live");
        auto lease = entries.at("live"); std::weak_ptr<int> weak = lease;
        entries["old"] = std::make_shared<int>(3); eligible.insert("old");
        int waits = 0, releases = 0;
        const auto release = [&](int*) { require(waits > 0); ++releases; };
        require(Engine::Assets::CollectUnusedLeasedEntries(entries, eligible, [&] { ++waits; }, release) == 1);
        require(entries.contains("pinned") && entries.contains("live") && !entries.contains("old") && !weak.expired());
        require(Engine::Assets::CollectUnusedLeasedEntries(entries, eligible, [&] { ++waits; }, release) == 0 && waits == 1);
        lease.reset();
        // New acquisition during the wait must be observed by the recheck.
        require(Engine::Assets::CollectUnusedLeasedEntries(entries, eligible, [&] { ++waits; lease = entries.at("live"); }, release) == 0);
        lease.reset();
        bool failed = false;
        try { Engine::Assets::CollectUnusedLeasedEntries(entries, eligible, [] { throw std::runtime_error("wait failed"); }, release); }
        catch (...) { failed = true; }
        require(failed && entries.contains("live") && !weak.expired());
        require(Engine::Assets::CollectUnusedLeasedEntries(entries, eligible, [&] { ++waits; }, release) == 1 && weak.expired());
        require(releases == 2 && entries.size() == 1);
        std::cout << "PASS: live leases/pins, wait-before-release, post-wait acquisition recheck and failed-wait isolation\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
