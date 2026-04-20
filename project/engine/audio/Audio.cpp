#include "Audio.h"
#include <wrl.h>

namespace Engine::AudioSystem {

namespace {
constexpr char kRiffChunkId[] = "RIFF";
constexpr char kWaveChunkId[] = "WAVE";
constexpr char kFormatChunkId[] = "fmt ";
constexpr char kDataChunkId[] = "data";
constexpr char kJunkChunkId[] = "JUNK";
constexpr char kListChunkId[] = "LIST";
constexpr size_t kChunkIdLength = 4;

RiffHeader ReadRiffHeader(std::ifstream& file)
{
    RiffHeader riff{};
    file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
    assert(strncmp(riff.chunk.id, kRiffChunkId, kChunkIdLength) == 0);
    assert(strncmp(riff.type, kWaveChunkId, kChunkIdLength) == 0);
    return riff;
}

FormatChunk ReadFormatChunk(std::ifstream& file)
{
    FormatChunk format{};
    file.read(reinterpret_cast<char*>(&format), sizeof(ChunkHeader));
    assert(strncmp(format.chunk.id, kFormatChunkId, kChunkIdLength) == 0);
    assert(format.chunk.size <= sizeof(format.fmt));
    file.read(reinterpret_cast<char*>(&format.fmt), format.chunk.size);
    return format;
}

bool IsSkippableChunk(const ChunkHeader& chunk)
{
    return strncmp(chunk.id, kJunkChunkId, kChunkIdLength) == 0 ||
        strncmp(chunk.id, kListChunkId, kChunkIdLength) == 0;
}

ChunkHeader ReadDataChunk(std::ifstream& file)
{
    ChunkHeader data{};
    file.read(reinterpret_cast<char*>(&data), sizeof(data));
    while (IsSkippableChunk(data)) {
        file.seekg(data.size, std::ios_base::cur);
        file.read(reinterpret_cast<char*>(&data), sizeof(data));
    }

    assert(strncmp(data.id, kDataChunkId, kChunkIdLength) == 0);
    return data;
}

SoundData ReadSoundData(std::ifstream& file, const FormatChunk& format, const ChunkHeader& data)
{
    SoundData soundData{};
    soundData.buffer.resize(data.size);
    file.read(reinterpret_cast<char*>(soundData.buffer.data()), data.size);
    soundData.wfex = format.fmt;
    soundData.bufferSize = data.size;
    return soundData;
}
}

Audio* Audio::GetInstance()
{
    static Audio instance;
    return &instance;
}

void Audio::Initialize()
{
    // XAudio2 本体とマスターボイスを先に生成して、以降の SourceVoice 作成先を確保する
    HRESULT hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    assert(SUCCEEDED(hr));

    hr = xAudio2->CreateMasteringVoice(&masterVoice);
    assert(SUCCEEDED(hr));
}

void Audio::Finalize()
{
    // すべての再生中の音を停止
    for (auto& voiceEntry : activeVoices) {
        IXAudio2SourceVoice* voice = voiceEntry.second;
        if (voice) {
            voice->Stop();
            voice->DestroyVoice();
        }
    }
    activeVoices.clear();

    xAudio2.Reset();
    masterVoice = nullptr;
}

SoundData Audio::SoundLoadWave(const char* filename)
{
    std::ifstream file(filename, std::ios_base::binary);
    assert(file.is_open());

    // RIFF -> fmt -> data の順で抽出し、補助チャンクはヘルパー側で読み飛ばす
    ReadRiffHeader(file);
    FormatChunk format = ReadFormatChunk(file);
    ChunkHeader data = ReadDataChunk(file);
    SoundData soundData = ReadSoundData(file, format, data);
    file.close();

    return soundData;
}

void Audio::SoundUnload(SoundData* soundData)
{
    StopSpecificAudio(soundData);
    soundData->buffer.clear();
    soundData->bufferSize = 0;
    soundData->wfex = {};
}

void Audio::SoundPlayWave(const SoundData& soundData)
{
    HRESULT hr;

    // 同じ SoundData の二重再生を避けるため、既存 Voice があれば先に止める
    StopSpecificAudio(const_cast<SoundData*>(&soundData));

    IXAudio2SourceVoice* newVoice = nullptr;
    hr = xAudio2->CreateSourceVoice(&newVoice, &soundData.wfex);
    assert(SUCCEEDED(hr));

    // 読み込み済みバッファをそのまま SourceVoice へ渡して再生する
    XAUDIO2_BUFFER buf{};
    buf.pAudioData = soundData.buffer.data();
    buf.AudioBytes = soundData.bufferSize;
    buf.Flags = XAUDIO2_END_OF_STREAM;

    hr = newVoice->SubmitSourceBuffer(&buf);
    hr = newVoice->Start();

    activeVoices[const_cast<SoundData*>(&soundData)] = newVoice;
}

void Audio::StopAudio()
{
    // 生成済み Voice をすべて破棄して再生状態をリセットする
    for (auto& voiceEntry : activeVoices) {
        IXAudio2SourceVoice* voice = voiceEntry.second;
        if (voice) {
            voice->Stop();
            voice->DestroyVoice();
        }
    }
    activeVoices.clear();
}

void Audio::StopSpecificAudio(SoundData* soundData)
{
    auto it = activeVoices.find(soundData);
    if (it != activeVoices.end()) {
        IXAudio2SourceVoice* voice = it->second;
        if (voice) {
            voice->Stop();
            voice->DestroyVoice();
        }
        activeVoices.erase(it);
    }
}

void Audio::PauseAudio()
{
    for (auto& voiceEntry : activeVoices) {
        IXAudio2SourceVoice* voice = voiceEntry.second;
        if (voice) {
            voice->Stop();
        }
    }
}

void Audio::PauseSpecificAudio(SoundData* soundData)
{
    auto it = activeVoices.find(soundData);
    if (it != activeVoices.end()) {
        IXAudio2SourceVoice* voice = it->second;
        if (voice) {
            voice->Stop();
        }
    }
}

void Audio::ResumeAudio()
{
    for (auto& voiceEntry : activeVoices) {
        IXAudio2SourceVoice* voice = voiceEntry.second;
        if (voice) {
            voice->Start();
        }
    }
}

void Audio::ResumeSpecificAudio(SoundData* soundData)
{
    auto it = activeVoices.find(soundData);
    if (it != activeVoices.end()) {
        IXAudio2SourceVoice* voice = it->second;
        if (voice) {
            voice->Start();
        }
    }
}

void Audio::SetPlaybackSpeed(float speed)
{
    // 再生中の全 Voice に同じ周波数比を適用する
    for (auto& voiceEntry : activeVoices) {
        IXAudio2SourceVoice* voice = voiceEntry.second;
        if (voice) {
            HRESULT hr = voice->SetFrequencyRatio(speed);
            assert(SUCCEEDED(hr));
        }
    }
}

void Audio::SetPlaybackSpeed(SoundData* soundData, float speed)
{
    auto it = activeVoices.find(soundData);
    if (it != activeVoices.end()) {
        IXAudio2SourceVoice* voice = it->second;
        if (voice) {
            HRESULT hr = voice->SetFrequencyRatio(speed);
            assert(SUCCEEDED(hr));
        }
    }
}

bool Audio::IsSoundPlaying() const
{
    for (const auto& voiceEntry : activeVoices) {
        IXAudio2SourceVoice* voice = voiceEntry.second;
        if (voice) {
            XAUDIO2_VOICE_STATE state;
            voice->GetState(&state);
            if (state.BuffersQueued > 0) {
                return true;
            }
        }
    }
    return false;
}



bool Audio::IsSoundPlaying(SoundData* soundData) const
{
    auto it = activeVoices.find(soundData);
    if (it != activeVoices.end()) {
        IXAudio2SourceVoice* voice = it->second;
        if (voice) {
            XAUDIO2_VOICE_STATE state;
            voice->GetState(&state);
            return state.BuffersQueued > 0;
        }
    }
    return false;
}

void Audio::SetVolume(float volume)
{
    for (auto& voiceEntry : activeVoices) {
        IXAudio2SourceVoice* voice = voiceEntry.second;
        if (voice) {
            voice->SetVolume(volume);
        }
    }
}

void Audio::SetVolume(SoundData* soundData, float volume)
{
    auto it = activeVoices.find(soundData);
    if (it != activeVoices.end()) {
        IXAudio2SourceVoice* voice = it->second;
        if (voice) {
            voice->SetVolume(volume);
        }
    }
}

}
