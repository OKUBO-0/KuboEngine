#pragma once
#include <compare>

namespace Engine::Base {
enum class TextureColorSpace { Automatic, Srgb, Linear };
enum class TextureMipMode { Automatic, Preserve, Generate };
struct TextureLoadOptions {
	TextureColorSpace colorSpace = TextureColorSpace::Automatic;
	TextureMipMode mipMode = TextureMipMode::Automatic;
	auto operator<=>(const TextureLoadOptions&) const = default;
};
}
