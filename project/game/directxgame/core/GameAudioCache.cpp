#include "game/directxgame/core/GameAudioCache.h"
#include "game/directxgame/core/ResourcePaths.h"
#include "Audio.h"
#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace DirectXGame {

namespace {

struct CachedSoundEntry {
	std::string fullPath;
	Engine::AudioSystem::SoundData soundData{};
	float baseVolume = 1.0f;
};

std::unordered_map<std::string, SoundHandle>& GetPathToHandle()
{
	static std::unordered_map<std::string, SoundHandle> cache;
	return cache;
}

std::unordered_map<uint32_t, CachedSoundEntry>& GetHandleToSound()
{
	static std::unordered_map<uint32_t, CachedSoundEntry> cache;
	return cache;
}

uint32_t& GetNextHandle()
{
	static uint32_t nextHandle = 1;
	return nextHandle;
}

float& GetMasterVolumeStorage()
{
	static float masterVolume = 1.0f;
	return masterVolume;
}

float ClampVolume(float volume)
{
	return std::clamp(volume, 0.0f, 1.0f);
}

float GetEffectiveVolume(float baseVolume)
{
	return ClampVolume(baseVolume) * GetMasterVolumeStorage();
}

std::unordered_map<std::string, float>& GetTunedVolumes()
{
	static std::unordered_map<std::string, float> tunedVolumes;
	return tunedVolumes;
}

void LogAudioLoadMessage(const std::string& relativePath, const std::string& fullPath, const char* reason)
{
	std::ostringstream message;
	message << "[DirectXGame][GameAudioCache::LoadWave] " << reason
		<< " relativePath=\"" << relativePath << "\""
		<< " fullPath=\"" << fullPath << "\""
		<< " currentPath=\"" << std::filesystem::current_path().generic_string() << "\"\n";
	OutputDebugStringA(message.str().c_str());
}

CachedSoundEntry* FindSoundEntry(SoundHandle handle)
{
	if (!handle) {
		return nullptr;
	}
	auto& handleToSound = GetHandleToSound();
	const auto it = handleToSound.find(handle.value);
	return it == handleToSound.end() ? nullptr : &it->second;
}

}

SoundHandle GameAudioCache::LoadWave(const std::string& relativePath)
{
	const std::string fullPath = ResourcePaths::MakePath(relativePath);
	auto& pathToHandle = GetPathToHandle();
	if (pathToHandle.contains(fullPath)) {
		return pathToHandle.at(fullPath);
	}

	CachedSoundEntry entry{};
	entry.fullPath = fullPath;
	if (!std::filesystem::exists(fullPath)) {
		LogAudioLoadMessage(relativePath, fullPath, "wave file is missing");
	}
	entry.soundData = Engine::AudioSystem::Audio::GetInstance()->SoundLoadWave(fullPath.c_str());
	if (entry.soundData.buffer.empty() || entry.soundData.bufferSize == 0) {
		LogAudioLoadMessage(relativePath, fullPath, "Audio::SoundLoadWave returned empty sound data");
		throw std::runtime_error(
			"GameAudioCache::LoadWave failed: empty sound data: " +
			fullPath);
	}
	const SoundHandle handle{ GetNextHandle()++ };
	pathToHandle.emplace(fullPath, handle);
	GetHandleToSound().emplace(handle.value, std::move(entry));
	return handle;
}

Engine::AudioSystem::SoundData* GameAudioCache::GetSoundData(SoundHandle handle)
{
	CachedSoundEntry* entry = FindSoundEntry(handle);
	return entry ? &entry->soundData : nullptr;
}

void GameAudioCache::Play(SoundHandle handle)
{
	CachedSoundEntry* entry = FindSoundEntry(handle);
	if (!entry) {
		return;
	}
	Engine::AudioSystem::Audio::GetInstance()->SoundPlayWave(entry->soundData);
	Engine::AudioSystem::Audio::GetInstance()->SetVolume(&entry->soundData,
		GetEffectiveVolume(entry->baseVolume));
}

void GameAudioCache::PlayLoop(SoundHandle handle)
{
	CachedSoundEntry* entry = FindSoundEntry(handle);
	if (!entry) {
		return;
	}
	Engine::AudioSystem::Audio::GetInstance()->SoundPlayWave(entry->soundData, true);
	Engine::AudioSystem::Audio::GetInstance()->SetVolume(&entry->soundData,
		GetEffectiveVolume(entry->baseVolume));
}

void GameAudioCache::Stop(SoundHandle handle)
{
	if (Engine::AudioSystem::SoundData* soundData = GetSoundData(handle)) {
		Engine::AudioSystem::Audio::GetInstance()->StopSpecificAudio(soundData);
	}
}

void GameAudioCache::Pause(SoundHandle handle)
{
	if (Engine::AudioSystem::SoundData* soundData = GetSoundData(handle)) {
		Engine::AudioSystem::Audio::GetInstance()->PauseSpecificAudio(soundData);
	}
}

void GameAudioCache::Resume(SoundHandle handle)
{
	if (Engine::AudioSystem::SoundData* soundData = GetSoundData(handle)) {
		Engine::AudioSystem::Audio::GetInstance()->ResumeSpecificAudio(soundData);
	}
}

void GameAudioCache::SetVolume(SoundHandle handle, float volume)
{
	CachedSoundEntry* entry = FindSoundEntry(handle);
	if (!entry) {
		return;
	}
	entry->baseVolume = ClampVolume(volume);
	Engine::AudioSystem::Audio::GetInstance()->SetVolume(&entry->soundData, GetEffectiveVolume(entry->baseVolume));
}

void GameAudioCache::SetVolumeFromTuning(SoundHandle handle, std::string_view key, float fallbackVolume)
{
	SetVolume(handle, GetTunedVolume(key, fallbackVolume));
}

void GameAudioCache::SetTunedVolume(std::string_view key, float volume)
{
	GetTunedVolumes()[std::string(key)] = ClampVolume(volume);
}

float GameAudioCache::GetTunedVolume(std::string_view key, float fallbackVolume)
{
	const auto& tunedVolumes = GetTunedVolumes();
	const auto it = tunedVolumes.find(std::string(key));
	if (it == tunedVolumes.end()) {
		return ClampVolume(fallbackVolume);
	}
	return it->second;
}

void GameAudioCache::SetMasterVolume(float volume)
{
	GetMasterVolumeStorage() = ClampVolume(volume);
	for (auto& [handle, entry] : GetHandleToSound()) {
		(void)handle;
		Engine::AudioSystem::Audio::GetInstance()->SetVolume(&entry.soundData, GetEffectiveVolume(entry.baseVolume));
	}
}

float GameAudioCache::GetMasterVolume()
{
	return GetMasterVolumeStorage();
}

bool GameAudioCache::IsPlaying(SoundHandle handle)
{
	Engine::AudioSystem::SoundData* soundData = GetSoundData(handle);
	return soundData &&
		Engine::AudioSystem::Audio::GetInstance()->IsSoundPlaying(soundData);
}

}
