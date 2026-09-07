#include "ModelAssetResolver.h"

#include <filesystem>

namespace Engine::Graphics3D {

std::string ResolveModelAssetPath(const std::string& directoryPath, const std::string& filename)
{
	const std::filesystem::path resourceRoot = directoryPath;
	const std::filesystem::path resourceDirectPath = resourceRoot / filename;
	if (std::filesystem::exists(resourceDirectPath)) {
		return resourceDirectPath.generic_string();
	}

	const std::filesystem::path modelRoot = std::filesystem::path(directoryPath) / "models";
	const std::filesystem::path directPath = modelRoot / filename;
	if (std::filesystem::exists(directPath)) {
		return directPath.generic_string();
	}

	const std::filesystem::path assetName = filename;
	const std::filesystem::path groupedPath = modelRoot / assetName.stem() / assetName.filename();
	if (std::filesystem::exists(groupedPath)) {
		return groupedPath.generic_string();
	}

	if (std::filesystem::is_directory(modelRoot)) {
		for (const auto& entry : std::filesystem::recursive_directory_iterator(modelRoot)) {
			if (entry.is_regular_file() && entry.path().filename() == assetName.filename()) {
				return entry.path().generic_string();
			}
		}
	}

	return directPath.generic_string();
}

std::string ResolveModelResourcePath(const std::string& directoryPath, const std::string& filename)
{
	const std::filesystem::path resourceRoot = directoryPath;
	const std::filesystem::path directPath = resourceRoot / filename;
	if (std::filesystem::exists(directPath)) {
		return directPath.generic_string();
	}

	const std::filesystem::path targetFilename = std::filesystem::path(filename).filename();
	for (const std::filesystem::directory_entry& entry :
		std::filesystem::recursive_directory_iterator(resourceRoot)) {
		if (entry.is_regular_file() && entry.path().filename() == targetFilename) {
			return entry.path().generic_string();
		}
	}

	return directPath.generic_string();
}

}
