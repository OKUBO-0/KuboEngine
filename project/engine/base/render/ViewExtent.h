#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include "Vector2.h"

namespace Engine::Base {
struct ViewExtent {
    uint32_t width = 1280, height = 720;
    float Aspect() const { return float(width) / float(height); }
    bool operator==(const ViewExtent&) const = default;
};
inline bool ValidGameOutputExtent(ViewExtent extent) {
    return extent.width>0 && extent.height>0 && extent.width<=4096 && extent.height<=4096 &&
        uint64_t(extent.width)*extent.height<=8388608;
}
struct ViewRect {uint32_t left=0,top=0,width=0,height=0;bool operator==(const ViewRect&) const = default;};
inline ViewRect GamePresentationRect(ViewExtent window,ViewExtent game) {
    if(!window.width || !window.height || !game.width || !game.height) return {};
    uint32_t width=window.width,height=window.height;
    if(uint64_t(window.width)*game.height>uint64_t(window.height)*game.width)
        width=static_cast<uint32_t>((std::max)(uint64_t(1),uint64_t(window.height)*game.width/game.height));
    else height=static_cast<uint32_t>((std::max)(uint64_t(1),uint64_t(window.width)*game.height/game.width));
    return {(window.width-width)/2,(window.height-height)/2,width,height};
}
inline std::optional<Vector2> GameOutputPoint(Vector2 point,Vector2 origin,Vector2 size,ViewExtent output) {
    if(!(size.x>0 && size.y>0))return std::nullopt;
    const float x=(point.x-origin.x)/size.x,y=(point.y-origin.y)/size.y;
    if(!(x>=0 && x<1 && y>=0 && y<1))return std::nullopt;
    return Vector2{x*output.width,y*output.height};
}
// Minimized/invalid panels do not replace the last usable extent. Bound allocation
// cost uniformly, retaining aspect except for unavoidable integer rounding.
inline bool PanelViewExtent(float width, float height, ViewExtent& output) {
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1) return false;
    const double scale = (std::min)(1.0, 2048.0 / (std::max)(double(width), double(height)));
    output = {uint32_t((std::max)(1.0, std::floor(double(width) * scale))),
              uint32_t((std::max)(1.0, std::floor(double(height) * scale)))};
    return true;
}
}
