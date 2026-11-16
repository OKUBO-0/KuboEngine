#pragma once

#include "AbstractSceneFactory.h"
#include <memory>
#include <filesystem>
#include "engine/base/ProjectPaths.h"

/// @brief 文字列から具体的なシーンを生成するファクトリ
/// @details SceneManager から呼ばれ、シーン名とクラス実装を対応付ける。
namespace Engine::Scene {

class SceneFactory
	: public AbstractSceneFactory
{

public:
	explicit SceneFactory(const std::filesystem::path& projectRoot = {})
		: gameplayScenePath_(Engine::Base::ProjectPaths(projectRoot).LegacyScene()) {}
	void SetGameplayScenePath(std::filesystem::path path) { gameplayScenePath_ = std::filesystem::absolute(path).lexically_normal(); }
	/// @brief シーン名に応じたシーンを生成する
	/// @param sceneName 生成対象のシーン名
	/// @return 生成したシーン
	std::unique_ptr<BaseScene> CreateScene(const std::string&sceneName)override;

private:
	std::filesystem::path gameplayScenePath_;

};

}

