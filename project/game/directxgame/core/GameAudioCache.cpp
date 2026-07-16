#include "GameAudioCache.h"

#include "Audio.h"
#include "ResourcePaths.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace DirectXGame {
namespace {

struct CachedSoundEntry {
	Engine::AudioSystem::SoundData soundData{};
	AudioBus bus = AudioBus::Se;
	float baseVolume = 1.0f;
};

std::deque<CachedSoundEntry>& GetSoundEntries()
{
	static std::deque<CachedSoundEntry> entries(1);
	return entries;
}

std::unordered_map<std::string, SoundHandle>& GetPathToHandle()
{
	static std::unordered_map<std::string, SoundHandle> cache;
	return cache;
}

std::unordered_map<std::string, float>& GetTunedVolumes()
{
	static std::unordered_map<std::string, float> volumes;
	return volumes;
}

std::unordered_map<std::string, std::chrono::steady_clock::time_point>& GetLastPlayTimes()
{
	static std::unordered_map<std::string, std::chrono::steady_clock::time_point> times;
	return times;
}

float& MasterVolume()
{
	static float volume = 0.85f;
	return volume;
}

float& BgmVolume()
{
	static float volume = 0.28f;
	return volume;
}

float& SeVolume()
{
	static float volume = 0.78f;
	return volume;
}

float ClampVolume(float volume)
{
	return std::clamp(volume, 0.0f, 1.0f);
}

float BusVolume(AudioBus bus)
{
	return bus == AudioBus::Bgm ? BgmVolume() : SeVolume();
}

float EffectiveVolume(const CachedSoundEntry& entry)
{
	return ClampVolume(MasterVolume() * BusVolume(entry.bus) * entry.baseVolume);
}

CachedSoundEntry* FindSoundEntry(SoundHandle handle)
{
	std::deque<CachedSoundEntry>& entries = GetSoundEntries();
	if (!handle || handle.value >= entries.size()) {
		return nullptr;
	}
	return &entries[handle.value];
}

void ApplyVolumeToActiveVoices()
{
	for (CachedSoundEntry& entry : GetSoundEntries()) {
		if (entry.soundData.buffer.empty()) {
			continue;
		}
		Engine::AudioSystem::Audio::GetInstance()->SetVolume(
			&entry.soundData,
			EffectiveVolume(entry));
	}
}

void LogAudioLoadMessage(const std::string& relativePath, const std::string& fullPath, const char* reason)
{
	std::ostringstream message;
	message << "[DirectXGame][GameAudioCache::LoadWave] " << reason
		<< " relativePath=\"" << relativePath
		<< "\" fullPath=\"" << fullPath << "\"\n";
	OutputDebugStringA(message.str().c_str());
}

}

SoundHandle GameAudioCache::LoadWave(const std::string& relativePath, AudioBus bus)
{
	const std::string cacheKey =
		std::string(bus == AudioBus::Bgm ? "bgm:" : "se:") + relativePath;
	auto& cache = GetPathToHandle();
	if (const auto it = cache.find(cacheKey); it != cache.end()) {
		return it->second;
	}

	const std::string fullPath = ResourcePaths::MakeAudioPath(relativePath);
	if (!std::filesystem::exists(fullPath)) {
		LogAudioLoadMessage(relativePath, fullPath, "wave file is missing");
		return {};
	}

	CachedSoundEntry entry{};
	entry.bus = bus;
	entry.soundData = Engine::AudioSystem::Audio::GetInstance()->SoundLoadWave(fullPath.c_str());
	if (entry.soundData.buffer.empty()) {
		LogAudioLoadMessage(relativePath, fullPath, "Audio::SoundLoadWave returned empty sound data");
		throw std::runtime_error("GameAudioCache::LoadWave failed: empty sound data: " + fullPath);
	}

	std::deque<CachedSoundEntry>& entries = GetSoundEntries();
	const SoundHandle handle{ static_cast<uint32_t>(entries.size()) };
	entries.push_back(std::move(entry));
	cache.emplace(cacheKey, handle);
	return handle;
}

Engine::AudioSystem::SoundData* GameAudioCache::GetSoundData(SoundHandle handle)
{
	if (CachedSoundEntry* entry = FindSoundEntry(handle)) {
		return &entry->soundData;
	}
	return nullptr;
}

void GameAudioCache::Play(SoundHandle handle)
{
	if (CachedSoundEntry* entry = FindSoundEntry(handle)) {
		Engine::AudioSystem::Audio::GetInstance()->SoundPlayWave(
			entry->soundData,
			false,
			EffectiveVolume(*entry));
	}
}

void GameAudioCache::PlayLoop(SoundHandle handle)
{
	if (CachedSoundEntry* entry = FindSoundEntry(handle)) {
		Engine::AudioSystem::Audio::GetInstance()->StopSpecificAudio(
			&entry->soundData);
		Engine::AudioSystem::Audio::GetInstance()->SoundPlayWave(
			entry->soundData,
			true,
			EffectiveVolume(*entry));
	}
}

void GameAudioCache::PlayTuned(
	SoundHandle handle,
	std::string_view key,
	float fallbackVolume,
	float minimumIntervalSeconds)
{
	if (!handle) {
		return;
	}
	if (minimumIntervalSeconds > 0.0f) {
		const std::string keyText(key);
		const auto now = std::chrono::steady_clock::now();
		auto& lastPlayTimes = GetLastPlayTimes();
		if (const auto it = lastPlayTimes.find(keyText); it != lastPlayTimes.end()) {
			const float elapsed =
				std::chrono::duration<float>(now - it->second).count();
			if (elapsed < minimumIntervalSeconds) {
				return;
			}
		}
		lastPlayTimes[keyText] = now;
	}
	SetVolumeFromTuning(handle, key, fallbackVolume);
	Play(handle);
}

void GameAudioCache::Stop(SoundHandle handle)
{
	if (Engine::AudioSystem::SoundData* soundData = GetSoundData(handle)) {
		Engine::AudioSystem::Audio::GetInstance()->StopSpecificAudio(soundData);
	}
}

void GameAudioCache::StopBus(AudioBus bus)
{
	for (CachedSoundEntry& entry : GetSoundEntries()) {
		if (entry.soundData.buffer.empty() || entry.bus != bus) {
			continue;
		}
		Engine::AudioSystem::Audio::GetInstance()->StopSpecificAudio(
			&entry.soundData);
	}
}

void GameAudioCache::SetVolume(SoundHandle handle, float volume)
{
	CachedSoundEntry* entry = FindSoundEntry(handle);
	if (!entry) {
		return;
	}
	entry->baseVolume = ClampVolume(volume);
	Engine::AudioSystem::Audio::GetInstance()->SetVolume(
		&entry->soundData,
		EffectiveVolume(*entry));
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
	const auto& volumes = GetTunedVolumes();
	if (const auto it = volumes.find(std::string(key)); it != volumes.end()) {
		return it->second;
	}
	return ClampVolume(fallbackVolume);
}

void GameAudioCache::SetMasterVolume(float volume)
{
	MasterVolume() = ClampVolume(volume);
	ApplyVolumeToActiveVoices();
}

float GameAudioCache::GetMasterVolume()
{
	return MasterVolume();
}

void GameAudioCache::SetBusVolume(AudioBus bus, float volume)
{
	if (bus == AudioBus::Bgm) {
		BgmVolume() = ClampVolume(volume);
	} else {
		SeVolume() = ClampVolume(volume);
	}
	ApplyVolumeToActiveVoices();
}

float GameAudioCache::GetBusVolume(AudioBus bus)
{
	return bus == AudioBus::Bgm ? BgmVolume() : SeVolume();
}

bool GameAudioCache::IsPlaying(SoundHandle handle)
{
	Engine::AudioSystem::SoundData* soundData = GetSoundData(handle);
	return soundData && Engine::AudioSystem::Audio::GetInstance()->IsSoundPlaying(soundData);
}

}
