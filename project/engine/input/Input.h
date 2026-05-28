#pragma once
#include <Windows.h>

#include <wrl.h>
#define DIRECTINPUT_VERSION 0x0800 // DirectInputのバージョン指定
#include <dinput.h>
#include <Vector2.h>
#include <array>
template <class T>using ComPtr = Microsoft::WRL::ComPtr<T>;

#include <Xinput.h>
#pragma comment(lib, "Xinput.lib")

namespace Engine::Base {
class WinApp;
}

namespace Engine::InputSystem {

/// @brief キーボード、マウス、ゲームパッド入力を統合管理するクラス
/// @details DirectInput と XInput を使って各デバイスの入力状態を更新し、
///          押下・トリガー・座標・振動制御を提供する。
class Input
{
	Input() = default;
	~Input() = default;
	Input(Input&) = default;
	Input& operator=(Input&) = delete;

public: // インナークラス
	struct MouseMove {
		LONG lX;
		LONG lY;
		LONG lZ;
	};
public:
	/// @brief シングルトンインスタンスを取得する
	/// @param なし
	/// @return Input のインスタンス
	static Input* GetInstance();
	/// @brief 入力管理インスタンスを解放する
	/// @param なし
	/// @return なし
	void Finalize();



	/// @brief 入力デバイスを初期化する
	/// @param winApp ウィンドウ情報を持つアプリケーション管理クラス
	/// @return なし
	void Initialize(Engine::Base::WinApp* winApp);
	/// @brief 全入力デバイスの状態を更新する
	/// @param なし
	/// @return なし
	void Update();

	/// @brief キーが押下中かを判定する
	/// @param keyNumber 判定したいキーコード
	/// @return 押下中なら true
	bool PushKey(BYTE keyNumber);//押してるとき
	/// @brief キーが押された瞬間かを判定する
	/// @param keyNumber 判定したいキーコード
	/// @return 押されたフレームなら true
	bool TriggerKey(BYTE keyNumber);//押したとき


	/// @brief マウスボタンが押下中かを判定する
	/// @param buttonNumber 判定したいボタン番号
	/// @return 押下中なら true
	bool PushMouse(int buttonNumber);
	/// @brief マウスボタンが押された瞬間かを判定する
	/// @param buttonNumber 判定したいボタン番号
	/// @return 押されたフレームなら true
	bool TriggerMouse(int buttonNumber);
	//マウスの座標
	const Vector2& GetMousePos()const { return mousePos; };
	//マウスの移動量
	MouseMove GetMouseMove()const {
		MouseMove move;
		move.lX = mouse.lX;
		move.lY = mouse.lY;
		move.lZ = mouse.lZ;
		return move;
	};

	/// @brief ゲームパッドボタンが押下中かを判定する
	/// @param button 判定したいボタンフラグ
	/// @return 押下中なら true
	bool PushGamePadButton(WORD button);
	/// @brief ゲームパッドボタンが押された瞬間かを判定する
	/// @param button 判定したいボタンフラグ
	/// @return 押されたフレームなら true
	bool TriggerGamePadButton(WORD button);
	/// @brief ゲームパッドの X 軸スティック値を取得する
	/// @param right true なら右スティック、false なら左スティック
	/// @return 正規化したスティック値
	float GetGamePadStickX(bool right=false);
	/// @brief ゲームパッドの Y 軸スティック値を取得する
	/// @param right true なら右スティック、false なら左スティック
	/// @return 正規化したスティック値
	float GetGamePadStickY(bool right = false);
	/// @brief ゲームパッドのトリガー値を取得する
	/// @param right true なら右トリガー、false なら左トリガー
	/// @return トリガーの入力値
	BYTE GetGamePadTrigger(bool right = false);

	/// @brief ゲームパッドの振動を設定する
	/// @param leftMotor 左モーターの強さ
	/// @param rightMotor 右モーターの強さ
	/// @return なし
	void SetVibration(float leftMotor, float rightMotor);



private:
	ComPtr<IDirectInput8>directInput = nullptr;
	BYTE key[256] = {};
	BYTE preKey[256] = {};
	ComPtr<IDirectInputDevice8>keyboard;
	Engine::Base::WinApp* winApp_ = nullptr;

	Microsoft::WRL::ComPtr<IDirectInputDevice8> devMouse_;
	DIMOUSESTATE2 mouse;
	DIMOUSESTATE2 preMouse;
	Vector2 mousePos;

	//ゲームパッド
	XINPUT_STATE state_; // 現在のゲームパッド状態
	XINPUT_STATE prevState_; // 前回のゲームパッド状態
	bool gamepadConnected_ = false;


};

}

