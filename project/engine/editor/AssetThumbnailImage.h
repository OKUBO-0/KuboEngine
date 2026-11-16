#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace Engine::Editor {
enum class ThumbnailImageStatus { Ready, Unsupported, Invalid, Stale, Cancelled, AllocationFailure, DecodeFailure };
// Caller owns project/GUID/revision -> serial correlation. No registry pointer,
// GPU owner or live World crosses the decoder thread.
struct ThumbnailImageRequest {
    uint64_t serial=0;
    std::filesystem::path assetRoot,source;
    uint64_t expectedBytes=0;
    int64_t expectedModifiedTicks=0; // AssetRegistry's filesystem clock ticks.
    uint32_t maximumEdge=128;
};
struct ThumbnailImageResult {
    uint64_t serial=0;
    ThumbnailImageStatus status=ThumbnailImageStatus::Invalid;
    uint32_t width=0,height=0;
    // Canonical 8-bit sRGB channels plus an sRGB-tagged PNG for the existing
    // AcquireTextureFromMemory lease path (no new Runtime texture API).
    std::vector<uint8_t> rgba;
    std::vector<uint8_t> png;
    std::array<char,160> diagnostic{};
};
inline constexpr uint64_t ThumbnailMaximumFileBytes=32ull*1024*1024;
inline constexpr size_t ThumbnailMaximumSourcePixels=16ull*1024*1024;
inline constexpr size_t ThumbnailMaximumDecodedBytes=64ull*1024*1024;
inline constexpr size_t ThumbnailMaximumPngBytes=1024*1024;
// Bounded read-only CPU decode. Single 2D images only; no GPU/asset writes.
ThumbnailImageResult DecodeThumbnailImage(const ThumbnailImageRequest&,const std::atomic_bool& cancelled) noexcept;

// One owned CPU worker. Start/Poll/destruction belong to one host thread;
// Cancel alone may be called concurrently. Poll never publishes half a result.
class AssetThumbnailImageJob final {
public:
    AssetThumbnailImageJob()=default;
    ~AssetThumbnailImageJob();
    AssetThumbnailImageJob(const AssetThumbnailImageJob&)=delete;
    AssetThumbnailImageJob& operator=(const AssetThumbnailImageJob&)=delete;
    bool Start(ThumbnailImageRequest request);
    bool Poll(ThumbnailImageResult& output);
    // Host thread only; discard unpublished output and join before shared
    // editor services retire. The object remains reusable afterwards.
    void CancelAndWait();
    void Cancel() noexcept {cancelled_.store(true,std::memory_order_relaxed);}
    bool IsBusy() const noexcept {return busy_.load(std::memory_order_acquire);}
private:
    std::thread worker_;
    mutable std::mutex mutex_;
    std::atomic_bool cancelled_{false},busy_{false};
    ThumbnailImageResult result_;
    bool ready_=false;
};
}


