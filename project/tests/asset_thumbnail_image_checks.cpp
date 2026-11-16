#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "engine/editor/AssetThumbnailImage.h"
#include "externals/DirectXTex/DirectXTex.h"
#include <Windows.h>
#include <objbase.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
namespace fs=std::filesystem;using namespace Engine::Editor;
void Need(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct ComScope {HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~ComScope(){if(SUCCEEDED(hr))CoUninitialize();}};
std::string Read(const fs::path& file){std::ifstream in(file,std::ios::binary);Need(in.good(),"Thumbnail test read");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void Write(const fs::path& file,const std::string& data){std::ofstream out(file,std::ios::binary);out<<data;out.close();Need(!out.fail(),"Thumbnail test write");}
ThumbnailImageRequest Request(const fs::path& root,const fs::path& file,uint64_t serial=1){ThumbnailImageRequest value;value.serial=serial;value.assetRoot=root;value.source=file;value.expectedBytes=fs::file_size(file);value.expectedModifiedTicks=fs::last_write_time(file).time_since_epoch().count();value.maximumEdge=8;return value;}
ThumbnailImageResult Wait(AssetThumbnailImageJob& job){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);ThumbnailImageResult value;while(!job.Poll(value)){Need(std::chrono::steady_clock::now()<deadline,"Thumbnail worker timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}Need(!job.IsBusy(),"Thumbnail Poll left worker busy");return value;}
}
int main(){fs::path root;try{
    ComScope com;Need(SUCCEEDED(com.hr)||com.hr==RPC_E_CHANGED_MODE,"Thumbnail test COM");
    root=fs::temp_directory_path()/(L"kubo-thumbnail-images-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(fs::create_directory(root),"Refuse existing thumbnail fixture");fs::create_directory(root/"Assets");fs::create_directory(root/"Other");const auto file=root/L"Assets/画像.DDS";
    DirectX::ScratchImage image;Need(SUCCEEDED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM,16,8,1,1)),"Thumbnail source allocation");const auto* source=image.GetImage(0,0,0);Need(source&&source->pixels,"Thumbnail source pixels");
    for(size_t y=0;y<8;++y)for(size_t x=0;x<16;++x){auto* pixel=source->pixels+y*source->rowPitch+x*4;pixel[0]=x<4?128:x<8?255:0;pixel[1]=x<4?64:x<8?0:255;pixel[2]=x<4?32:0;pixel[3]=x<8?255:128;}
    Need(SUCCEEDED(DirectX::SaveToDDSFile(*source,DirectX::DDS_FLAGS_NONE,file.c_str())),"Thumbnail source save");const auto authored=Read(file);auto request=Request(root/"Assets",file);std::atomic_bool cancelled{false};auto result=DecodeThumbnailImage(request,cancelled);
    Need(result.status==ThumbnailImageStatus::Ready&&result.serial==1&&result.width==8&&result.height==4&&result.rgba.size()==128&&!result.png.empty()&&result.png.size()<=ThumbnailMaximumPngBytes,"Thumbnail DDS aspect/extent/PNG");
    const size_t grey=(2*8)*4,red=(2*8+2)*4,green=(2*8+6)*4;Need(result.rgba[red]>250&&result.rgba[red+1]<5&&result.rgba[red+3]>250&&result.rgba[green]<5&&result.rgba[green+1]>250&&result.rgba[green+3]>=127&&result.rgba[green+3]<=129,"Thumbnail DDS color/alpha lost");
    Need(std::abs(int(result.rgba[grey])-188)<=2&&std::abs(int(result.rgba[grey+1])-137)<=2&&std::abs(int(result.rgba[grey+2])-99)<=2,"Linear DDS did not convert to display sRGB");const auto expected=result.rgba;
    DirectX::TexMetadata pngMetadata{};Need(SUCCEEDED(DirectX::GetMetadataFromWICMemory(result.png.data(),result.png.size(),DirectX::WIC_FLAGS_NONE,pngMetadata))&&DirectX::IsSRGB(pngMetadata.format),"Thumbnail output PNG lost sRGB metadata");
    const auto pngFile=root/L"Assets/写真.png";auto srgbSource=*source;srgbSource.format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;Need(SUCCEEDED(DirectX::SaveToWICFile(srgbSource,DirectX::WIC_FLAGS_NONE,DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),pngFile.c_str())),"Thumbnail PNG source save");auto photo=DecodeThumbnailImage(Request(root/"Assets",pngFile,10),cancelled);
    Need(photo.status==ThumbnailImageStatus::Ready&&photo.width==8&&photo.height==4&&std::abs(int(photo.rgba[grey])-128)<=2&&std::abs(int(photo.rgba[grey+1])-64)<=2&&std::abs(int(photo.rgba[grey+2])-32)<=2&&photo.rgba[green+3]>=127&&photo.rgba[green+3]<=129,"sRGB PNG display color/alpha changed");
    auto unchanged=request;unchanged.maximumEdge=128;result=DecodeThumbnailImage(unchanged,cancelled);Need(result.status==ThumbnailImageStatus::Ready&&result.width==16&&result.height==8,"Thumbnail unexpectedly upscaled source");auto trailing=request;trailing.assetRoot/=L"";result=DecodeThumbnailImage(trailing,cancelled);Need(result.status==ThumbnailImageStatus::Ready&&result.rgba==expected,"Thumbnail rejected trailing asset-root separator");
    const auto refuse=[&](ThumbnailImageRequest input,ThumbnailImageStatus status){const auto failed=DecodeThumbnailImage(input,cancelled);Need(failed.status==status&&!failed.width&&!failed.height&&failed.rgba.empty()&&failed.png.empty()&&failed.serial==input.serial,"Thumbnail refusal published pixels/wrong status");};
    auto invalid=request;invalid.serial=0;refuse(invalid,ThumbnailImageStatus::Invalid);invalid=request;invalid.maximumEdge=0;refuse(invalid,ThumbnailImageStatus::Invalid);invalid=request;invalid.maximumEdge=257;refuse(invalid,ThumbnailImageStatus::Invalid);invalid=request;invalid.expectedBytes=ThumbnailMaximumFileBytes+1;refuse(invalid,ThumbnailImageStatus::Invalid);
    invalid=request;++invalid.expectedBytes;refuse(invalid,ThumbnailImageStatus::Stale);invalid=request;invalid.expectedModifiedTicks^=1;refuse(invalid,ThumbnailImageStatus::Stale);invalid=request;invalid.assetRoot=root/"Other";refuse(invalid,ThumbnailImageStatus::Invalid);cancelled.store(true);refuse(request,ThumbnailImageStatus::Cancelled);cancelled.store(false);
    const auto unknown=root/"Assets/plain.txt";Write(unknown,authored);refuse(Request(root/"Assets",unknown),ThumbnailImageStatus::Unsupported);
    const auto corrupt=root/"Assets/corrupt.png";Write(corrupt,"not an image");refuse(Request(root/"Assets",corrupt),ThumbnailImageStatus::DecodeFailure);
    const auto malicious=root/"Assets/oversized.dds";auto header=authored;Need(header.size()>20,"Short DDS fixture");for(size_t i=12;i<20;++i)header[i]=char(0x7f);Write(malicious,header);result=DecodeThumbnailImage(Request(root/"Assets",malicious),cancelled);Need(result.status!=ThumbnailImageStatus::Ready&&result.rgba.empty(),"Oversized DDS admitted");
    DirectX::ScratchImage compressed;Need(SUCCEEDED(compressed.Initialize2D(DXGI_FORMAT_BC1_UNORM,4096,4096,1,1)),"Compressed thumbnail budget fixture allocation");std::fill_n(compressed.GetPixels(),compressed.GetPixelsSize(),static_cast<unsigned char>(0));const auto compressedFile=root/"Assets/intermediate-budget.dds";Need(SUCCEEDED(DirectX::SaveToDDSFile(compressed.GetImages(),compressed.GetImageCount(),compressed.GetMetadata(),DirectX::DDS_FLAGS_NONE,compressedFile.c_str())),"Compressed thumbnail budget fixture save");compressed.Release();refuse(Request(root/"Assets",compressedFile),ThumbnailImageStatus::Invalid);
    const auto link=root/"Assets/alias.dds";Need(CreateHardLinkW(link.c_str(),file.c_str(),nullptr)!=FALSE,"Thumbnail hardlink fixture");refuse(request,ThumbnailImageStatus::Invalid);Need(fs::remove(link),"Thumbnail owned hardlink cleanup");
    AssetThumbnailImageJob job;ThumbnailImageResult sentinel;sentinel.serial=999;Need(!job.Poll(sentinel)&&sentinel.serial==999,"Idle Poll changed output");Need(job.Start(request),"Thumbnail worker start");Need(!job.Start(request),"Thumbnail worker accepted overlapping request");result=Wait(job);Need(result.status==ThumbnailImageStatus::Ready&&result.rgba==expected,"Thumbnail worker publication differs from decoder");
    request.serial=2;Need(job.Start(request),"Thumbnail worker reuse");job.Cancel();result=Wait(job);Need(result.serial==2&&result.status==ThumbnailImageStatus::Cancelled&&result.rgba.empty()&&result.png.empty(),"Cancelled thumbnail worker published pixels");
    request.serial=3;Need(job.Start(request),"Thumbnail explicit join fixture start");job.CancelAndWait();sentinel.serial=999;Need(!job.IsBusy()&&!job.Poll(sentinel)&&sentinel.serial==999,"Explicit thumbnail join retained unpublished result");
    request.serial=4;Need(job.Start(request),"Thumbnail worker reuse after explicit join");result=Wait(job);Need(result.serial==4&&result.status==ThumbnailImageStatus::Ready&&result.rgba==expected,"Thumbnail worker reuse after explicit join changed pixels");
    {AssetThumbnailImageJob stopping;request.serial=5;Need(stopping.Start(request),"Thumbnail destructor fixture start");}
    Need(Read(file)==authored,"Thumbnail decoder changed source");fs::remove_all(root);root.clear();std::cout<<"PASS: candidate CPU DDS+PNG thumbnail pixels/sRGB metadata+display color/aspect/alpha/no upscale, bounded metadata and input, stale size/time/root/link/corrupt/type/cancel refusal, single worker/reuse/Cancel/Poll isolation/join and source preservation (not integrated Browser/GPU/Model previews)\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';if(!root.empty())std::cerr<<"THUMBNAIL_FIXTURE_RETAINED="<<root<<'\n';return 1;}}


