#pragma once
#include "ProjectContext.h"
#include <stdexcept>
namespace Engine::Base {
// Explicit selection never falls back to directory discovery. Legacy embedders
// without a selected file retain the existing unique-directory discovery policy.
inline bool OpenProjectManifestAtRoot(const std::filesystem::path& root,const std::filesystem::path& selected,
    ProjectContext& output,std::string& error) {
    try {
        const auto owner=std::filesystem::absolute(root).lexically_normal();
        const auto file=selected.empty()?ProjectContext::FindManifest(owner):
            (selected.is_absolute()?selected:owner/selected).lexically_normal();
        if(file.empty() || file.native().find(L'\0')!=std::wstring::npos ||
            _wcsicmp(file.extension().c_str(),L".kubo") || !std::filesystem::is_regular_file(file))
            throw std::runtime_error("Select an existing project .kubo file");
        const auto canonicalOwner=std::filesystem::canonical(owner);
        if(_wcsicmp(std::filesystem::canonical(file.parent_path()).c_str(),canonicalOwner.c_str()) ||
            _wcsicmp(std::filesystem::canonical(file).parent_path().c_str(),canonicalOwner.c_str()))
            throw std::runtime_error("Selected project file is outside the owning root");
        ProjectContext candidate;if(!candidate.Open(file,error))return false;
        output=std::move(candidate);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
