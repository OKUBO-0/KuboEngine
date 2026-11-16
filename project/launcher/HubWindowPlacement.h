#pragma once

#include "engine/base/AtomicFile.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace KuboHub {

struct WindowPlacement {
    RECT normal{};
    UINT dpi = 96;
    bool maximized = false;

    bool Valid() const noexcept
    {
        return dpi >= 48 && dpi <= 768 &&
            normal.left >= -100000 && normal.top >= -100000 &&
            normal.right <= 100000 && normal.bottom <= 100000 &&
            normal.right > normal.left && normal.bottom > normal.top &&
            normal.right - normal.left <= 32768 && normal.bottom - normal.top <= 32768;
    }
};

inline WindowPlacement ReadWindowPlacement(const std::filesystem::path& file)
{
    std::ifstream input(file, std::ios::binary);
    char bytes[256]{};
    input.read(bytes, sizeof(bytes));
    if (!input.eof() || input.bad()) return {};

    std::istringstream fields(std::string(bytes, static_cast<size_t>(input.gcount())));
    WindowPlacement placement;
    std::string format, extra;
    unsigned maximized = 0;
    if (!(fields >> format >> placement.normal.left >> placement.normal.top
        >> placement.normal.right >> placement.normal.bottom >> placement.dpi >> maximized) ||
        format != "kubo-hub-window-1" || maximized > 1 || (fields >> extra) ||
        !placement.Valid()) return {};
    placement.maximized = maximized == 1;
    return placement;
}

inline bool SaveWindowPlacement(const std::filesystem::path& file, const WindowPlacement& placement)
{
    if (!placement.Valid()) return false;
    const RECT& rect = placement.normal;
    std::ostringstream fields;
    fields << "kubo-hub-window-1 " << rect.left << ' ' << rect.top << ' '
        << rect.right << ' ' << rect.bottom << ' ' << placement.dpi << ' '
        << (placement.maximized ? 1 : 0) << '\n';
    std::string error;
    return Engine::Base::AtomicFile::Write(file, fields.str(), 255, error);
}

inline RECT FitWindowToWorkArea(const WindowPlacement& placement, const RECT& work, UINT dpi)
{
    const bool restore = placement.Valid();
    const auto scale = [&](int size, UINT sourceDpi) {
        return MulDiv(size, static_cast<int>(dpi), static_cast<int>(sourceDpi));
    };
    const LONG availableWidth = (std::max)(1L, work.right - work.left);
    const LONG availableHeight = (std::max)(1L, work.bottom - work.top);
    const LONG width = std::clamp<LONG>(
        restore ? scale(placement.normal.right - placement.normal.left, placement.dpi) : scale(1180, 96),
        (std::min)(availableWidth, static_cast<LONG>(scale(560, 96))), availableWidth);
    const LONG height = std::clamp<LONG>(
        restore ? scale(placement.normal.bottom - placement.normal.top, placement.dpi) : scale(800, 96),
        (std::min)(availableHeight, static_cast<LONG>(scale(360, 96))), availableHeight);
    const LONG x = std::clamp<LONG>(
        restore ? placement.normal.left : work.left + (availableWidth - width) / 2,
        work.left, work.left + availableWidth - width);
    const LONG y = std::clamp<LONG>(
        restore ? placement.normal.top : work.top + (availableHeight - height) / 2,
        work.top, work.top + availableHeight - height);
    return {x, y, x + width, y + height};
}

} // namespace KuboHub
