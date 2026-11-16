#include "Framework.h"
#include "DirectXCommon.h"
#include "OffscreenRenderManager.h"
#include "WinApp.h"
#include <DirectXMath.h>
#include <d3d12sdklayers.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace Engine::Base;
using namespace DirectX;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class SceneGridChecks final : public Framework {
public:
    ~SceneGridChecks() override
    {
        if (verified_) {
            std::error_code ignored;
            std::filesystem::remove_all(root_,ignored);
        }
    }

    void Initialize() override
    {
        root_ = std::filesystem::temp_directory_path() /
            (L"kubo-scene-grid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        Require(std::filesystem::create_directory(root_),"Scene grid fixture path already exists");
        SetProjectRoot(root_);
        Microsoft::WRL::ComPtr<ID3D12Debug> debug;
        Require(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))), "Scene grid debug layer unavailable");
        debug->EnableDebugLayer();
        Framework::Initialize();
        Require(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages_))), "Scene grid diagnostic queue unavailable");
        VerifyDpiResizeMessage();
    }

    void Draw() override
    {
        const auto topDown = [](float x, float z) {
            return XMMatrixLookAtLH(XMVectorSet(x,12,z,1),XMVectorSet(x,0,z,1),XMVectorSet(0,0,1,0)) *
                XMMatrixOrthographicLH(16,9,0.1f,200.0f);
        };
        for (const float background : {0.012f, 0.82f}) {
            const auto baseline = Render({240,135},topDown(0,0),background,false,1.0f);
            const auto drawn = Render({240,135},topDown(0,0),background,true,1.0f);
            const auto changed = ChangedPixels(baseline,drawn);
            Require(changed > 1000, "Grid did not produce visible coverage on a light or dark background");
            Require(changed < 240 * 135 * 9 / 10, "Fine grid tint filled the gaps between lines");
            Require(baseline == Render({240,135},topDown(0,0),background,true,0.0f), "Occluded grid still changed color pixels");
            std::cout << "SCENE_GRID background=" << background << " changed=" << changed << '\n';
        }

        const auto distantView = topDown(1250,1540);
        const auto farBaseline = Render({160,90},distantView,0.012f,false,1.0f);
        const auto farGrid = Render({160,90},distantView,0.012f,true,1.0f);
        Require(ChangedPixels(farBaseline,farGrid) > 400,"Grid disappeared after resizing away from the origin");

        const auto horizon = XMMatrixLookAtLH(XMVectorSet(0,2,-10,1),XMVectorSet(0,1,0,1),XMVectorSet(0,1,0,0)) *
            XMMatrixPerspectiveFovLH(0.65f,16.0f/9,0.1f,200.0f);
        const auto sky = Render({160,90},horizon,0.012f,false,1.0f);
        const auto floor = Render({160,90},horizon,0.012f,true,1.0f);
        Require(ChangedPixels(sky,floor) > 200, "Perspective grid did not produce visible floor coverage");
        Require(std::equal(sky.begin(),sky.begin() + 160 * 20 * 4,floor.begin()), "Grid colored the sky above the perspective horizon");
        drewAllCases_ = true;
        endRequest_ = true;
    }

    void Verify()
    {
        Require(drewAllCases_ && messages_,"Scene grid checks did not finish");
        for (UINT64 index = 0; index < messages_->GetNumStoredMessages(); ++index) {
            SIZE_T size = 0;
            Require(SUCCEEDED(messages_->GetMessage(index,nullptr,&size)),"Cannot read Scene grid diagnostic size");
            std::vector<unsigned char> bytes(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            Require(SUCCEEDED(messages_->GetMessage(index,message,&size)),"Cannot read Scene grid diagnostic");
            Require(message->Severity != D3D12_MESSAGE_SEVERITY_ERROR && message->Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION,message->pDescription);
        }
        verified_ = true;
        std::cout << "PASS: Scene grid GPU pixels, bright/dark, depth occlusion, perspective sky, far coordinates, resize and clean D3D12 diagnostics\n";
    }

private:
    void VerifyDpiResizeMessage()
    {
        const auto window = winApp->GetHwnd();
        RECT original{};
        Require(GetWindowRect(window,&original) != FALSE,"Cannot read Editor test window bounds");
        RECT suggested{original.left,original.top,original.left + 880,original.top + 640};
        SendMessageW(window,WM_DPICHANGED,MAKEWPARAM(144,144),reinterpret_cast<LPARAM>(&suggested));
        RECT observed{};
        Require(GetWindowRect(window,&observed) && EqualRect(&suggested,&observed),"Editor did not apply the DPI resize suggestion");
        SetWindowPos(window,nullptr,original.left,original.top,original.right-original.left,original.bottom-original.top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        std::cout << "PASS: Editor HWND applied the simulated WM_DPICHANGED rectangle\n";
    }

    static size_t ChangedPixels(const std::vector<unsigned char>& before, const std::vector<unsigned char>& after)
    {
        Require(before.size() == after.size(),"Scene grid readback sizes differ");
        size_t changed = 0;
        for (size_t index = 0; index < before.size(); index += 4) {
            if (!std::equal(before.begin()+index,before.begin()+index+3,after.begin()+index)) ++changed;
        }
        return changed;
    }

    std::vector<unsigned char> Render(ViewExtent extent, const XMMATRIX& matrix, float background, bool drawGrid, float depth)
    {
        offscreenRenderManager->ResizeEditorView(extent);
        offscreenRenderManager->BindEditorView();
        auto* device = dxCommon->GetDevice();
        auto* commands = dxCommon->GetCommandList();
        auto* color = offscreenRenderManager->GetViewRenderTarget(true);
        auto rtv = dxCommon->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,false);
        auto dsv = dxCommon->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1,false);
        const auto rtvHandle = rtv->GetCPUDescriptorHandleForHeapStart();
        const auto dsvHandle = dsv->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(color,&dxCommon->GetRTVDesc(),rtvHandle);
        device->CreateDepthStencilView(offscreenRenderManager->GetViewDepthTarget(true),nullptr,dsvHandle);
        const float clear[] = {background,background,background,1.0f};
        commands->ClearRenderTargetView(rtvHandle,clear,0,nullptr);
        commands->ClearDepthStencilView(dsvHandle,D3D12_CLEAR_FLAG_DEPTH,depth,0,0,nullptr);
        XMFLOAT4X4 projection;
        XMStoreFloat4x4(&projection,matrix);
        if (drawGrid) offscreenRenderManager->DrawEditorGrid(projection.m);

        const auto desc = color->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 total = 0;
        device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&total);
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = total;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        Require(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
            IID_PPV_ARGS(&readback))),"Scene grid readback allocation failed");
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = color;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_COPY_SOURCE);
        commands->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
        offscreenRenderManager->EndEditorView();
        dxCommon->CommandKick();
        const size_t rowBytes = size_t(extent.width) * 4;
        std::vector<unsigned char> pixels(rowBytes * extent.height);
        void* mapped = nullptr;
        const D3D12_RANGE range{0,size_t(total)};
        Require(SUCCEEDED(readback->Map(0,&range,&mapped)),"Scene grid readback map failed");
        const auto* data = static_cast<const unsigned char*>(mapped) + footprint.Offset;
        for (size_t row = 0; row < extent.height; ++row) {
            std::copy_n(data + row * footprint.Footprint.RowPitch,rowBytes,pixels.data() + row * rowBytes);
        }
        const D3D12_RANGE written{0,0};
        readback->Unmap(0,&written);
        return pixels;
    }

    Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages_;
    std::filesystem::path root_;
    bool drewAllCases_ = false;
    bool verified_ = false;
};
}

void RunSceneGridRenderChecks()
{
    SceneGridChecks checks;
    checks.Run();
    checks.Verify();
}
