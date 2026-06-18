#include "Input.h"
#include "HResult.h"
#include "WinApp.h"
#include <cassert>
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")
#include <cmath>

namespace Engine::InputSystem {

namespace {
constexpr float kGamePadAxisMaxValue = 32767.0f;
constexpr float kGamePadDeadZone = 0.2f;
constexpr float kGamePadVibrationMaxValue = 65535.0f;
}

Input* Input::GetInstance()
{
	static Input instance;
	return &instance;
}

void Input::Finalize()
{
	keyboard.Reset();
	devMouse_.Reset();
	directInput.Reset();
	winApp_ = nullptr;

}

void Input::Initialize(Engine::Base::WinApp* winApp)
{
	winApp_ = winApp;
	HRESULT hr;
	// キーボードとマウスの両方で使う DirectInput の本体を生成する
	//DirectInputのインスタンスを生成
	hr = DirectInput8Create(winApp->GetHInstance(), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&directInput, nullptr);
	Engine::Base::ThrowIfFailed(hr, "DirectInput8Create");
	//キーボードデバイス生成

	hr = directInput->CreateDevice(GUID_SysKeyboard, &keyboard, NULL);
	Engine::Base::ThrowIfFailed(hr, "IDirectInput8::CreateDevice keyboard");
	//入力データ形式のセット
	hr = keyboard->SetDataFormat(&c_dfDIKeyboard);
	Engine::Base::ThrowIfFailed(hr, "IDirectInputDevice8::SetDataFormat keyboard");
	//排他制御レベルのセット
	hr = keyboard->SetCooperativeLevel(winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	Engine::Base::ThrowIfFailed(hr, "IDirectInputDevice8::SetCooperativeLevel keyboard");

	// マウスはクライアント座標へ変換して扱うためウィンドウハンドルに紐付ける
	//マウスデバイス生成
	hr = directInput->CreateDevice(GUID_SysMouse, &devMouse_, NULL);
	Engine::Base::ThrowIfFailed(hr, "IDirectInput8::CreateDevice mouse");
	//入力データ形式のセット
	hr = devMouse_->SetDataFormat(&c_dfDIMouse2);
	Engine::Base::ThrowIfFailed(hr, "IDirectInputDevice8::SetDataFormat mouse");
	//排他制御レベルのセット
	hr = devMouse_->SetCooperativeLevel(winApp_->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	Engine::Base::ThrowIfFailed(hr, "IDirectInputDevice8::SetCooperativeLevel mouse");


}

void Input::Update()
{
	//前回のキー入力を保存
	memcpy(preKey, key, sizeof(key));
	//キーボード情報の取得
	keyboard->Acquire();
	//全キーボード入力情報を取得
	keyboard->GetDeviceState(sizeof(key), key);


	//前回のマウス入力を保存
	memcpy(&preMouse, &mouse, sizeof(mouse));
	//マウス情報の取得
	devMouse_->Acquire();
	//全マウス入力情報を取得
	devMouse_->GetDeviceState(sizeof(mouse), &mouse);
	//マウス座標の取得
	POINT point;
	GetCursorPos(&point);
	ScreenToClient(winApp_->GetHwnd(), &point);
	mousePos.x = (float)point.x;
	mousePos.y = (float)point.y;

	// ゲームパッドは前フレームとの差分も使うため状態を退避してから更新する
	prevState_ = state_;

	// XInputの状態を取得
	ZeroMemory(&state_, sizeof(XINPUT_STATE));
	if (XInputGetState(0, &state_) == ERROR_SUCCESS) {
		gamepadConnected_ = true;
	} else {
		gamepadConnected_ = false;
	}
	
}

bool Input::PushKey(BYTE keyNumber)
{
	if (key[keyNumber]) {

		return true;
	}
	return false;

}

bool Input::TriggerKey(BYTE keyNumber)
{
	if (key[keyNumber] && !preKey[keyNumber]) {

		return true;
	}
	return false;


}



bool Input::PushMouse(int buttonNumber)
{
	if (mouse.rgbButtons[buttonNumber]) {
		return true;
	}
	return false;
}

bool Input::TriggerMouse(int buttonNumber)
{
	if (mouse.rgbButtons[buttonNumber] && !preMouse.rgbButtons[buttonNumber]) {
		return true;
	}
	return false;
}

bool Input::PushGamePadButton(WORD button)
{
	return (gamepadConnected_ && (state_.Gamepad.wButtons & button));
}

bool Input::TriggerGamePadButton(WORD button)
{
	return (gamepadConnected_ && (state_.Gamepad.wButtons & button) && !(prevState_.Gamepad.wButtons & button));
}

float Input::GetGamePadStickX(bool right)
{
	if (!gamepadConnected_) return 0.0f;

	SHORT rawX = right ? state_.Gamepad.sThumbRX : state_.Gamepad.sThumbLX;
	float normX = rawX / kGamePadAxisMaxValue; // -1.0 ~ 1.0 に正規化

	// 微小入力で不要に反応しないよう中央付近は切り捨てる
	// デッドゾーン処理
	if (std::fabs(normX) < kGamePadDeadZone) return 0.0f;

	return normX;
}

float Input::GetGamePadStickY(bool right)
{
	if (!gamepadConnected_) return 0.0f;

	SHORT rawY = right ? state_.Gamepad.sThumbRY : state_.Gamepad.sThumbLY;
	float normY = rawY / kGamePadAxisMaxValue; // -1.0 ~ 1.0 に正規化

	// 微小入力で不要に反応しないよう中央付近は切り捨てる
	// デッドゾーン処理
	if (std::fabs(normY) < kGamePadDeadZone) return 0.0f;

	return normY; // Y軸は通常、上がマイナスなので反転
}

BYTE Input::GetGamePadTrigger(bool right)
{
	return gamepadConnected_ ? (right ? state_.Gamepad.bRightTrigger : state_.Gamepad.bLeftTrigger) : 0;
}

void Input::SetVibration(float leftMotor, float rightMotor)
{

	if (!gamepadConnected_) return;

	// 0.0-1.0 の強度を XInput の 16bit 値へ変換して適用する
	XINPUT_VIBRATION vibration;
	ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
	vibration.wLeftMotorSpeed = static_cast<WORD>(leftMotor * kGamePadVibrationMaxValue);
	vibration.wRightMotorSpeed = static_cast<WORD>(rightMotor * kGamePadVibrationMaxValue);
	XInputSetState(0, &vibration);

}

}
