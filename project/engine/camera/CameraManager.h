#pragma once
#include <Camera.h>
#include <unordered_map>
#include <string>
#include <memory>

/// @brief 複数カメラの登録とアクティブ切り替えを管理するクラス
/// @details シーンから追加されたカメラを名前で保持し、現在使用するカメラを返す。
namespace Engine::CameraSystem {

class CameraManager
{
	CameraManager() = default;
	~CameraManager() = default;
	CameraManager(CameraManager&) = default;
	CameraManager& operator=(CameraManager&) = delete;

public:
	/// @brief シングルトンインスタンスを取得する
	/// @param なし
	/// @return CameraManager インスタンス
	static CameraManager* GetInstance();
	/// @brief 管理中のカメラを破棄する
	/// @param なし
	/// @return なし
	void Finalize();

	/// @brief デフォルトカメラを初期化する
	/// @param なし
	/// @return なし
	void Initialize();



	/// @brief カメラを名前付きで登録する
	/// @param name 登録名
	/// @param camera 登録するカメラ
	/// @return なし
	void AddCamera(const std::string& name, const Camera* camera);

	/// @brief 登録済みカメラを削除する
	/// @param name 削除対象名
	/// @return なし
	void RemoveCamera(const std::string& name);

	/// @brief 名前からカメラを取得する
	/// @param name 取得対象名
	/// @return 見つかったカメラ。未登録なら nullptr
	Camera* GetCamera(const std::string& name);

	/// @brief 現在のアクティブカメラを取得する
	/// @param なし
	/// @return アクティブカメラ
	Camera* GetActiveCamera();


	/// @brief アクティブカメラを切り替える
	/// @param name 切り替え先名
	/// @return なし
	void SetActiveCamera(const std::string& name);



private:
	//カメラデータ
	std::unordered_map<std::string, Camera> cameras;

	// アクティブカメラ名
	std::string activeCameraName;

	//デフォルトカメラ
	
	//デフォルトカメラ
	std::unique_ptr<Camera> defaultCamera = nullptr;


};

}

