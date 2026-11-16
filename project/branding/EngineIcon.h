#pragma once

#include <Windows.h>
#include "EngineIconResource.h"

namespace Engine::Branding {

inline HICON LoadEngineIcon(HINSTANCE instance)
{
    return LoadIconW(instance, MAKEINTRESOURCEW(IDI_KUBO_ENGINE));
}

} // namespace Engine::Branding
