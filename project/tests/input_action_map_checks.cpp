#include "../engine/input/ActionMap.h"

#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {

void Require(bool condition, const char* message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

bool Near(float a, float b)
{
	return std::fabs(a - b) <= 0.00001f;
}

auto DownSet(std::set<int> keys)
{
	return [keys](int key) {
		return keys.count(key) != 0;
	};
}

}

int main()
{
	Engine::InputSystem::ActionMap actionMap;
	actionMap.Bind("move", 'W', Vector3{ 0.0f, 0.0f, 1.0f });
	actionMap.Bind("move", 'S', Vector3{ 0.0f, 0.0f, -1.0f });
	actionMap.Bind("move", 'A', Vector3{ -1.0f, 0.0f, 0.0f });
	actionMap.Bind("move", 'D', Vector3{ 1.0f, 0.0f, 0.0f });
	actionMap.Bind("start", ' ');

	Require(actionMap.BindingCount("move") == 4, "move should have four bindings");
	Require(actionMap.BindingCount("start") == 1, "start should have one binding");

	Vector3 diagonal = actionMap.Evaluate("move", DownSet({ 'W', 'D' }));
	Require(Near(diagonal.x, 1.0f) && Near(diagonal.z, 1.0f), "W+D should evaluate to diagonal move");

	Vector3 canceled = actionMap.Evaluate("move", DownSet({ 'W', 'S', 'A', 'D' }));
	Require(Near(canceled.x, 0.0f) && Near(canceled.z, 0.0f), "opposite movement keys should cancel");

	Require(actionMap.Active("start", DownSet({ ' ' })), "start should be active while bound key is down");
	Require(!actionMap.Active("unknown", DownSet({ ' ' })), "unknown action should not be active");

	actionMap.Bind("move", 'W', Vector3{ 0.0f, 0.0f, 2.0f });
	Require(actionMap.BindingCount("move") == 4, "rebinding the same key should update instead of duplicate");
	Require(Near(actionMap.Evaluate("move", DownSet({ 'W' })).z, 2.0f), "rebinding should update contribution");

	actionMap.ClearAction("move");
	Require(!actionMap.HasAction("move"), "cleared action should not exist");

	std::cout << "PASS: ActionMap bindings, evaluation, active checks, rebinding, clear\n";
}
