#include "Audio.h"
#include "HResult.h"
#include "../base/AssetPath.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <wrl.h>

namespace Engine::AudioSystem {

Audio* Audio::GetInstance()
{
    static Audio instance;
    return &instance;
}

void Audio::Initialize(const std::filesystem::path& projectRoot)
{
    const auto ownedRoot = Engine::Base::ProjectPaths(projectRoot).Root();
    // XAudio2 本体とマスターボイスを先に生成して、以降の SourceVoice 作成先を確保する
    HRESULT hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    Engine::Base::ThrowIfFailed(hr, "XAudio2Create");

    hr = xAudio2->CreateMasteringVoice(&masterVoice);
    Engine::Base::ThrowIfFailed(hr, "IXAudio2::CreateMasteringVoice");
    projectRoot_ = ownedRoot;
}

void Audio::Finalize()
{
    // すべての再生中の音を停止
    for (auto& [soundData, voices] : activeVoices) {
        (void)soundData;
        for (IXAudio2SourceVoice* voice : voices) {
            if (voice) {
                voice->Stop();
                voice->DestroyVoice();
            }
        }
    }
    activeVoices.clear();

    if(masterVoice) masterVoice->DestroyVoice();
    masterVoice = nullptr;
    xAudio2.Reset();
    projectRoot_.clear();
}

SoundData Audio::SoundLoadWave(const char* filename)
{
    if(!filename) throw std::invalid_argument("Wave filename must not be null");
    SoundData sound;std::string error;
    if(!TrySoundLoadWave(Engine::Base::PathFromUtf8(filename),sound,error))
        throw std::runtime_error("Audio::SoundLoadWave: "+error);
    return sound;
}

bool Audio::TrySoundLoadWave(const std::filesystem::path& filename,SoundData& output,std::string& error) const
{
    try {
        if(projectRoot_.empty()) throw std::logic_error("Audio must be initialized before loading project sound");
        if(activeVoices.contains(&output)) throw std::logic_error("Stop sound voices before replacing their buffer");
        if(filename.empty() || filename.native().find(L'\0')!=std::wstring::npos) throw std::invalid_argument("Invalid Wave filename");
        const auto path=(filename.is_absolute()?filename:projectRoot_/filename).lexically_normal();
        return WaveFile::Load(path,output,error);
    } catch(const std::exception& exception) {error=exception.what();return false;}
}

void Audio::SoundUnload(SoundData* soundData)
{
    StopSpecificAudio(soundData);
    soundData->buffer.clear();
    soundData->bufferSize = 0;
    soundData->wfex = {};
}

void Audio::SoundPlayWave(
	const SoundData& soundData,
	bool loop,
	float volume)
{
    if(!xAudio2 || !masterVoice || soundData.buffer.empty() || soundData.bufferSize!=soundData.buffer.size() ||
        soundData.wfex.cbSize!=0 || (soundData.wfex.wFormatTag!=WAVE_FORMAT_PCM && soundData.wfex.wFormatTag!=WAVE_FORMAT_IEEE_FLOAT) || !std::isfinite(volume))
        throw std::invalid_argument("Audio playback requires initialized device, supported complete buffer and finite volume");
    HRESULT hr;
    PruneFinishedVoices();

    IXAudio2SourceVoice* newVoice = nullptr;
	hr = xAudio2->CreateSourceVoice(&newVoice, &soundData.wfex);
	Engine::Base::ThrowIfFailed(hr, "IXAudio2::CreateSourceVoice");
	hr = newVoice->SetVolume(std::clamp(volume, 0.0f, 1.0f));
	if (FAILED(hr)) {
		newVoice->DestroyVoice();
		Engine::Base::ThrowIfFailed(hr, "IXAudio2Voice::SetVolume before start");
	}

    // 読み込み済みバッファをそのまま SourceVoice へ渡して再生する
    XAUDIO2_BUFFER buf{};
    buf.pAudioData = soundData.buffer.data();
    buf.AudioBytes = soundData.bufferSize;
    buf.Flags = XAUDIO2_END_OF_STREAM;
    buf.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

    hr = newVoice->SubmitSourceBuffer(&buf);
    if (FAILED(hr)) {
        newVoice->DestroyVoice();
        Engine::Base::ThrowIfFailed(
            hr,
            "IXAudio2SourceVoice::SubmitSourceBuffer");
    }
    hr = newVoice->Start();
    if (FAILED(hr)) {
        newVoice->DestroyVoice();
        Engine::Base::ThrowIfFailed(
            hr,
            "IXAudio2SourceVoice::Start");
    }

    try {
        activeVoices[&soundData].push_back(newVoice);
    } catch (...) {
        newVoice->Stop();
        newVoice->DestroyVoice();
        throw;
    }
}

void Audio::PruneFinishedVoices()
{
    for (auto mapIt = activeVoices.begin(); mapIt != activeVoices.end();) {
        std::vector<IXAudio2SourceVoice*>& voices = mapIt->second;
        std::erase_if(voices, [](IXAudio2SourceVoice* voice) {
            if (!voice) {
                return true;
            }
            XAUDIO2_VOICE_STATE state{};
            voice->GetState(&state);
            if (state.BuffersQueued > 0) {
                return false;
            }
            voice->DestroyVoice();
            return true;
            });
        if (voices.empty()) {
            mapIt = activeVoices.erase(mapIt);
        } else {
            ++mapIt;
        }
    }
}

void Audio::StopAudio()
{
    // 生成済み Voice をすべて破棄して再生状態をリセットする
    for (auto& [soundData, voices] : activeVoices) {
        (void)soundData;
        for (IXAudio2SourceVoice* voice : voices) {
            if (voice) {
                voice->Stop();
                voice->DestroyVoice();
            }
        }
    }
    activeVoices.clear();
}

void Audio::StopSpecificAudio(SoundData* soundData)
{
    auto it = activeVoices.find(soundData);
    if (it != activeVoices.end()) {
        for (IXAudio2SourceVoice* voice : it->second) {
            if (voice) {
                voice->Stop();
                voice->DestroyVoice();
            }
        }
        activeVoices.erase(it);
    }
}

void Audio::PauseAudio()
{
    for (auto& [soundData, voices] : activeVoices) {
        (void)soundData;
        for (IXAudio2SourceVoice* voice : voices) {
            if (voice) {
                voice->Stop();
            }
        }
    }
}

void Audio::PauseSpecificAudio(SoundData* soundData)
{
    auto it = activeVoices.find(soundData);
    if (it != activeVoices.end()) {
        for (IXAudio2SourceVoice* voice : it->second) {
            if (voice) {
                voice->Stop();
            }
        }
    }
}

void Audio::ResumeAudio()
{
    for (auto& [soundData, voices] : activeVoices) {
        (void)soundData;
        for (IXAudio2SourceVoice* voice : voices) {
            if (voice) {
                Engine::Base::ThrowIfFailed(
                    voice->Start(),
                    "IXAudio2SourceVoice::Start resume all");
            }
        }
    }
}

void Audio::ResumeSpecificAudio(SoundData* soundData)
{
    auto it = activeVoices.find(soundData);
    if (it != activeVoices.end()) {
        for (IXAudio2SourceVoice* voice : it->second) {
            if (voice) {
                Engine::Base::ThrowIfFailed(
                    voice->Start(),
                    "IXAudio2SourceVoice::Start resume specific");
            }
        }
    }
}

void Audio::SetPlaybackSpeed(float speed)
{
    // 再生中の全 Voice に同じ周波数比を適用する
    for (auto& [soundData, voices] : activeVoices) {
        (void)soundData;
        for (IXAudio2SourceVoice* voice : voices) {
            if (voice) {
                HRESULT hr = voice->SetFrequencyRatio(speed);
                Engine::Base::ThrowIfFailed(
                    hr,
                    "IXAudio2SourceVoice::SetFrequencyRatio");
            }
        }
    }
}

void Audio::SetPlaybackSpeed(SoundData* soundData, float speed)
{
    auto it = activeVoices.find(soundData);
    if (it != activeVoices.end()) {
        for (IXAudio2SourceVoice* voice : it->second) {
            if (voice) {
                HRESULT hr = voice->SetFrequencyRatio(speed);
                Engine::Base::ThrowIfFailed(
                    hr,
                    "IXAudio2SourceVoice::SetFrequencyRatio");
            }
        }
    }
}

bool Audio::IsSoundPlaying() const
{
    for (const auto& [soundData, voices] : activeVoices) {
        (void)soundData;
        for (IXAudio2SourceVoice* voice : voices) {
            if (voice) {
                XAUDIO2_VOICE_STATE state{};
                voice->GetState(&state);
                if (state.BuffersQueued > 0) {
                    return true;
                }
            }
        }
    }
    return false;
}



bool Audio::IsSoundPlaying(SoundData* soundData) const
{
    auto it = activeVoices.find(soundData);
    if (it != activeVoices.end()) {
        for (IXAudio2SourceVoice* voice : it->second) {
            if (voice) {
                XAUDIO2_VOICE_STATE state{};
                voice->GetState(&state);
                if (state.BuffersQueued > 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

void Audio::SetVolume(float volume)
{
    for (auto& [soundData, voices] : activeVoices) {
        (void)soundData;
        for (IXAudio2SourceVoice* voice : voices) {
            if (voice) {
                Engine::Base::ThrowIfFailed(
                    voice->SetVolume(volume),
                    "IXAudio2Voice::SetVolume all");
            }
        }
    }
}

size_t Audio::GetQueuedVoiceCount(const SoundData* soundData) const
{
    const auto found=activeVoices.find(soundData);
    if(found==activeVoices.end()) return 0;
    size_t count=0;
    for(auto* voice:found->second) if(voice) {
        XAUDIO2_VOICE_STATE state{};voice->GetState(&state);
        if(state.BuffersQueued) ++count;
    }
    return count;
}
uint64_t Audio::GetPlayedSampleCount(const SoundData* soundData) const
{
    const auto found=activeVoices.find(soundData);if(found==activeVoices.end()) return 0;
    uint64_t count=0;
    for(auto* voice:found->second) if(voice) {XAUDIO2_VOICE_STATE state{};voice->GetState(&state);count+=state.SamplesPlayed;}
    return count;
}

void Audio::SetVolume(SoundData* soundData, float volume)
{
    auto it = activeVoices.find(soundData);
    if (it != activeVoices.end()) {
        for (IXAudio2SourceVoice* voice : it->second) {
            if (voice) {
                Engine::Base::ThrowIfFailed(
                    voice->SetVolume(volume),
                    "IXAudio2Voice::SetVolume specific");
            }
        }
    }
}

bool Audio::QueuedVoicesHaveVolume(const SoundData* soundData,float volume) const
{
    if(!std::isfinite(volume)) return false;
    const auto found=activeVoices.find(soundData);if(found==activeVoices.end()) return false;
    size_t queued=0;
    for(auto* voice:found->second) if(voice) {
        XAUDIO2_VOICE_STATE state{};voice->GetState(&state);if(!state.BuffersQueued) continue;
        float actual=0;voice->GetVolume(&actual);if(std::abs(actual-volume)>0.00001f) return false;++queued;
    }
    return queued>0;
}

}
