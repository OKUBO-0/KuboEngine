#pragma once
#include <filesystem>
void PreparePlayerLightingProject(const std::filesystem::path& root);
void RunPlayerLightingProject(const std::filesystem::path& manifest);
void RunPlayerLightingChecks();
