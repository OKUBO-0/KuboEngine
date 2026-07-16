#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Engine::AudioSystem {
struct SoundData;
}

namespace DirectXGame {

enum class AudioBus {
	Bgm,
	Se,
};

struct SoundHandle {
	uint32_t value = 0;
	explicit operator bool() const { return value != 0; }
	friend bool operator==(const SoundHandle&, const SoundHandle&) = default;
};

class GameAudioCache {
public:
	static SoundHandle LoadWave(const std::string& relativePath, AudioBus bus = AudioBus::Se);
	static Engine::AudioSystem::SoundData* GetSoundData(SoundHandle handle);
	static void Play(SoundHandle handle);
	static void PlayLoop(SoundHandle handle);
	static void PlayTuned(
		SoundHandle handle,
		std::string_view key,
		float fallbackVolume,
		float minimumIntervalSeconds = 0.0f);
	static void Stop(SoundHandle handle);
	static void StopBus(AudioBus bus);
	static void SetVolume(SoundHandle handle, float volume);
	static void SetVolumeFromTuning(SoundHandle handle, std::string_view key, float fallbackVolume);
	static void SetTunedVolume(std::string_view key, float volume);
	static float GetTunedVolume(std::string_view key, float fallbackVolume);
	static void SetMasterVolume(float volume);
	static float GetMasterVolume();
	static void SetBusVolume(AudioBus bus, float volume);
	static float GetBusVolume(AudioBus bus);
	static bool IsPlaying(SoundHandle handle);
};

}
