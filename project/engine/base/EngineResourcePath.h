#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <filesystem>
#include <stdexcept>

namespace Engine::Base {
inline std::filesystem::path ResolveEngineResource(const std::filesystem::path& relative)
{
    if(relative.empty() || relative.native().find(L'\0')!=std::wstring::npos)
        throw std::invalid_argument("Engine resource path must be non-empty without embedded NUL");
    const auto requested=relative.lexically_normal();
    // Explicit absolute files are allowed, but relative engine files belong to the installation.
    if(requested.is_absolute()) return requested;
    if(requested.has_root_path()) throw std::invalid_argument("Engine resource path must not be drive/root-relative");
    for(const auto& component:requested) if(component==L"..")
        throw std::invalid_argument("Engine resource path escapes the installation");
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    if (length == 0 || length == 32768) throw std::runtime_error("Cannot resolve engine installation directory");
    return (std::filesystem::path(executable).parent_path() / requested).lexically_normal();
}
}
