#include "Input.h"
#include "HResult.h"
#include "WinApp.h"
#include "Logger.h"
#include "engine/base/ProjectPaths.h"
#include <cassert>
#include <cstring>
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")
#include <cmath>

namespace Engine::InputSystem {

namespace {
constexpr float kGamePadAxisMaxValue = 32767.0f;
constexpr float kGamePadDeadZone = 0.2f;
constexpr float kGamePadVibrationMaxValue = 65535.0f;

float NormalizeStickAxis(SHORT rawValue)
{
	const float value = static_cast<float>(rawValue) / kGamePadAxisMaxValue;
	return std::fabs(value) < kGamePadDeadZone ? 0.0f : value;
}
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
	actionBindingsPath_.clear();
	gamepadConnected_=previousGamepadConnected_=false;
	ZeroMemory(&state_,sizeof(state_));ZeroMemory(&prevState_,sizeof(prevState_));

}

void Input::Initialize(Engine::Base::WinApp* winApp, const std::filesystem::path& projectRoot)
{
	actionBindingsPath_ = Engine::Base::ProjectPaths(projectRoot).InputBindings();
	winApp_ = winApp;
	ZeroMemory(key,sizeof(key));ZeroMemory(preKey,sizeof(preKey));
	ZeroMemory(&mouse,sizeof(mouse));ZeroMemory(&preMouse,sizeof(preMouse));
	mousePos={};ZeroMemory(&state_,sizeof(state_));ZeroMemory(&prevState_,sizeof(prevState_));
	gamepadConnected_=previousGamepadConnected_=false;
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

	InitializeDefaultActionMap();
	std::string bindingError;
	const auto& bindingPath = actionBindingsPath_;
	if (std::filesystem::exists(bindingPath) && !actionMap_.Load(bindingPath, bindingError)) {
		Engine::Base::Logger::Log("Input bindings load failed: " + bindingError);
	}

}

void Input::Update()
{
	//前回のキー入力を保存
	memcpy(preKey, key, sizeof(key));
	//キーボード情報の取得
	keyboard->Acquire();
	//全キーボード入力情報を取得
	if (FAILED(keyboard->GetDeviceState(sizeof(key), key))) ZeroMemory(key,sizeof(key));


	//前回のマウス入力を保存
	memcpy(&preMouse, &mouse, sizeof(mouse));
	//マウス情報の取得
	devMouse_->Acquire();
	//全マウス入力情報を取得
	if (FAILED(devMouse_->GetDeviceState(sizeof(mouse), &mouse))) ZeroMemory(&mouse,sizeof(mouse));
	//マウス座標の取得
	POINT point;
	GetCursorPos(&point);
	ScreenToClient(winApp_->GetHwnd(), &point);
	mousePos.x = (float)point.x;
	mousePos.y = (float)point.y;

	// ゲームパッドは前フレームとの差分も使うため状態を退避してから更新する
	prevState_ = state_;
	previousGamepadConnected_ = gamepadConnected_;

	// XInputの状態を取得
	ZeroMemory(&state_, sizeof(XINPUT_STATE));
	if (XInputGetState(0, &state_) == ERROR_SUCCESS) {
		gamepadConnected_ = true;
	} else {
		gamepadConnected_ = false;
	}
	
}

GamepadSample Input::CapturePadSample(bool previous) const noexcept
{
	const auto& pad=(previous ? prevState_ : state_).Gamepad;
	return {previous ? previousGamepadConnected_ : gamepadConnected_,pad.wButtons,
		pad.sThumbLX,pad.sThumbLY,pad.sThumbRX,pad.sThumbRY,pad.bLeftTrigger,pad.bRightTrigger};
}

InputFrame Input::CaptureFrame(bool enabled) const
{
	std::array<bool,256> current{},previous{};
	std::array<bool,8> buttons{},previousButtons{};
	for(size_t index=0;index<current.size();++index) { current[index]=key[index]!=0;previous[index]=preKey[index]!=0; }
	for(size_t index=0;index<buttons.size();++index) { buttons[index]=mouse.rgbButtons[index]!=0;previousButtons[index]=preMouse.rgbButtons[index]!=0; }
	const bool available=enabled && winApp_ && GetForegroundWindow()==winApp_->GetHwnd();
	auto frame=MakeInputFrame(actionMap_,current,previous,buttons,previousButtons,available,
		CapturePadSample(false),CapturePadSample(true));
	if(available) {
		frame.mouseX=mousePos.x;frame.mouseY=mousePos.y;
		frame.deltaX=static_cast<float>(mouse.lX);frame.deltaY=static_cast<float>(mouse.lY);frame.wheel=static_cast<float>(mouse.lZ)/WHEEL_DELTA;
	}
	return frame;
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

void Input::BindKeyAction(const std::string& action, BYTE keyNumber, const Vector3& contribution)
{
	actionMap_.Bind(action, keyNumber, contribution);
}

void Input::BindKeyAction(const std::string& action, BYTE keyNumber)
{
	actionMap_.Bind(action, keyNumber);
}

bool Input::PushAction(const std::string& action) const
{
	return actionMap_.Active(action, [this](int keyNumber) {
		return keyNumber>=0 && keyNumber<256 && key[keyNumber] != 0;
	},MakeGamepadFrame(CapturePadSample(false),CapturePadSample(true),true));
}

bool Input::TriggerAction(const std::string& action) const
{
	const auto found=actionMap_.Bindings().find(action);if(found==actionMap_.Bindings().end())return false;
	const auto current=MakeGamepadFrame(CapturePadSample(false),CapturePadSample(true),true);
	const auto previous=MakeGamepadFrame(CapturePadSample(true),{},true);
	const auto down=[this](int code){return code>=0 && code<256 && key[code]!=0;};
	const auto wasDown=[this](int code){return code>=0 && code<256 && preKey[code]!=0;};
	for(const auto& binding:found->second)
		if(ActionMap::IsActive(binding,ActionMap::Value(binding,down,current)) && !ActionMap::IsActive(binding,ActionMap::Value(binding,wasDown,previous)))return true;
	return false;
}

Vector3 Input::EvaluateAction(const std::string& action) const
{
	return actionMap_.Evaluate(action, [this](int keyNumber) {
		return keyNumber>=0 && keyNumber<256 && key[keyNumber] != 0;
	},MakeGamepadFrame(CapturePadSample(false),CapturePadSample(true),true));
}

void Input::ClearAction(const std::string& action)
{
	actionMap_.ClearAction(action);
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

	const SHORT rawX = right ? state_.Gamepad.sThumbRX : state_.Gamepad.sThumbLX;
	return NormalizeStickAxis(rawX);
}

float Input::GetGamePadStickY(bool right)
{
	if (!gamepadConnected_) return 0.0f;

	const SHORT rawY = right ? state_.Gamepad.sThumbRY : state_.Gamepad.sThumbLY;
	return NormalizeStickAxis(rawY);
}

float Input::GetPreviousGamePadStickX(bool right)
{
	if (!gamepadConnected_) return 0.0f;

	const SHORT rawX = right ? prevState_.Gamepad.sThumbRX : prevState_.Gamepad.sThumbLX;
	return NormalizeStickAxis(rawX);
}

float Input::GetPreviousGamePadStickY(bool right)
{
	if (!gamepadConnected_) return 0.0f;

	const SHORT rawY = right ? prevState_.Gamepad.sThumbRY : prevState_.Gamepad.sThumbLY;
	return NormalizeStickAxis(rawY);
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

void Input::InitializeDefaultActionMap()
{
	BindKeyAction("move", DIK_W, Vector3{ 0.0f, 0.0f, 1.0f });
	BindKeyAction("move", DIK_S, Vector3{ 0.0f, 0.0f, -1.0f });
	BindKeyAction("move", DIK_A, Vector3{ -1.0f, 0.0f, 0.0f });
	BindKeyAction("move", DIK_D, Vector3{ 1.0f, 0.0f, 0.0f });
	BindKeyAction("start", DIK_SPACE);
	BindKeyAction("toggle", DIK_SPACE);
}

}
