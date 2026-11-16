#pragma once

#include "HubEditorSession.h"
#include "engine/base/AtomicFile.h"
#include "engine/base/ProjectPreviewPath.h"
#include <wincodec.h>
#include <wrl.h>
#include <atomic>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <future>

#pragma comment(lib, "Windowscodecs.lib")

namespace KuboHub {

struct ProjectPreview {
    std::wstring project;
    FILETIME modified{};
    DWORD bytes = 0;
    std::shared_ptr<const std::vector<uint8_t>> bgra;
    std::wstring lastOpened;
};

inline void RecordProjectOpened(const std::filesystem::path& project)
{
    const auto root = project.parent_path();
    if (!Engine::Base::PreviewDirectoriesAllowed(root, true)) return;
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    const ULONGLONG ticks = (ULONGLONG(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    std::string error;
    Engine::Base::AtomicFile::Write(root / L"UserSettings/Hub/last_opened.txt",
        std::to_string(ticks), 24, error);
}

inline std::wstring ReadLastOpened(const std::filesystem::path& root)
{
    std::ifstream input(root / L"UserSettings/Hub/last_opened.txt", std::ios::binary);
    char bytes[24]{};
    input.read(bytes, sizeof(bytes));
    if (!input.eof() || input.bad() || !input.gcount()) return {};
    ULONGLONG ticks = 0;
    const auto end = bytes + input.gcount();
    const auto number = std::from_chars(bytes, end, ticks);
    if (number.ec != std::errc{} || number.ptr != end || !ticks) return {};
    FILETIME utc{DWORD(ticks), DWORD(ticks >> 32)}, local{};
    SYSTEMTIME date{};
    if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &date)) return {};
    wchar_t label[32]{};
    swprintf_s(label, L"%04u/%02u/%02u %02u:%02u", unsigned(date.wYear), unsigned(date.wMonth),
        unsigned(date.wDay), unsigned(date.wHour), unsigned(date.wMinute));
    return label;
}

inline constexpr UINT PreviewWidth = 320;
inline constexpr UINT PreviewHeight = 180;

inline std::shared_ptr<const std::vector<uint8_t>> DecodeProjectPreview(
    IWICImagingFactory& factory, std::vector<uint8_t>& png)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    UINT count = 0, width = 0, height = 0;
    GUID format{};
    if (FAILED(factory.CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(png.data(), static_cast<DWORD>(png.size()))) ||
        FAILED(factory.CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetContainerFormat(&format)) || !IsEqualGUID(format, GUID_ContainerFormatPng) ||
        FAILED(decoder->GetFrameCount(&count)) || count != 1 ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&width, &height)) ||
        width != Engine::Base::ProjectPreviewWidth || height != Engine::Base::ProjectPreviewHeight) return {};
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converted;
    if (FAILED(factory.CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(frame.Get(), PreviewWidth, PreviewHeight, WICBitmapInterpolationModeFant)) ||
        FAILED(factory.CreateFormatConverter(&converted)) ||
        FAILED(converted->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    auto pixels = std::make_shared<std::vector<uint8_t>>(PreviewWidth * PreviewHeight * 4);
    if (FAILED(converted->CopyPixels(nullptr, PreviewWidth * 4,
        static_cast<UINT>(pixels->size()), pixels->data()))) return {};
    return pixels;
}

inline std::vector<ProjectPreview> ScanProjectPreviews(const std::vector<std::wstring>& projects,
    const std::vector<ProjectPreview>& previous, const std::atomic_bool& cancelled)
{
    std::vector<ProjectPreview> result;
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return result;
    struct Uninitialize { ~Uninitialize() { CoUninitialize(); } } com;
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)))) return result;
    for (const auto& project : projects) {
        if (cancelled.load(std::memory_order_relaxed)) break;
        ProjectPreview item;
        item.project = project;
        const auto root = std::filesystem::path(project).parent_path();
        if (Engine::Base::PreviewDirectoriesAllowed(root, false)) {
            item.lastOpened = ReadLastOpened(root);
            const auto file = Engine::Base::ProjectPreviewPath(root);
            const HANDLE opened = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (opened != INVALID_HANDLE_VALUE) {
                UniqueWinHandle handle(opened);
                BY_HANDLE_FILE_INFORMATION info{};
                if (GetFileInformationByHandle(handle.get(), &info) &&
                    !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
                    !info.nFileSizeHigh && info.nFileSizeLow &&
                    info.nFileSizeLow <= Engine::Base::ProjectPreviewMaxBytes) {
                    item.bytes = info.nFileSizeLow;
                    item.modified = info.ftLastWriteTime;
                    const auto old = std::find_if(previous.begin(), previous.end(), [&](const auto& entry) {
                        return entry.project == item.project && entry.bytes == item.bytes &&
                            CompareFileTime(&entry.modified, &item.modified) == 0;
                    });
                    if (old != previous.end()) item.bgra = old->bgra;
                    else {
                        std::vector<uint8_t> png(item.bytes);
                        DWORD read = 0;
                        if (ReadFile(handle.get(), png.data(), item.bytes, &read, nullptr) && read == item.bytes)
                            item.bgra = DecodeProjectPreview(*factory.Get(), png);
                    }
                }
            }
        }
        result.push_back(std::move(item));
    }
    return result;
}

} // namespace KuboHub
