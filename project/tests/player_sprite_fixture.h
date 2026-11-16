#pragma once
#include <filesystem>
void PreparePlayerLuaUIProject(const std::filesystem::path& root);
void RunPlayerLuaUIProject(const std::filesystem::path& manifest);
void RunPlayerSpriteChecks();
void RunPlayerLuaUIChecks();
