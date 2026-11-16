#include "../engine/assets/AssetRevision.h"
#include <iostream>
int main() {
    using namespace Engine::Assets;
    const std::string a(64, 'a'), b(64, 'b'), c(64, 'c');
    const auto require = [](bool value) { if (!value) throw std::runtime_error("Asset revision invariant failed"); };
    try {
        require(RevisionIdentity("legacy", {}) == "legacy");
        require(RevisionIdentity("guid", a) == RevisionIdentity("guid", a));
        require(RevisionIdentity("guid", a) != RevisionIdentity("guid", b));
        require(RevisionIdentity("guid", a) != RevisionIdentity("other", a));
        require(RevisionIdentity("a:b", c) != RevisionIdentity("a", c));
        bool rejected = false;
        try { RevisionIdentity("guid", "invalid"); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected);
        ModelRevisionState state;
        require(state.NeedsReload(a, false)); state.Commit(a);
        require(!state.NeedsReload(a, true) && state.NeedsReload(b, true));
        state.Reject(b);
        require(state.current == a && !state.NeedsReload(b, true) && state.NeedsReload(c, true));
        state.Commit(c); require(!state.failedAttempt && !state.NeedsReload(c, true));
        require(state.NeedsReload(c, false));
        ModelRevisionState initial;
        initial.Reject({}); require(!initial.NeedsReload({}, false));
        require(initial.NeedsReload(a, false));
        std::cout << "PASS: revision cache identities, legacy compatibility, transactional reload state and failed-revision suppression\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
