#pragma once

#include "../../base/directx/HResult.h"
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib,"d2d1.lib")
#pragma comment(lib,"dwrite.lib")
#pragma comment(lib,"windowscodecs.lib")

namespace Engine::Scene {

// Generates a straight-alpha white glyph texture. Sprite tint supplies the text color.
class UITextRasterizer final {
public:
    static constexpr uint32_t MaxDimension=1024;

    std::vector<uint8_t> Rasterize(std::string_view utf8,uint32_t width,uint32_t height,float fontPixels,
        DWRITE_TEXT_ALIGNMENT alignment)
    {
        using Microsoft::WRL::ComPtr;
        using Base::ThrowIfFailed;
        if(utf8.empty() || utf8.size()>4096 || utf8.find('\0')!=utf8.npos ||
            !std::isfinite(fontPixels) || fontPixels<=0 || !width || !height || width>MaxDimension || height>MaxDimension)
            throw std::invalid_argument("Invalid UI text bitmap");
        const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),int(utf8.size()),nullptr,0);
        if(!count)throw std::invalid_argument("Invalid UI text UTF-8");
        std::wstring text(count,L'\0');
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),int(utf8.size()),text.data(),count))
            throw std::invalid_argument("Could not convert UI text");

        Initialize();
        ComPtr<IWICBitmap> bitmap;
        ThrowIfFailed(imaging_->CreateBitmap(width,height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap),
            "Create UI text bitmap");
        ComPtr<ID2D1RenderTarget> target;
        const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        ThrowIfFailed(drawing_->CreateWicBitmapRenderTarget(bitmap.Get(),properties,&target),"Create UI text render target");
        ComPtr<IDWriteTextFormat> format;
        ThrowIfFailed(writing_->CreateTextFormat(L"Yu Gothic UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,fontPixels,L"ja-jp",&format),"Create UI text format");
        ThrowIfFailed(format->SetTextAlignment(alignment),"Set UI text alignment");
        ThrowIfFailed(format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER),"Center UI text vertically");
        ComPtr<IDWriteTextLayout> layout;
        ThrowIfFailed(writing_->CreateTextLayout(text.data(),UINT32(count),format.Get(),float(width),float(height),&layout),
            "Lay out UI text");
        ComPtr<ID2D1SolidColorBrush> brush;
        ThrowIfFailed(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White),&brush),"Create UI text brush");
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0,0,0,0));
        target->DrawTextLayout(D2D1::Point2F(),layout.Get(),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
        ThrowIfFailed(target->EndDraw(),"Draw UI text");

        std::vector<uint8_t> pixels(size_t(width)*height*4);
        ThrowIfFailed(bitmap->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data()),"Read UI text pixels");
        for(size_t i=0;i<pixels.size();i+=4) pixels[i]=pixels[i+1]=pixels[i+2]=255;
        return pixels;
    }
private:
    void Initialize() {
        using Base::ThrowIfFailed;
        if(!imaging_)
            ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&imaging_)),
                "Create UI text imaging factory");
        if(!drawing_)
            ThrowIfFailed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,drawing_.ReleaseAndGetAddressOf()),
                "Create UI text drawing factory");
        if(!writing_)
            ThrowIfFailed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(writing_.ReleaseAndGetAddressOf())),"Create UI text font factory");
    }
    Microsoft::WRL::ComPtr<IWICImagingFactory> imaging_;
    Microsoft::WRL::ComPtr<ID2D1Factory> drawing_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writing_;
};

} // namespace Engine::Scene
