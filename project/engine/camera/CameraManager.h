#pragma once
#include <Camera.h>
#include <unordered_map>
#include <string>
#include <string_view>
#include <memory>

/// @brief 複数カメラの登録とアクティブ切り替えを管理するクラス
/// @details シーンから追加されたカメラを名前で保持し、現在使用するカメラを返す。
namespace Engine::CameraSystem {

class CameraManager
{
	CameraManager() = default;
	~CameraManager() = default;
	CameraManager(CameraManager&) = delete;
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
	/// @param camera 登録するカメラ。所有権は呼び出し側が保持し、破棄前にRemoveCameraする
	/// @return なし
	void AddCamera(const std::string& name, Camera* camera);

	/// @brief 登録済みカメラを削除する
	/// @param name 削除対象名
	/// @return なし
	void RemoveCamera(const std::string& name);

	/// @brief 名前からカメラを取得する
	/// @param name 取得対象名
	/// @return 見つかったカメラ。未登録なら nullptr
	Camera* GetCamera(const std::string& name);

	// Same-host-thread borrowed lookup/detach. Linear lookup avoids owning
	// temporary names. Detach preserves a replacement registered by another owner.
	Camera* FindCamera(std::string_view name) noexcept;
	bool DetachCamera(std::string_view name, const Camera* expected,
	    std::string_view preferredActive = {}) noexcept;

	/// @brief 現在のアクティブカメラを取得する
	/// @param なし
	/// @return アクティブカメラ
	Camera* GetActiveCamera();

	/// @brief 現在のアクティブカメラ名を取得する
	/// @return アクティブカメラ名。未設定なら空文字
	// Borrowed reference: copy before a registry mutation if retention is needed.
	const std::string& GetActiveCameraName() const { return activeCameraName ? *activeCameraName : emptyCameraName; }


	/// @brief アクティブカメラを切り替える
	/// @param name 切り替え先名
	/// @return なし
	void SetActiveCamera(const std::string& name);



private:
	//カメラデータ
	std::unordered_map<std::string, Camera*> cameras;

	// アクティブカメラ名
	// unordered_map key references survive rehash. Clear/repoint BEFORE erase.
	const std::string* activeCameraName = nullptr;
	const std::string emptyCameraName;
	using CameraIterator = decltype(cameras)::iterator;
	CameraIterator FindRegistration(std::string_view name) noexcept;

	//デフォルトカメラ
	
	//デフォルトカメラ
	std::unique_ptr<Camera> defaultCamera = nullptr;


};

}

