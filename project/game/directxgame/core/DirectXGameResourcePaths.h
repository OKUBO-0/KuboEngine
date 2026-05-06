#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace DirectXGame::ResourcePaths {

inline constexpr char kResourceRoot[] = "Resources/DirectXGame";

inline std::string MakePath(std::string_view relativePath)
{
	if (relativePath.starts_with("Resources/") || relativePath.starts_with("Resources\\")) {
		return std::filesystem::path(relativePath).generic_string();
	}
	return (std::filesystem::path(kResourceRoot) / relativePath).generic_string();
}

inline std::string MakeAudioPath(std::string_view relativePath)
{
	return MakePath(std::filesystem::path("audio").generic_string() + "/" + std::string(relativePath));
}

inline std::string MakeDataPath(std::string_view relativePath)
{
	return MakePath(std::filesystem::path("data").generic_string() + "/" + std::string(relativePath));
}

inline std::string MakeTexturePath(std::string_view relativePath)
{
	return MakePath(std::string(relativePath));
}

inline std::string GetModelResourceRoot()
{
	return kResourceRoot;
}

}
