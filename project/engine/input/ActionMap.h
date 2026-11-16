#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

#include "Vector3.h"
#include "GamepadFrame.h"

namespace Engine::InputSystem {

/// @brief キー入力をゲーム内アクション名へ対応付ける軽量マップ
/// @details キー状態の取得を呼び出し側の関数に委譲するため、DirectInput に依存せず単体テストしやすい。
class ActionMap
{
public:
	using KeyDownFn = std::function<bool(int)>;
	enum class Source { Keyboard, PadButton, PadAxis };
	enum class PadAxis { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

	struct Binding {
		int key = 0;
		Vector3 contribution{};
		Source source = Source::Keyboard;
		float activationThreshold = 0.5f;
	};

	void Bind(const std::string& action, int key, const Vector3& contribution);
	void Bind(const std::string& action, int key);
	void BindPadButton(const std::string& action, GamepadButton button, const Vector3& contribution={1,0,0});
	void BindPadAxis(const std::string& action, PadAxis axis, const Vector3& contribution, float activationThreshold=0.5f);
	static float Value(const Binding& binding, const KeyDownFn& down, const GamepadFrame& pad);
	static bool IsActive(const Binding& binding, float value) noexcept;
	void ClearAction(const std::string& action);
	void Clear();

	bool HasAction(const std::string& action) const;
	bool Active(const std::string& action, const KeyDownFn& down) const;
	Vector3 Evaluate(const std::string& action, const KeyDownFn& down) const;
	bool Active(const std::string& action, const KeyDownFn& down, const GamepadFrame& pad) const;
	Vector3 Evaluate(const std::string& action, const KeyDownFn& down, const GamepadFrame& pad) const;
	std::size_t BindingCount(const std::string& action) const;

	const std::unordered_map<std::string, std::vector<Binding>>& Bindings() const { return actions_; }
	bool Save(const std::filesystem::path& path, std::string& error) const;
	bool Load(const std::filesystem::path& path, std::string& error);

private:
	void BindSource(const std::string& action, Binding binding);
	std::unordered_map<std::string, std::vector<Binding>> actions_;
};

}
