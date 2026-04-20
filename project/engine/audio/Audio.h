#pragma once
#include <xaudio2.h>
#pragma comment(lib,"xaudio2.lib")
#include <wrl.h>
#include <fstream>
#include <cassert>
#include <unordered_map>
#include <vector>

namespace Engine::AudioSystem {

struct ChunkHeader {
    char id[4];
    int32_t size;
};

struct RiffHeader {
    ChunkHeader chunk;
    char type[4];
};

struct FormatChunk {
    ChunkHeader chunk;
    WAVEFORMATEX fmt;
};

struct SoundData {
    WAVEFORMATEX wfex; // 波形フォーマット
    std::vector<BYTE> buffer; // 波形データ本体
    unsigned int bufferSize; // バッファのサイズ
};

/// @brief XAudio2 を用いた音声再生を管理するクラス
/// @details Wave ファイルの読込、SourceVoice の生成、再生状態や音量の制御を担当する。
class Audio {
    Audio() = default;
    ~Audio() = default;
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

public:
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    /// @brief シングルトンインスタンスを取得する
    /// @param なし
    /// @return Audio のインスタンス
    static Audio* GetInstance();
    /// @brief オーディオデバイスとマスターボイスを初期化する
    /// @param なし
    /// @return なし
    void Initialize();
    /// @brief 保持中の音声再生資源を解放する
    /// @param なし
    /// @return なし
    void Finalize();

    /// @brief Wave ファイルを読み込んで再生用データを生成する
    /// @param filename 読み込む wave ファイルパス
    /// @return 読み込んだ音声データ
    SoundData SoundLoadWave(const char* filename);
    /// @brief 読み込み済み音声データを解放する
    /// @param soundData 解放対象の音声データ
    /// @return なし
    void SoundUnload(SoundData* soundData);

    /// @brief 音声データを再生する
    /// @param soundData 再生する音声データ
    /// @return なし
    void SoundPlayWave(const SoundData& soundData);
    /// @brief すべての音声再生を停止する
    /// @param なし
    /// @return なし
    void StopAudio(); // 全音停止
    /// @brief 指定した音声再生を停止する
    /// @param soundData 停止対象の音声データ
    /// @return なし
    void StopSpecificAudio(SoundData* soundData); // 指定音停止

    /// @brief すべての音声再生を一時停止する
    /// @param なし
    /// @return なし
    void PauseAudio(); // 全音一時停止
    /// @brief 指定した音声再生を一時停止する
    /// @param soundData 一時停止対象の音声データ
    /// @return なし
    void PauseSpecificAudio(SoundData* soundData); // 指定音一時停止
    /// @brief 一時停止中の音声再生をすべて再開する
    /// @param なし
    /// @return なし
    void ResumeAudio(); // 全音再開
    /// @brief 指定した音声再生を再開する
    /// @param soundData 再開対象の音声データ
    /// @return なし
    void ResumeSpecificAudio(SoundData* soundData); // 指定音再開

    /// @brief すべての音声再生速度を変更する
    /// @param speed 設定する再生速度
    /// @return なし
    void SetPlaybackSpeed(float speed); // 全音の速度変更
    /// @brief 指定した音声の再生速度を変更する
    /// @param soundData 対象の音声データ
    /// @param speed 設定する再生速度
    /// @return なし
    void SetPlaybackSpeed(SoundData* soundData, float speed); // 指定音の速度変更

    /// @brief すべての音量を変更する
    /// @param volume 設定する音量
    /// @return なし
    void SetVolume(float volume); // 全ての音の音量変更
    /// @brief 指定した音声の音量を変更する
    /// @param soundData 対象の音声データ
    /// @param volume 設定する音量
    /// @return なし
    void SetVolume(SoundData* soundData, float volume); // 指定音の音量変更

    /// @brief 何らかの音声が再生中かを判定する
    /// @param なし
    /// @return 再生中の音声があれば true
    bool IsSoundPlaying() const; // 何か音が鳴っているか
    /// @brief 指定した音声が再生中かを判定する
    /// @param soundData 判定対象の音声データ
    /// @return 再生中なら true
    bool IsSoundPlaying(SoundData* soundData) const; // 指定音が鳴っているか

private:
    ComPtr<IXAudio2> xAudio2 = nullptr;
    IXAudio2MasteringVoice* masterVoice = nullptr;

    // 複数の SourceVoice を管理
    std::unordered_map<SoundData*, IXAudio2SourceVoice*> activeVoices;
};

}
