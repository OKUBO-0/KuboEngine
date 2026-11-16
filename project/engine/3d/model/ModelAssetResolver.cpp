#include "ModelAssetResolver.h"

#include <filesystem>
#include "../../base/AssetPath.h"
#include "../../base/EngineResourcePath.h"

namespace Engine::Graphics3D {
namespace {
std::filesystem::path ModelResourceRoot(const std::string& directoryPath)
{
	const auto root=Base::PathFromUtf8(directoryPath);
	// Legacy sample names only: explicit custom roots and GUID sources stay fail-closed.
	if(root==L"Resources" && !std::filesystem::is_directory(root)) return Base::ResolveEngineResource(root);
	return root;
}
}

std::string ResolveModelAssetPath(const std::string& directoryPath, const std::string& filename)
{
	const auto resourceRoot = ModelResourceRoot(directoryPath);
	const auto resourceDirectPath = resourceRoot / Base::PathFromUtf8(filename);
	if (std::filesystem::exists(resourceDirectPath)) {
		return Base::PathToUtf8(resourceDirectPath);
	}

	const auto modelRoot = resourceRoot / "models";
	const auto directPath = modelRoot / Base::PathFromUtf8(filename);
	if (std::filesystem::exists(directPath)) {
		return Base::PathToUtf8(directPath);
	}

	const auto assetName = Base::PathFromUtf8(filename);
	const std::filesystem::path groupedPath = modelRoot / assetName.stem() / assetName.filename();
	if (std::filesystem::exists(groupedPath)) {
		return Base::PathToUtf8(groupedPath);
	}

	if (std::filesystem::is_directory(modelRoot)) {
		for (const auto& entry : std::filesystem::recursive_directory_iterator(modelRoot)) {
			if (entry.is_regular_file() && entry.path().filename() == assetName.filename()) {
				return Base::PathToUtf8(entry.path());
			}
		}
	}

	return Base::PathToUtf8(directPath);
}

std::string ResolveModelResourcePath(const std::string& directoryPath, const std::string& filename)
{
	const auto resourceRoot = ModelResourceRoot(directoryPath);
	const auto directPath = resourceRoot / Base::PathFromUtf8(filename);
	if (std::filesystem::exists(directPath)) {
		return Base::PathToUtf8(directPath);
	}

	const auto targetFilename = Base::PathFromUtf8(filename).filename();
	if(std::filesystem::is_directory(resourceRoot)) for (const std::filesystem::directory_entry& entry :
		std::filesystem::recursive_directory_iterator(resourceRoot)) {
		if (entry.is_regular_file() && entry.path().filename() == targetFilename) {
			return Base::PathToUtf8(entry.path());
		}
	}

	return Base::PathToUtf8(directPath);
}

}
