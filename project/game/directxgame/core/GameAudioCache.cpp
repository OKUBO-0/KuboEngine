#include "game/directxgame/core/GameAudioCache.h"
#include "game/directxgame/core/DirectXGameResourcePaths.h"
#include "Audio.h"
#include <Windows.h>
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <sstream>
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

std::unordered_map<SoundHandle, CachedSoundEntry>& GetHandleToSound()
{
	static std::unordered_map<SoundHandle, CachedSoundEntry> cache;
	return cache;
}

SoundHandle& GetNextHandle()
{
	static SoundHandle nextHandle = 1;
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

}

SoundHandle GameAudioCache::LoadWave(const std::string& relativePath)
{
	const std::string fullPath = ResourcePaths::MakePath(relativePath);
	auto& pathToHandle = GetPathToHandle();
	if (pathToHandle.contains(fullPath)) {
		return pathToHandle.at(fullPath);
	}

	SoundHandle handle = GetNextHandle()++;
	CachedSoundEntry entry{};
	entry.fullPath = fullPath;
	if (!std::filesystem::exists(fullPath)) {
		LogAudioLoadMessage(relativePath, fullPath, "wave file is missing");
	}
	entry.soundData = Engine::AudioSystem::Audio::GetInstance()->SoundLoadWave(fullPath.c_str());
	if (entry.soundData.buffer.empty() || entry.soundData.bufferSize == 0) {
		LogAudioLoadMessage(relativePath, fullPath, "Audio::SoundLoadWave returned empty sound data");
		assert(false && "GameAudioCache::LoadWave failed; see OutputDebugString for path details");
	}
	pathToHandle.emplace(fullPath, handle);
	GetHandleToSound().emplace(handle, std::move(entry));
	return handle;
}

Engine::AudioSystem::SoundData* GameAudioCache::GetSoundData(SoundHandle handle)
{
	auto& handleToSound = GetHandleToSound();
	assert(handleToSound.contains(handle));
	return &handleToSound.at(handle).soundData;
}

void GameAudioCache::Play(SoundHandle handle)
{
	Engine::AudioSystem::Audio::GetInstance()->SoundPlayWave(*GetSoundData(handle));
	Engine::AudioSystem::Audio::GetInstance()->SetVolume(GetSoundData(handle),
		GetEffectiveVolume(GetHandleToSound().at(handle).baseVolume));
}

void GameAudioCache::PlayLoop(SoundHandle handle)
{
	Engine::AudioSystem::Audio::GetInstance()->SoundPlayWave(*GetSoundData(handle), true);
	Engine::AudioSystem::Audio::GetInstance()->SetVolume(GetSoundData(handle),
		GetEffectiveVolume(GetHandleToSound().at(handle).baseVolume));
}

void GameAudioCache::Stop(SoundHandle handle)
{
	Engine::AudioSystem::Audio::GetInstance()->StopSpecificAudio(GetSoundData(handle));
}

void GameAudioCache::Pause(SoundHandle handle)
{
	Engine::AudioSystem::Audio::GetInstance()->PauseSpecificAudio(GetSoundData(handle));
}

void GameAudioCache::Resume(SoundHandle handle)
{
	Engine::AudioSystem::Audio::GetInstance()->ResumeSpecificAudio(GetSoundData(handle));
}

void GameAudioCache::SetVolume(SoundHandle handle, float volume)
{
	auto& entry = GetHandleToSound().at(handle);
	entry.baseVolume = ClampVolume(volume);
	Engine::AudioSystem::Audio::GetInstance()->SetVolume(&entry.soundData, GetEffectiveVolume(entry.baseVolume));
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
	return Engine::AudioSystem::Audio::GetInstance()->IsSoundPlaying(GetSoundData(handle));
}

}
