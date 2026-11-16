#pragma once

#include "HubUpdateFeed.h"
#include "HubEditorSession.h"
#include "EnginePackageInstallation.h"
#include "engine/base/AtomicFile.h"
#include "engine/base/ProjectPreviewPath.h"
#include <fstream>
#include <functional>

namespace KuboHub {

struct UpdateInstallation {
    EnginePackageInstallation::Result result = EnginePackageInstallation::Result::Invalid;
    InstalledPackageReport installed;
    std::filesystem::path staged;
    std::wstring error;
};

inline std::wstring EncodePayloadUrl(std::string_view relative)
{
    std::wstring path;
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    for (unsigned char c : relative) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~' || c == '/') path += wchar_t(c);
        else {
            path += L'%';
            path += hex[c >> 4];
            path += hex[c & 15];
        }
    }
    return path;
}

// A package is fetched as its manifest and declared members. No archive
// extraction or executable is needed until the existing installer has verified it.
using UpdateByteSink = std::function<bool(const char*, DWORD)>;
using UpdateFetch = decltype(&FetchUpdateBytes<UpdateByteSink>);

inline UpdateInstallation DownloadEngineUpdate(UpdateFeedResult release, std::filesystem::path store,
    const std::atomic_bool& cancelled, std::atomic<uint64_t>& progress,
    UpdateFetch fetch)
{
    UpdateInstallation result;
    try {
        using Manifest = Engine::Services::EnginePackageManifest;
        using Updates = Engine::Services::UpdateService;
        UpdateUrl manifestUrl;
        if (release.state != UpdateFeedResult::State::Available ||
            !ParseUpdateUrl(std::wstring(release.manifestUrl.begin(), release.manifestUrl.end()), manifestUrl) ||
            manifestUrl.target.find(L'?') != std::wstring::npos ||
            !manifestUrl.target.ends_with(L"/engine-package.json")) {
            result.error = L"選択した更新の配布先を確認できません。";
            return result;
        }
        std::string bytes;
        if (!fetch(manifestUrl, Manifest::MaxBytes, cancelled, [&](const char* chunk, DWORD size) {
            bytes.append(chunk, size);
            return true;
        }, result.error, 8000)) return result;
        Manifest manifest;
        std::string error;
        if (!Manifest::Decode(bytes, manifest, error) || !manifest.Versioned() ||
            manifest.engineVersion != release.version) {
            result.error = L"配布パッケージの形式か版が更新情報と一致しません。";
            return result;
        }
        auto downloads = store.parent_path() / L"Updates";
        if (!Engine::Base::PlainLocalDirectories(downloads,true)) {
            result.error = L"更新の保存先に通常のローカルフォルダーを指定してください。";
            return result;
        }
        std::filesystem::create_directories(downloads);
        static std::atomic<uint64_t> sequence{0};
        const auto stage = downloads / (L"kubo-update-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence));
        if (!std::filesystem::create_directory(stage)) {
            result.error = L"更新用の作業フォルダーを作成できません。";
            return result;
        }
        result.staged = stage;
        const auto manifestFile = stage / L"engine-package.json";
        std::string digest;
        if (!Engine::Base::AtomicFile::Write(manifestFile,bytes,Manifest::MaxBytes,error,false) ||
            !Updates::ComputeSha256(manifestFile,digest,error) ||
            _stricmp(digest.c_str(),release.manifestHash.c_str()) != 0) {
            result.error = L"パッケージ宣言が更新情報のSHA-256と一致しません。";
            return result;
        }
        const auto prefix = manifestUrl.target.substr(0, manifestUrl.target.find_last_of(L'/')+1);
        const PackageVerificationLimits limits;
        const uint64_t reserve = uint64_t{4} << 30;
        const auto deadline = GetTickCount64() + 20 * 60 * 1000;
        uint64_t total = 0;
        for (const auto& file : manifest.files) {
            if (cancelled.load(std::memory_order_relaxed) || GetTickCount64() >= deadline) {
                result.error = L"更新の取得を中止しました。";
                return result;
            }
            const auto target = stage / Engine::Base::PathFromUtf8(file.path);
            std::filesystem::create_directories(target.parent_path());
            const HANDLE created = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (created == INVALID_HANDLE_VALUE) {
                result.error = L"パッケージの保存先を作成できません。";
                return result;
            }
            UniqueWinHandle output(created);
            auto url = manifestUrl;
            url.target = prefix + EncodePayloadUrl(file.path);
            ULARGE_INTEGER free{};
            uint64_t nextSpaceCheck = total;
            bool diskLow = false;
            const auto now = GetTickCount64();
            const auto remaining = deadline > now ? deadline - now : 0;
            const bool fetched = fetch(url, (std::min)(limits.fileBytes, limits.totalBytes - total),
                cancelled, [&](const char* chunk, DWORD size) {
                    if (total >= nextSpaceCheck) {
                        // Reserve room for the installation copy as well as the
                        // existing managed store's four-GiB free-space floor.
                        diskLow = !GetDiskFreeSpaceExW(stage.c_str(),&free,nullptr,nullptr) ||
                            free.QuadPart < reserve + total + 2ull * 1024 * 1024;
                        if (diskLow) return false;
                        nextSpaceCheck = total + 1024 * 1024;
                    }
                    DWORD written = 0;
                    if (!WriteFile(output.get(),chunk,size,&written,nullptr) || written != size) return false;
                    total += size;
                    progress.store(total,std::memory_order_relaxed);
                    return true;
                }, result.error, (std::min)(remaining,ULONGLONG(120000)));
            const bool flushed = fetched && FlushFileBuffers(output.get());
            output.reset();
            if (diskLow) result.error = L"更新の取得と導入に必要な空き容量がありません。";
            if (!flushed) {
                if (result.error.empty()) result.error = L"更新データを保存できませんでした。";
                return result;
            }
            if (!Updates::ComputeSha256(target,digest,error) || digest != file.sha256) {
                result.error = L"取得したパッケージにチェックサムが一致しないファイルがあります。";
                return result;
            }
        }
        result.result = EnginePackageInstallation::Install(manifestFile,store,result.installed,error,&cancelled);
        if (result.result != EnginePackageInstallation::Result::Installed &&
            result.result != EnginePackageInstallation::Result::AlreadyPresent)
            result.error = L"検証または導入に失敗しました。 " + Engine::Base::PathFromUtf8(error).wstring();
    } catch (...) {
        result.error = L"更新を取得または導入できませんでした。";
    }
    return result;
}

} // namespace KuboHub
