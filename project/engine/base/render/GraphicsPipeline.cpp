#include "GraphicsPipeline.h"
#include "DirectXCommon.h"
#include "HResult.h"
#include "Logger.h"
#include "OffscreenRenderManager.h"
#include "PipelineRootSignatureFactory.h"
#include "PipelineStateBuilder.h"
#include <array>

namespace {

D3D12_BLEND_DESC CreateAlphaBlendDesc(D3D12_BLEND destBlend)
{
	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = true;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = destBlend;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	return blendDesc;
}

D3D12_RASTERIZER_DESC CreateSolidRasterizerDesc(D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_NONE)
{
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = cullMode;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	return rasterizerDesc;
}

D3D12_DEPTH_STENCIL_DESC CreateDepthStencilDesc(D3D12_DEPTH_WRITE_MASK depthWriteMask)
{
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = depthWriteMask;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	return depthStencilDesc;
}

D3D12_DEPTH_STENCIL_DESC CreateDisabledDepthStencilDesc()
{
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = false;
	depthStencilDesc.StencilEnable = false;
	return depthStencilDesc;
}

void SetupStandardInputElements(D3D12_INPUT_ELEMENT_DESC (&inputElementDescs)[3])
{
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
}

void SetupSkinningInputElements(D3D12_INPUT_ELEMENT_DESC (&inputElementDescs)[5])
{
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[3].SemanticName = "WEIGHT";
	inputElementDescs[3].SemanticIndex = 0;
	inputElementDescs[3].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[3].InputSlot = 1;
	inputElementDescs[3].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[4].SemanticName = "INDEX";
	inputElementDescs[4].SemanticIndex = 0;
	inputElementDescs[4].Format = DXGI_FORMAT_R32G32B32A32_SINT;
	inputElementDescs[4].InputSlot = 1;
	inputElementDescs[4].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
}

void SetupLineInputElements(D3D12_INPUT_ELEMENT_DESC (&inputElementDescs)[1])
{
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = 0;
}

D3D12_INPUT_LAYOUT_DESC CreateInputLayoutDesc(D3D12_INPUT_ELEMENT_DESC* inputElements, UINT elementCount)
{
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElements;
	inputLayoutDesc.NumElements = elementCount;
	return inputLayoutDesc;
}

D3D12_INPUT_LAYOUT_DESC CreateEmptyInputLayoutDesc()
{
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = nullptr;
	inputLayoutDesc.NumElements = 0;
	return inputLayoutDesc;
}

std::pair<Microsoft::WRL::ComPtr<IDxcBlob>, Microsoft::WRL::ComPtr<IDxcBlob>> CompileShaderPair(
	Engine::Base::DirectXCommon* dxCommon,
	const wchar_t* vertexShaderPath,
	const wchar_t* pixelShaderPath)
{
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob =
		dxCommon->CompileShader(vertexShaderPath, L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob =
		dxCommon->CompileShader(pixelShaderPath, L"ps_6_0");
	assert(pixelShaderBlob != nullptr);

	return { vertexShaderBlob, pixelShaderBlob };
}

using PostEffectEntry = std::pair<PostEffectType, const wchar_t*>;

const std::array<PostEffectEntry, 6> kPostEffectEntries = { {
	{ PostEffectType::Fullscreen, L"Resources/Shaders/post/fullscreen/Fullscreen.PS.hlsl" },
	{ PostEffectType::Grayscale, L"Resources/Shaders/post/color/GrayScale.PS.hlsl" },
	{ PostEffectType::Vignette, L"Resources/Shaders/post/color/Vignette.PS.hlsl" },
	{ PostEffectType::BoxFilter, L"Resources/Shaders/filter/convolution/BoxFilter.PS.hlsl" },
	{ PostEffectType::LuminanceOutline, L"Resources/Shaders/filter/outline/LuminanceBasedOutline.PS.hlsl" },
	{ PostEffectType::RadialBlur, L"Resources/Shaders/post/fullscreen/RadialBlur.PS.hlsl" },
} };

}

namespace Engine::Base {

void GraphicsPipeline::Create()
{

	RootSignatureCreate();

	// Object3D 用の頂点レイアウトを組み立てる
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	SetupStandardInputElements(inputElementDescs);
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = CreateInputLayoutDesc(inputElementDescs, _countof(inputElementDescs));

	D3D12_BLEND_DESC blendDesc = CreateAlphaBlendDesc(D3D12_BLEND_INV_SRC_ALPHA);
	D3D12_RASTERIZER_DESC rasterizerDesc = CreateSolidRasterizerDesc();

	// 通常 3D 描画用のシェーダーを読み込み、共通 PSO を生成する
	auto [vertexshaderBlob, pixelShaderBlob] = CompileShaderPair(
		dxCommon_, L"Resources/Shaders/object/Object3d.VS.hlsl", L"Resources/Shaders/object/Object3d.PS.hlsl");

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc = CreateDepthStencilDesc(D3D12_DEPTH_WRITE_MASK_ALL);
	CreateGraphicsPipelineState(dxCommon_, {
		rootSignature.Get(),
		inputLayoutDesc,
		vertexshaderBlob.Get(),
		pixelShaderBlob.Get(),
		blendDesc,
		rasterizerDesc,
		depthStencilDesc,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
	}, graphicsPipelineState.GetAddressOf());

}


void GraphicsPipeline::RootSignatureCreate()
{
	CreateObjectRootSignature(dxCommon_, rootSignature.GetAddressOf());
}


void GraphicsPipeline::CreateParticle()
{

	RootSignatureParticleCreate();

	// パーティクルは通常メッシュと同じ頂点を使いつつ、加算合成で描画する
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	SetupStandardInputElements(inputElementDescs);
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = CreateInputLayoutDesc(inputElementDescs, _countof(inputElementDescs));

	D3D12_BLEND_DESC blendDesc = CreateAlphaBlendDesc(D3D12_BLEND_ONE);
	D3D12_RASTERIZER_DESC rasterizerDesc = CreateSolidRasterizerDesc();

	auto [vertexshaderBlob, pixelShaderBlob] = CompileShaderPair(
		dxCommon_, L"Resources/Shaders/particle/Particle.VS.hlsl", L"Resources/Shaders/particle/Particle.PS.hlsl");

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc = CreateDepthStencilDesc(D3D12_DEPTH_WRITE_MASK_ZERO);
	CreateGraphicsPipelineState(dxCommon_, {
		rootSignatureParticle.Get(),
		inputLayoutDesc,
		vertexshaderBlob.Get(),
		pixelShaderBlob.Get(),
		blendDesc,
		rasterizerDesc,
		depthStencilDesc,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
	}, graphicsPipelineStateParticle.GetAddressOf());

}

void GraphicsPipeline::RootSignatureParticleCreate()
{
	CreateParticleRootSignature(dxCommon_, rootSignatureParticle.GetAddressOf());
}





void GraphicsPipeline::CreateSprite()
{
	RootSignatureSpriteCreate();

	// Sprite 用の最小構成パイプラインを作成する
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	SetupStandardInputElements(inputElementDescs);
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = CreateInputLayoutDesc(inputElementDescs, _countof(inputElementDescs));

	D3D12_BLEND_DESC blendDesc = CreateAlphaBlendDesc(D3D12_BLEND_INV_SRC_ALPHA);
	D3D12_RASTERIZER_DESC rasterizerDesc = CreateSolidRasterizerDesc();

	auto [vertexshaderBlob, pixelShaderBlob] = CompileShaderPair(
		dxCommon_, L"Resources/Shaders/sprite/Sprite.VS.hlsl", L"Resources/Shaders/sprite/Sprite.PS.hlsl");

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc = CreateDepthStencilDesc(D3D12_DEPTH_WRITE_MASK_ALL);
	CreateGraphicsPipelineState(dxCommon_, {
		rootSignatureSprite.Get(),
		inputLayoutDesc,
		vertexshaderBlob.Get(),
		pixelShaderBlob.Get(),
		blendDesc,
		rasterizerDesc,
		depthStencilDesc,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
	}, graphicsPipelineStateSprite.GetAddressOf());

}


void GraphicsPipeline::RootSignatureLineCreate()
{
	CreateLineRootSignature(dxCommon_, rootSignatureLine.GetAddressOf());
}

void GraphicsPipeline::RootSignatureSkinningCreate()
{
	CreateSkinningRootSignature(dxCommon_, rootSignatureSkinning.GetAddressOf());
}

void GraphicsPipeline::CreateShadowMap()
{
	RootSignatureShadowMapCreate();
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	SetupStandardInputElements(inputElementDescs);
	const D3D12_INPUT_LAYOUT_DESC inputLayoutDesc =
		CreateInputLayoutDesc(inputElementDescs, _countof(inputElementDescs));
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob =
		dxCommon_->CompileShader(L"Resources/Shaders/object/ShadowMap.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	D3D12_RASTERIZER_DESC rasterizerDesc = CreateSolidRasterizerDesc(D3D12_CULL_MODE_NONE);
	rasterizerDesc.DepthBias = 180;
	rasterizerDesc.SlopeScaledDepthBias = 1.0f;
	rasterizerDesc.DepthBiasClamp = 0.002f;
	CreateGraphicsPipelineState(dxCommon_, {
		rootSignatureShadowMap.Get(),
		inputLayoutDesc,
		vertexShaderBlob.Get(),
		nullptr,
		CreateAlphaBlendDesc(D3D12_BLEND_ZERO),
		rasterizerDesc,
		CreateDepthStencilDesc(D3D12_DEPTH_WRITE_MASK_ALL),
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		0,
		DXGI_FORMAT_UNKNOWN,
		DXGI_FORMAT_D32_FLOAT,
		"ID3D12Device::CreateGraphicsPipelineState shadow map",
	}, graphicsPipelineStateShadowMap.GetAddressOf());
}

void GraphicsPipeline::RootSignatureShadowMapCreate()
{
	CreateShadowMapRootSignature(dxCommon_, rootSignatureShadowMap.GetAddressOf());
}

void GraphicsPipeline::CreateSkinning()
{
	RootSignatureSkinningCreate();

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[5] = {};
	SetupSkinningInputElements(inputElementDescs);
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = CreateInputLayoutDesc(inputElementDescs, _countof(inputElementDescs));
	D3D12_BLEND_DESC blendDesc = CreateAlphaBlendDesc(D3D12_BLEND_INV_SRC_ALPHA);
	D3D12_RASTERIZER_DESC rasterizerDesc = CreateSolidRasterizerDesc();
	auto [vertexshaderBlob, pixelShaderBlob] = CompileShaderPair(
		dxCommon_, L"Resources/Shaders/object/SkinningObject3d.VS.hlsl", L"Resources/Shaders/object/SkinningObject3d.PS.hlsl");
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc = CreateDepthStencilDesc(D3D12_DEPTH_WRITE_MASK_ALL);
	CreateGraphicsPipelineState(dxCommon_, {
		rootSignatureSkinning.Get(),
		inputLayoutDesc,
		vertexshaderBlob.Get(),
		pixelShaderBlob.Get(),
		blendDesc,
		rasterizerDesc,
		depthStencilDesc,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
	}, graphicsPipelineStateSkinning.GetAddressOf());
}

void GraphicsPipeline::CreateLine()
{
	// RootSignature作成（ライン用）
	RootSignatureLineCreate(); 
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[1] = {};
	SetupLineInputElements(inputElementDescs);
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = CreateInputLayoutDesc(inputElementDescs, _countof(inputElementDescs));
	D3D12_BLEND_DESC blendDesc = CreateAlphaBlendDesc(D3D12_BLEND_INV_SRC_ALPHA);
	D3D12_RASTERIZER_DESC rasterizerDesc = CreateSolidRasterizerDesc();
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc = CreateDepthStencilDesc(D3D12_DEPTH_WRITE_MASK_ALL);
	auto [vertexShaderBlob, pixelShaderBlob] = CompileShaderPair(
		dxCommon_, L"Resources/Shaders/line/Line.VS.hlsl", L"Resources/Shaders/line/Line.PS.hlsl");
	CreateGraphicsPipelineState(dxCommon_, {
		rootSignatureLine.Get(),
		inputLayoutDesc,
		vertexShaderBlob.Get(),
		pixelShaderBlob.Get(),
		blendDesc,
		rasterizerDesc,
		depthStencilDesc,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
	}, graphicsPipelineStateLine.GetAddressOf());
}




void GraphicsPipeline::RootSignatureSpriteCreate()
{
	CreateSpriteRootSignature(dxCommon_, rootSignatureSprite.GetAddressOf());
}
void GraphicsPipeline::CreateCopyImage(PostEffectType type, const std::wstring& psFilename)
{
	RootSignatureCopyImageCreate();

	// フルスクリーン三角形で共通 VS とポストエフェクト別 PS を組み合わせる
	auto [vertexShaderBlob, pixelShaderBlob] = CompileShaderPair(
		dxCommon_, L"Resources/Shaders/post/fullscreen/Fullscreen.VS.hlsl", psFilename.c_str());
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = CreateEmptyInputLayoutDesc();
	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	D3D12_RASTERIZER_DESC rasterizerDesc = CreateSolidRasterizerDesc();
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc = CreateDisabledDepthStencilDesc();
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
	CreateGraphicsPipelineState(dxCommon_, {
		rootSignatureCopyImage.Get(),
		inputLayoutDesc,
		vertexShaderBlob.Get(),
		pixelShaderBlob.Get(),
		blendDesc,
		rasterizerDesc,
		depthStencilDesc,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
	}, pso.GetAddressOf());

	// 後段描画で種類ごとに切り替えられるよう PSO を保持する
	copyImagePipelines_[type] = pso;
}

void GraphicsPipeline::CreateAllPostEffects() {
	for (const auto& [type, shaderPath] : kPostEffectEntries) {
		// ポストエフェクト種別ごとに PSO を先行生成しておく
		CreateCopyImage(type, shaderPath);
	}
}


void GraphicsPipeline::RootSignatureCopyImageCreate()
{
	CreateCopyImageRootSignature(dxCommon_, rootSignatureCopyImage.GetAddressOf());
}




void GraphicsPipeline::CreateSkybox()
{
	RootSignatureSkyboxCreate();

	// スカイボックスは通常メッシュと同じ頂点形式を使うが、深度書き込みだけ無効化する
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	SetupStandardInputElements(inputElementDescs);
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = CreateInputLayoutDesc(inputElementDescs, _countof(inputElementDescs));
	D3D12_BLEND_DESC blendDesc = CreateAlphaBlendDesc(D3D12_BLEND_INV_SRC_ALPHA);
	D3D12_RASTERIZER_DESC rasterizerDesc = CreateSolidRasterizerDesc();
	auto [vertexshaderBlob, pixelShaderBlob] = CompileShaderPair(
		dxCommon_, L"Resources/Shaders/skybox/Skybox.VS.hlsl", L"Resources/Shaders/skybox/Skybox.PS.hlsl");
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc = CreateDepthStencilDesc(D3D12_DEPTH_WRITE_MASK_ZERO);
	CreateGraphicsPipelineState(dxCommon_, {
		rootSignatureSkybox.Get(),
		inputLayoutDesc,
		vertexshaderBlob.Get(),
		pixelShaderBlob.Get(),
		blendDesc,
		rasterizerDesc,
		depthStencilDesc,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
	}, graphicsPipelineStateSkybox.GetAddressOf());
}

void GraphicsPipeline::RootSignatureSkyboxCreate()
{
	CreateSkyboxRootSignature(dxCommon_, rootSignatureSkybox.GetAddressOf());
}

ID3D12PipelineState* GraphicsPipeline::GetGraphicsPipelineStateCopyImage(PostEffectType type) {
	auto it = copyImagePipelines_.find(type);
	if (it != copyImagePipelines_.end()) {
		return it->second.Get();
	}
	return nullptr; // または assert(false)
}

void GraphicsPipeline::Initialize(Engine::Base::DirectXCommon* dxCommon)
{
	dxCommon_ = dxCommon;

}

}
