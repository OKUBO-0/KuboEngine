#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "AssetThumbnailImage.h"
#include "externals/DirectXTex/DirectXTex.h"
#include <Windows.h>
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace Engine::Editor {
namespace {
namespace fs=std::filesystem;
struct Refusal {ThumbnailImageStatus status;const char* diagnostic;};
void Need(bool condition,ThumbnailImageStatus status,const char* text){if(!condition)throw Refusal{status,text};}
void CheckCancel(const std::atomic_bool& stop){Need(!stop.load(std::memory_order_relaxed),ThumbnailImageStatus::Cancelled,"Thumbnail cancelled");}
void Diagnostic(ThumbnailImageResult& result,const char* text) noexcept {
    const auto count=(std::min)(std::strlen(text),result.diagnostic.size()-1);std::memcpy(result.diagnostic.data(),text,count);result.diagnostic[count]='\0';
}
bool SamePart(const fs::path& a,const fs::path& b){return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_EQUAL;}
fs::path Root(const fs::path& input){auto result=input.lexically_normal();while(result!=result.root_path()&&result.filename().empty())result=result.parent_path();return result;}
bool Inside(const fs::path& source,const fs::path& root){auto part=source.begin();for(const auto& parent:root){if(part==source.end()||!SamePart(*part,parent))return false;++part;}return part!=source.end();}
void Ordinary(const fs::path& source,const fs::path& root){
    const auto check=[](const fs::path& path){const auto attributes=GetFileAttributesW(path.c_str());Need(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),ThumbnailImageStatus::Invalid,"Thumbnail source is missing or linked");};
    check(root);auto cursor=root;for(const auto& component:source.lexically_relative(root)){cursor/=component;check(cursor);}
}
struct File {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    File()=default;File(const File&)=delete;File& operator=(const File&)=delete;
};
void Open(File& file,const fs::path& path){
    file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    Need(file.value!=INVALID_HANDLE_VALUE,ThumbnailImageStatus::Invalid,"Cannot read thumbnail source");
}
BY_HANDLE_FILE_INFORMATION Identity(HANDLE handle){
    BY_HANDLE_FILE_INFORMATION info{};Need(GetFileType(handle)==FILE_TYPE_DISK&&GetFileInformationByHandle(handle,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&info.nNumberOfLinks==1,ThumbnailImageStatus::Invalid,"Thumbnail source must be an ordinary single-link file");return info;
}
bool SameFile(const BY_HANDLE_FILE_INFORMATION& a,const BY_HANDLE_FILE_INFORMATION& b){return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow;}
void Current(const ThumbnailImageRequest& request,HANDLE handle,const BY_HANDLE_FILE_INFORMATION& original){
    LARGE_INTEGER size{};Need(GetFileSizeEx(handle,&size)&&size.QuadPart>=0&&uint64_t(size.QuadPart)==request.expectedBytes,ThumbnailImageStatus::Stale,"Thumbnail source size changed");
    std::error_code error;const auto stamp=fs::last_write_time(request.source,error);Need(!error&&stamp.time_since_epoch().count()==request.expectedModifiedTicks,ThumbnailImageStatus::Stale,"Thumbnail source revision changed");
    Ordinary(request.source.lexically_normal(),Root(request.assetRoot));File fresh;Open(fresh,request.source);Need(SameFile(original,Identity(fresh.value)),ThumbnailImageStatus::Stale,"Thumbnail source identity changed");
}
enum class Decoder {Wic,Dds,Tga,Hdr};
Decoder Kind(const fs::path& path){auto extension=path.extension().native();for(auto& c:extension)if(c>=L'A'&&c<=L'Z')c=static_cast<wchar_t>(c+(L'a'-L'A'));
    if(extension==L".dds")return Decoder::Dds;if(extension==L".tga")return Decoder::Tga;if(extension==L".hdr")return Decoder::Hdr;
    for(const auto* supported:{L".png",L".jpg",L".jpeg",L".bmp",L".tif",L".tiff",L".gif",L".webp"})if(extension==supported)return Decoder::Wic;
    throw Refusal{ThumbnailImageStatus::Unsupported,"No thumbnail decoder for this file type"};
}
void CheckMetadata(const DirectX::TexMetadata& metadata){
    Need(metadata.dimension==DirectX::TEX_DIMENSION_TEXTURE2D&&metadata.arraySize==1&&metadata.depth==1&&!metadata.IsCubemap(),ThumbnailImageStatus::Unsupported,"Thumbnail requires one 2D image");
    Need(metadata.width&&metadata.height&&metadata.width<=8192&&metadata.height<=8192&&metadata.width<=ThumbnailMaximumSourcePixels/metadata.height&&metadata.mipLevels&&metadata.mipLevels<=16,ThumbnailImageStatus::Invalid,"Thumbnail source dimensions exceed limits");
    size_t width=metadata.width,height=metadata.height,total=0;
    for(size_t level=0;level<metadata.mipLevels;++level){size_t row=0,slice=0;Need(SUCCEEDED(DirectX::ComputePitch(metadata.format,width,height,row,slice))&&slice<=ThumbnailMaximumDecodedBytes-total,ThumbnailImageStatus::Invalid,"Thumbnail source allocation exceeds limit");total+=slice;width=(std::max)(size_t(1),width/2);height=(std::max)(size_t(1),height/2);}
}
void ScratchRoom(size_t held,size_t width,size_t height,size_t bytesPerPixel){
    Need(held<=ThumbnailMaximumDecodedBytes&&width&&height&&bytesPerPixel&&
        width<=(ThumbnailMaximumDecodedBytes-held)/bytesPerPixel/height,
        ThumbnailImageStatus::Invalid,"Thumbnail intermediate allocation exceeds limit");
}
struct Com {
    HRESULT status=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ~Com(){if(SUCCEEDED(status))CoUninitialize();}
};
}
ThumbnailImageResult DecodeThumbnailImage(const ThumbnailImageRequest& request,const std::atomic_bool& cancelled) noexcept {
    ThumbnailImageResult result;result.serial=request.serial;
    try {
        CheckCancel(cancelled);Need(request.serial&&request.maximumEdge&&request.maximumEdge<=256&&request.expectedBytes&&request.expectedBytes<=ThumbnailMaximumFileBytes,ThumbnailImageStatus::Invalid,"Invalid thumbnail request or file size");
        Need(request.source.is_absolute()&&request.assetRoot.is_absolute()&&request.source.native().size()<32760&&request.assetRoot.native().size()<32760&&request.source.native().find(L'\0')==std::wstring::npos&&request.assetRoot.native().find(L'\0')==std::wstring::npos,ThumbnailImageStatus::Invalid,"Thumbnail requires bounded absolute paths");
        const auto source=request.source.lexically_normal(),root=Root(request.assetRoot);Need(Inside(source,root),ThumbnailImageStatus::Invalid,"Thumbnail source escapes asset root");Ordinary(source,root);
        const auto decoder=Kind(source);Com com;Need(SUCCEEDED(com.status)||com.status==RPC_E_CHANGED_MODE,ThumbnailImageStatus::DecodeFailure,"Thumbnail COM initialization failed");
        File file;Open(file,source);const auto identity=Identity(file.value);Current(request,file.value,identity);
        std::vector<uint8_t> bytes(static_cast<size_t>(request.expectedBytes));size_t offset=0;
        while(offset<bytes.size()){CheckCancel(cancelled);DWORD count=0;const auto chunk=static_cast<DWORD>((std::min)(size_t(1024*1024),bytes.size()-offset));Need(ReadFile(file.value,bytes.data()+offset,chunk,&count,nullptr)&&count==chunk,ThumbnailImageStatus::Stale,"Thumbnail source read changed or failed");offset+=count;}
        char extra=0;DWORD count=0;Need(ReadFile(file.value,&extra,1,&count,nullptr)&&!count,ThumbnailImageStatus::Stale,"Thumbnail source grew while reading");CheckCancel(cancelled);
        DirectX::TexMetadata metadata{};HRESULT hr=E_FAIL;
        switch(decoder){case Decoder::Dds:hr=DirectX::GetMetadataFromDDSMemory(bytes.data(),bytes.size(),DirectX::DDS_FLAGS_NONE,metadata);break;case Decoder::Tga:hr=DirectX::GetMetadataFromTGAMemory(bytes.data(),bytes.size(),DirectX::TGA_FLAGS_NONE,metadata);break;case Decoder::Hdr:hr=DirectX::GetMetadataFromHDRMemory(bytes.data(),bytes.size(),metadata);break;default:hr=DirectX::GetMetadataFromWICMemory(bytes.data(),bytes.size(),DirectX::WIC_FLAGS_NONE,metadata);break;}
        Need(SUCCEEDED(hr),ThumbnailImageStatus::DecodeFailure,"Thumbnail image metadata is invalid");CheckMetadata(metadata);CheckCancel(cancelled);
        DirectX::ScratchImage loaded;
        switch(decoder){case Decoder::Dds:hr=DirectX::LoadFromDDSMemory(bytes.data(),bytes.size(),DirectX::DDS_FLAGS_NONE,nullptr,loaded);break;case Decoder::Tga:hr=DirectX::LoadFromTGAMemory(bytes.data(),bytes.size(),DirectX::TGA_FLAGS_NONE,nullptr,loaded);break;case Decoder::Hdr:hr=DirectX::LoadFromHDRMemory(bytes.data(),bytes.size(),nullptr,loaded);break;default:hr=DirectX::LoadFromWICMemory(bytes.data(),bytes.size(),DirectX::WIC_FLAGS_NONE,nullptr,loaded);break;}
        Need(SUCCEEDED(hr)&&loaded.GetPixelsSize()<=ThumbnailMaximumDecodedBytes,ThumbnailImageStatus::DecodeFailure,"Thumbnail image decode failed");CheckCancel(cancelled);
        const DirectX::Image* image=loaded.GetImage(0,0,0);Need(image&&image->pixels&&image->width==metadata.width&&image->height==metadata.height,ThumbnailImageStatus::DecodeFailure,"Thumbnail decoded image shape changed");
        DirectX::ScratchImage decompressed,converted;
        if(DirectX::IsCompressed(image->format)){ScratchRoom(loaded.GetPixelsSize(),image->width,image->height,16);hr=DirectX::Decompress(*image,DXGI_FORMAT_UNKNOWN,decompressed);Need(SUCCEEDED(hr),ThumbnailImageStatus::DecodeFailure,"Thumbnail block decompression failed");image=decompressed.GetImage(0,0,0);}
        Need(image&&image->pixels,ThumbnailImageStatus::DecodeFailure,"Thumbnail decompression has no pixels");
        if(image->format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB){ScratchRoom(loaded.GetPixelsSize()+decompressed.GetPixelsSize(),image->width,image->height,4);hr=DirectX::Convert(*image,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DirectX::TEX_FILTER_DEFAULT,DirectX::TEX_THRESHOLD_DEFAULT,converted);Need(SUCCEEDED(hr),ThumbnailImageStatus::DecodeFailure,"Thumbnail sRGB conversion failed");image=converted.GetImage(0,0,0);loaded.Release();decompressed.Release();}
        Need(image&&image->pixels,ThumbnailImageStatus::DecodeFailure,"Thumbnail has no RGBA pixels");CheckCancel(cancelled);
        const auto scale=(std::min)({1.0,double(request.maximumEdge)/double(image->width),double(request.maximumEdge)/double(image->height)});
        const auto width=static_cast<size_t>((std::max)(1.0,std::floor(double(image->width)*scale+.5))),height=static_cast<size_t>((std::max)(1.0,std::floor(double(image->height)*scale+.5)));
        DirectX::ScratchImage resized;if(width!=image->width||height!=image->height){ScratchRoom(loaded.GetPixelsSize()+decompressed.GetPixelsSize()+converted.GetPixelsSize(),width,height,4);Need(SUCCEEDED(DirectX::Resize(*image,width,height,DirectX::TEX_FILTER_DEFAULT,resized)),ThumbnailImageStatus::DecodeFailure,"Thumbnail resize failed");image=resized.GetImage(0,0,0);loaded.Release();decompressed.Release();converted.Release();}
        Need(image&&image->pixels&&width<=request.maximumEdge&&height<=request.maximumEdge&&image->rowPitch>=width*4,ThumbnailImageStatus::DecodeFailure,"Thumbnail output extent is invalid");
        ScratchRoom(loaded.GetPixelsSize()+decompressed.GetPixelsSize()+converted.GetPixelsSize()+resized.GetPixelsSize()+ThumbnailMaximumPngBytes,width,height,4);
        std::vector<uint8_t> pixels(width*height*4);for(size_t y=0;y<height;++y)std::memcpy(pixels.data()+y*width*4,image->pixels+y*image->rowPitch,width*4);
        DirectX::Blob blob;Need(SUCCEEDED(DirectX::SaveToWICMemory(*image,DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),blob))&&blob.GetBufferSize()&&blob.GetBufferSize()<=ThumbnailMaximumPngBytes,ThumbnailImageStatus::DecodeFailure,"Thumbnail PNG encoding failed");
        const auto* encoded=static_cast<const uint8_t*>(blob.GetBufferPointer());std::vector<uint8_t> png(encoded,encoded+blob.GetBufferSize());
        Current(request,file.value,identity);CheckCancel(cancelled);result.width=static_cast<uint32_t>(width);result.height=static_cast<uint32_t>(height);result.rgba=std::move(pixels);result.png=std::move(png);result.status=ThumbnailImageStatus::Ready;
    }catch(const Refusal& error){result.status=error.status;Diagnostic(result,error.diagnostic);}
    catch(const std::bad_alloc&){result.status=ThumbnailImageStatus::AllocationFailure;Diagnostic(result,"Thumbnail allocation failed");}
    catch(...){result.status=ThumbnailImageStatus::DecodeFailure;Diagnostic(result,"Thumbnail decode refused");}
    return result;
}
AssetThumbnailImageJob::~AssetThumbnailImageJob(){Cancel();if(worker_.joinable())worker_.join();}
void AssetThumbnailImageJob::CancelAndWait(){
    Cancel();if(worker_.joinable())worker_.join();
    std::lock_guard lock(mutex_);result_={};ready_=false;busy_.store(false,std::memory_order_release);
}
bool AssetThumbnailImageJob::Start(ThumbnailImageRequest request){
    std::lock_guard lock(mutex_);if(worker_.joinable())return false;cancelled_.store(false,std::memory_order_relaxed);ready_=false;busy_.store(true,std::memory_order_release);
    try {worker_=std::thread([this,request=std::move(request)]{
        auto result=DecodeThumbnailImage(request,cancelled_);if(cancelled_.load(std::memory_order_relaxed)){result.status=ThumbnailImageStatus::Cancelled;result.width=result.height=0;result.rgba.clear();result.png.clear();Diagnostic(result,"Thumbnail cancelled");}
        {std::lock_guard finished(mutex_);static_assert(std::is_nothrow_move_assignable_v<ThumbnailImageResult>);result_=std::move(result);ready_=true;}busy_.store(false,std::memory_order_release);
    });return true;}catch(...){busy_.store(false,std::memory_order_release);return false;}
}
bool AssetThumbnailImageJob::Poll(ThumbnailImageResult& output){
    std::thread finished;
    {std::lock_guard lock(mutex_);if(!ready_)return false;
        // Cancellation on the owning thread before Poll also discards a result
        // which finished just before Cancel. It has not been published yet.
        if(cancelled_.load(std::memory_order_relaxed)){result_.status=ThumbnailImageStatus::Cancelled;result_.width=result_.height=0;result_.rgba.clear();result_.png.clear();Diagnostic(result_,"Thumbnail cancelled");}
        output=std::move(result_);ready_=false;finished=std::move(worker_);}
    if(finished.joinable())finished.join();return true;
}
}


