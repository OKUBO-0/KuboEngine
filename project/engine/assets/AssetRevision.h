#pragma once
#include <string>
#include <algorithm>
#include <stdexcept>
namespace Engine::Assets {
inline std::string RevisionIdentity(const std::string& identity, const std::string& revision) {
    if (revision.empty()) return identity;
    if (revision.size() != 64 || !std::all_of(revision.begin(), revision.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) throw std::invalid_argument("Invalid asset revision digest");
    return "kubo-revision:" + std::to_string(identity.size()) + ":" + identity + ":" + revision;
}
struct ModelRevisionState {
    std::string current, failed;
    bool failedAttempt = false;
    bool NeedsReload(const std::string& requested, bool hasObject) const {
        return (!hasObject || current != requested) && (!failedAttempt || failed != requested);
    }
    void Commit(const std::string& requested) { current = requested; failed.clear(); failedAttempt = false; }
    void Reject(const std::string& requested) { failed = requested; failedAttempt = true; }
};
}
