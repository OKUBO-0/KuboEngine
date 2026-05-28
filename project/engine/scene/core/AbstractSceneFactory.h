#pragma once

#include <memory>
#include <string>

/// @brief シーン生成の抽象インターフェース
/// @details シーン名から具体的なシーンクラスを生成する責務を持つ。
namespace Engine::Scene {

class BaseScene;

class AbstractSceneFactory
{

public:
	/// @brief シーン名に対応するシーンを生成する
	/// @param sceneName 生成対象のシーン名
	/// @return 生成したシーン
	virtual std::unique_ptr<BaseScene> CreateScene(const std::string& sceneName) = 0;
	virtual ~AbstractSceneFactory() = default;

};

}

