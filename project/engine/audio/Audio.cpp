#include "Audio.h"
#include <Windows.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <wrl.h>

namespace Engine::AudioSystem {

namespace {
constexpr char kRiffChunkId[] = "RIFF";
constexpr char kWaveChunkId[] = "WAVE";
constexpr char kFormatChunkId[] = "fmt ";
constexpr char kDataChunkId[] = "data";
constexpr size_t kChunkIdLength = 4;

std::string ChunkIdToString(const char* id)
{
    return std::string(id, id + kChunkIdLength);
}

void FailWaveLoad(const std::string& filename, const std::string& reason)
{
    std::ostringstream message;
    message << "[Audio::SoundLoadWave] " << reason
        << " filePath=\"" << filename << "\"\n";
    OutputDebugStringA(message.str().c_str());
    assert(false && "Audio::SoundLoadWave failed; see OutputDebugString for file path and chunk details");
    std::abort();
}

RiffHeader ReadRiffHeader(std::ifstream& file, const std::string& filename)
{
    RiffHeader riff{};
    file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
    if (!file) {
        FailWaveLoad(filename, "failed to read RIFF header");
    }
    if (strncmp(riff.chunk.id, kRiffChunkId, kChunkIdLength) != 0) {
        FailWaveLoad(filename, "invalid RIFF chunk id: actual=\"" + ChunkIdToString(riff.chunk.id) + "\" expected=\"RIFF\"");
    }
    if (strncmp(riff.type, kWaveChunkId, kChunkIdLength) != 0) {
        FailWaveLoad(filename, "invalid WAVE type id: actual=\"" + ChunkIdToString(riff.type) + "\" expected=\"WAVE\"");
    }
    return riff;
}

void SkipChunk(std::ifstream& file, int32_t size)
{
    file.seekg(size, std::ios_base::cur);
    if ((size & 1) != 0) {
        file.seekg(1, std::ios_base::cur);
    }
}

FormatChunk ReadFormatChunkPayload(std::ifstream& file, const ChunkHeader& header)
{
    FormatChunk format{};
    format.chunk = header;
    const std::streamsize readSize = (std::min)(
        static_cast<std::streamsize>(header.size),
        static_cast<std::streamsize>(sizeof(format.fmt)));
    file.read(reinterpret_cast<char*>(&format.fmt), readSize);
    if (header.size > readSize) {
        SkipChunk(file, header.size - static_cast<int32_t>(readSize));
    } else if ((header.size & 1) != 0) {
        file.seekg(1, std::ios_base::cur);
    }
    return format;
}

FormatChunk FindFormatChunk(std::ifstream& file, const std::string& filename)
{
    ChunkHeader chunk{};
    while (file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk))) {
        if (strncmp(chunk.id, kFormatChunkId, kChunkIdLength) == 0) {
            return ReadFormatChunkPayload(file, chunk);
        }
        SkipChunk(file, chunk.size);
    }

    FailWaveLoad(filename, "fmt chunk not found: expected chunk id=\"fmt \"");
    return {};
}

ChunkHeader ReadDataChunk(std::ifstream& file, const std::string& filename)
{
    ChunkHeader data{};
    while (file.read(reinterpret_cast<char*>(&data), sizeof(data))) {
        if (strncmp(data.id, kDataChunkId, kChunkIdLength) == 0) {
            return data;
        }
        SkipChunk(file, data.size);
    }

    FailWaveLoad(filename, "data chunk not found: expected chunk id=\"data\"");
    return {};
}

SoundData ReadSoundData(std::ifstream& file, const FormatChunk& format, const ChunkHeader& data, const std::string& filename)
{
    SoundData soundData{};
    soundData.buffer.resize(data.size);
    file.read(reinterpret_cast<char*>(soundData.buffer.data()), data.size);
    if (!file) {
        FailWaveLoad(filename, "failed to read data chunk payload: chunk id=\"" + ChunkIdToString(data.id) +
            "\" size=" + std::to_string(data.size));
    }
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
    const std::string path = filename != nullptr ? filename : "<null>";
    if (filename == nullptr) {
        FailWaveLoad(path, "filename is null");
    }

    std::ifstream file(filename, std::ios_base::binary);
    if (!file.is_open()) {
        FailWaveLoad(path, "failed to open wave file");
    }

    // RIFF 内のチャンクは fmt/data の間に JUNK/LIST/fact などが入ることがあるため、ID で探す
    ReadRiffHeader(file, path);
    FormatChunk format = FindFormatChunk(file, path);
    ChunkHeader data = ReadDataChunk(file, path);
    SoundData soundData = ReadSoundData(file, format, data, path);
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

void Audio::SoundPlayWave(const SoundData& soundData, bool loop)
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
    buf.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

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
