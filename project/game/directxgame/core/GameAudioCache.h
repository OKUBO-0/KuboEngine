#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Engine::AudioSystem {
struct SoundData;
}

namespace DirectXGame {

struct SoundHandle {
	uint32_t value = 0;

	explicit operator bool() const { return value != 0; }
	friend bool operator==(const SoundHandle&, const SoundHandle&) = default;
};

class GameAudioCache {
public:
	/// Invalid handles are ignored by playback/control operations.
	/// Handles are monotonic and remain valid for the process lifetime.
	static SoundHandle LoadWave(const std::string& relativePath);
	static Engine::AudioSystem::SoundData* GetSoundData(SoundHandle handle);
	static void Play(SoundHandle handle);
	static void PlayLoop(SoundHandle handle);
	static void Stop(SoundHandle handle);
	static void Pause(SoundHandle handle);
	static void Resume(SoundHandle handle);
	static void SetVolume(SoundHandle handle, float volume);
	static void SetVolumeFromTuning(SoundHandle handle, std::string_view key, float fallbackVolume);
	static void SetTunedVolume(std::string_view key, float volume);
	static float GetTunedVolume(std::string_view key, float fallbackVolume);
	static void SetMasterVolume(float volume);
	static float GetMasterVolume();
	static bool IsPlaying(SoundHandle handle);
};

}
