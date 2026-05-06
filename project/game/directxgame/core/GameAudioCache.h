#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Engine::AudioSystem {
struct SoundData;
}

namespace DirectXGame {

using SoundHandle = uint32_t;

class GameAudioCache {
public:
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
