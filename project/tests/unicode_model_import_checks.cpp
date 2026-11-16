#include "../engine/base/AssetPath.h"
#include "../engine/base/directx/TextureAssetResolver.h"
#include "../engine/3d/model/ModelAssetResolver.h"
#include "../engine/base/utility/StringUtility.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary); out << bytes; Require(bool(out), "Fixture write failed");
}
}
int main() {
    using namespace Engine;
    namespace fs = std::filesystem;
    const auto fixture = fs::temp_directory_path() / (L"kubo-unicode-import-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    bool comInitialized = false;
    try {
        const auto root = fixture / L"日本語のプロジェクト_🚀";
        fs::create_directories(root / L"models/素材");
        const auto model = root / L"models/素材/三角形.obj";
        const auto material = model.parent_path() / L"材質.mtl";
        const auto texture = model.parent_path() / L"色.png";
        Write(model, "mtllib 材質.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nusemtl Sample\nf 1/1/1 2/2/1 3/3/1\n");
        Write(material, "newmtl Sample\nKd 1 1 1\nmap_Kd 色.png\n");
        fs::copy_file("Resources/textures/common/white.png", texture);
        const auto rootUtf8 = Base::PathToUtf8(root);
        const auto resolved = Graphics3D::ResolveModelAssetPath(rootUtf8, "三角形.obj");
        Require(Base::PathFromUtf8(resolved) == model, "Unicode model search failed");
        Require(Base::NormalizeAssetPath(resolved) == Base::NormalizeAssetPath(model), "UTF-8/native cache identity mismatch");
        Assimp::Importer importer;
        const auto* scene = importer.ReadFile(resolved.c_str(), 0);
        Require(scene && scene->mNumMeshes == 1 && scene->mMeshes[0]->mNumVertices == 3, importer.GetErrorString());
        aiString materialTexture;
        Require(scene->mMaterials[scene->mMeshes[0]->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE, 0, &materialTexture) == AI_SUCCESS,
            "Assimp did not read the Japanese MTL dependency");
        Require(std::string(materialTexture.C_Str()) == "色.png", "Assimp lost UTF-8 texture name");
        const auto resolvedTexture = Graphics3D::ResolveModelResourcePath(Base::PathToUtf8(model.parent_path()), materialTexture.C_Str());
        Require(Base::PathFromUtf8(resolvedTexture) == texture, "Unicode material texture resolve failed");
        Require(Base::PathFromUtf8(Base::ResolveTextureAssetPath(resolvedTexture, root, fixture)) == texture, "Texture resolver lost native Unicode path");
        Require(Base::PathFromUtf8(Base::ResolveTextureAssetPath("models/素材/色.png", fixture, root)) == texture, "Unicode project-owned texture resolve failed");
        Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "COM initialization failed");
        comInitialized = true;
        const auto nativeTexture = Base::StringUtility::ConvertString(resolvedTexture);
        Require(fs::path(nativeTexture).make_preferred() == fs::path(texture).make_preferred(), "TextureManager UTF-8 to wide boundary lost characters");
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))), "WIC factory failed");
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        Require(SUCCEEDED(factory->CreateDecoderFromFilename(nativeTexture.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)), "WIC Unicode PNG open failed");
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        Require(SUCCEEDED(decoder->GetFrame(0, &frame)), "WIC PNG frame failed");
        UINT width = 0, height = 0;
        Require(SUCCEEDED(frame->GetSize(&width, &height)) && width > 0 && height > 0 && width <= 4096 && height <= 4096, "WIC PNG size failed");
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        Require(SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)), "WIC PNG conversion failed");
        std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
        Require(SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())), "Actual WIC Unicode texture decoding failed");
        converter.Reset(); frame.Reset(); decoder.Reset(); factory.Reset();
        CoUninitialize(); comInitialized = false;
        const auto gltf = model.parent_path() / L"外部頂点.gltf";
        const auto bin = model.parent_path() / L"頂点.bin";
        const float positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        {
            std::ofstream output(bin, std::ios::binary);
            output.write(reinterpret_cast<const char*>(positions), sizeof(positions));
            Require(bool(output), "glTF binary fixture write failed");
        }
        Write(gltf, R"({"asset":{"version":"2.0"},"buffers":[{"uri":"頂点.bin","byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})");
        const auto resolvedGltf = Graphics3D::ResolveModelAssetPath(Base::PathToUtf8(model.parent_path()), "外部頂点.gltf");
        const auto* gltfScene = importer.ReadFile(resolvedGltf.c_str(), 0);
        Require(gltfScene && gltfScene->mNumMeshes == 1 && gltfScene->mMeshes[0]->mNumVertices == 3,
            importer.GetErrorString());
        const auto glb=model.parent_path()/L"埋め込み頂点.glb";
        std::string glbJson=R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
        while(glbJson.size()%4)glbJson+=' ';
        std::string glbBytes;const auto word=[&](uint32_t value){for(int index=0;index<4;++index)glbBytes+=static_cast<char>((value>>(index*8))&255);};
        word(0x46546c67);word(2);word(static_cast<uint32_t>(28+glbJson.size()+sizeof(positions)));
        word(static_cast<uint32_t>(glbJson.size()));word(0x4e4f534a);glbBytes+=glbJson;
        word(sizeof(positions));word(0x004e4942);glbBytes.append(reinterpret_cast<const char*>(positions),sizeof(positions));Write(glb,glbBytes);
        const auto* glbScene=importer.ReadFile(Base::PathToUtf8(glb).c_str(),0);
        Require(glbScene && glbScene->mNumMeshes==1 && glbScene->mMeshes[0]->mNumVertices==3,importer.GetErrorString());
        fs::remove_all(fixture);
        std::cout << "PASS: UTF-8/native model/cache/material/texture paths, bundled Assimp Unicode OBJ/MTL/glTF/bin/GLB import and actual WIC decoding\n";
        return 0;
    } catch (const std::exception& e) { if (comInitialized) CoUninitialize(); std::cerr << e.what() << '\n'; std::error_code ec; fs::remove_all(fixture, ec); return 1; }
}
