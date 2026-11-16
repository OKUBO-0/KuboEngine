#pragma once
#include <memory>
#include <string>

/// @brief 現在シーンと次シーンを管理するシングルトンクラス
/// @details シーンの生成委譲、切り替え、更新、描画の入口を提供する。
namespace Engine::Scene {

class AbstractSceneFactory;
class BaseScene;

class SceneManager
{
public:

	static SceneManager* GetInstance();
	/// @brief 次フレームで切り替えるシーンを設定する
	/// @param nextScene 切り替え先シーン
	/// @return なし
	void SetNextScene(std::unique_ptr<BaseScene> nextScene);
	/// @brief 現在シーンを更新する
	/// @param なし
	/// @return なし
	void Update();
	void Update(float deltaTime);
	float GetFrameDeltaTime() const { return frameDeltaTime_; }
	BaseScene* GetCurrentScene() const { return currentScene.get(); }
	float SimulationDeltaTime(float realDeltaTime) const;
	/// @brief 現在シーンを描画する
	/// @param なし
	/// @return なし
	void Draw();
	/// @brief 管理中シーンの終了処理を行う
	/// @details callback前に所有Sceneを退役し、callback例外も元のまま通知する。
	///          再帰終了はno-op、終了中のScene/factory変更は拒否する。同じhost threadで使用する。
	/// @param なし
	/// @return なし
	void Finalize();

	/// @brief シーン生成に使うファクトリを設定する
	/// @param sceneFactory シーンファクトリ
	/// @return なし
	void SetSceneFactory(AbstractSceneFactory* sceneFactory);

	/// @brief シーン名を指定して遷移を要求する
	/// @param sceneName 遷移先シーン名
	/// @return なし
	void ChangeScene(const std::string &sceneName);
	

private:

	SceneManager();
	~SceneManager();
	SceneManager(SceneManager&) = delete;
	SceneManager& operator=(SceneManager&) = delete;

	

private:
	std::unique_ptr<BaseScene> currentScene = nullptr;
	std::unique_ptr<BaseScene> nextScene = nullptr;
	AbstractSceneFactory* sceneFactory = nullptr;
	float frameDeltaTime_ = 1.0f / 60.0f;
	bool finalizing_ = false;

};

}

