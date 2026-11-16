#pragma once
#include "ScriptRuntime.h"

namespace Engine::Scripting {
// Validates a text-only Lua module and returns an independent-VM instance factory.
std::function<ScriptCallbacks()> CreateLuaScriptFactory(const std::string& source, const std::string& name,
    std::shared_ptr<const InputSystem::InputFrame> input, ScriptProperties& defaults,
    std::shared_ptr<ScriptAudioRequests> audio = {}, std::shared_ptr<LuaResourceBudget> budget = {},
    std::shared_ptr<const ScriptSceneView> scene = {}, std::shared_ptr<ScriptAnimatorRequests> animator = {},
    std::shared_ptr<ScriptSceneTransitions> transitions = {});
}
