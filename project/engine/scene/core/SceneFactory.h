#pragma once

#include "AbstractSceneFactory.h"
#include <memory>

namespace DirectXGame {
class DirectXGameSessionContext;
}

/// @brief 文字列から具体的なシーンを生成するファクトリ
/// @details SceneManager から呼ばれ、シーン名とクラス実装を対応付ける。
namespace Engine::Scene {

class SceneFactory
	: public AbstractSceneFactory
{

public:
	/// @brief シーン名に応じたシーンを生成する
	/// @param sceneName 生成対象のシーン名
	/// @return 生成したシーン
	std::unique_ptr<BaseScene> CreateScene(const std::string&sceneName)override;

private:
	std::shared_ptr<DirectXGame::DirectXGameSessionContext> directXGameSessionContext_;

};

}

