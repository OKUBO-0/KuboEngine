#include "GraphicsPipeline.h"
#include "DirectXCommon.h"
#include "HResult.h"
#include "Logger.h"
#include "OffscreenRenderManager.h"
#include "PipelineRootSignatureFactory.h"
#include "PipelineStateBuilder.h"
#include <array>
#include <vector>

namespace {

enum class PipelineInputLayout {
	Standard,
	Skinning,
	Line,
	Empty,
};

enum class PipelineBlendMode {
	Alpha,
	Additive,
	Disabled,
};

enum class PipelineDepthMode {
	ReadWrite,
	ReadOnly,
	Disabled,
};

struct PipelineDefinition {
	const char* key;
	Microsoft::WRL::ComPtr<ID3D12RootSignature>* rootSignature;
	const wchar_t* vertexShaderPath;
	const wchar_t* pixelShaderPath;
	PipelineInputLayout inputLayout = PipelineInputLayout::Standard;
	PipelineBlendMode blendMode = PipelineBlendMode::Alpha;
	PipelineDepthMode depthMode = PipelineDepthMode::ReadWrite;
	D3D12_PRIMITIVE_TOPOLOGY_TYPE topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_NONE;
	UINT renderTargetCount = 1;
	DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	int depthBias = 0;
	float slopeScaledDepthBias = 0.0f;
	float depthBiasClamp = 0.0f;
	const char* failureContext = "ID3D12Device::CreateGraphicsPipelineState";
};

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

D3D12_BLEND_DESC CreateDisabledBlendDesc()
{
	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = false;
	return blendDesc;
}

D3D12_BLEND_DESC CreateBlendDesc(PipelineBlendMode mode)
{
	switch (mode) {
	case PipelineBlendMode::Alpha:
		return CreateAlphaBlendDesc(D3D12_BLEND_INV_SRC_ALPHA);
	case PipelineBlendMode::Additive:
		return CreateAlphaBlendDesc(D3D12_BLEND_ONE);
	case PipelineBlendMode::Disabled:
	default:
		return CreateDisabledBlendDesc();
	}
}

D3D12_RASTERIZER_DESC CreateSolidRasterizerDesc(D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_NONE)
{
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = cullMode;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	return rasterizerDesc;
}

D3D12_RASTERIZER_DESC CreateRasterizerDesc(const PipelineDefinition& definition)
{
	D3D12_RASTERIZER_DESC rasterizerDesc =
		CreateSolidRasterizerDesc(definition.cullMode);
	rasterizerDesc.DepthBias = definition.depthBias;
	rasterizerDesc.SlopeScaledDepthBias = definition.slopeScaledDepthBias;
	rasterizerDesc.DepthBiasClamp = definition.depthBiasClamp;
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

D3D12_DEPTH_STENCIL_DESC CreateDepthStencilDesc(PipelineDepthMode mode)
{
	switch (mode) {
	case PipelineDepthMode::ReadWrite:
		return CreateDepthStencilDesc(D3D12_DEPTH_WRITE_MASK_ALL);
	case PipelineDepthMode::ReadOnly:
		return CreateDepthStencilDesc(D3D12_DEPTH_WRITE_MASK_ZERO);
	case PipelineDepthMode::Disabled:
	default:
		return CreateDisabledDepthStencilDesc();
	}
}

void SetupStandardInputElements(D3D12_INPUT_ELEMENT_DESC* inputElementDescs)
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

void SetupSkinningInputElements(D3D12_INPUT_ELEMENT_DESC* inputElementDescs)
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

void SetupLineInputElements(D3D12_INPUT_ELEMENT_DESC* inputElementDescs)
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

std::vector<D3D12_INPUT_ELEMENT_DESC> CreateInputElements(PipelineInputLayout inputLayout)
{
	switch (inputLayout) {
	case PipelineInputLayout::Standard: {
		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements(3);
		SetupStandardInputElements(inputElements.data());
		return inputElements;
	}
	case PipelineInputLayout::Skinning: {
		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements(5);
		SetupSkinningInputElements(inputElements.data());
		return inputElements;
	}
	case PipelineInputLayout::Line: {
		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements(1);
		SetupLineInputElements(inputElements.data());
		return inputElements;
	}
	case PipelineInputLayout::Empty:
	default:
		return {};
	}
}

D3D12_INPUT_LAYOUT_DESC CreateInputLayoutDesc(
	const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputElements)
{
	if (inputElements.empty()) {
		return CreateEmptyInputLayoutDesc();
	}
	return CreateInputLayoutDesc(
		const_cast<D3D12_INPUT_ELEMENT_DESC*>(inputElements.data()),
		static_cast<UINT>(inputElements.size()));
}

std::pair<Microsoft::WRL::ComPtr<IDxcBlob>, Microsoft::WRL::ComPtr<IDxcBlob>> CompileShaderPair(
	Engine::Base::DirectXCommon& dxCommon,
	const wchar_t* vertexShaderPath,
	const wchar_t* pixelShaderPath)
{
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob =
		dxCommon.CompileShader(vertexShaderPath, L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob =
		pixelShaderPath ? dxCommon.CompileShader(pixelShaderPath, L"ps_6_0") : nullptr;
	assert(pixelShaderPath == nullptr || pixelShaderBlob != nullptr);

	return { vertexShaderBlob, pixelShaderBlob };
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePipelineFromDefinition(
	Engine::Base::GraphicsPipeline& owner,
	Engine::Base::DirectXCommon& dxCommon,
	const PipelineDefinition& definition)
{
	const std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements =
		CreateInputElements(definition.inputLayout);
	const D3D12_INPUT_LAYOUT_DESC inputLayoutDesc =
		CreateInputLayoutDesc(inputElements);
	auto [vertexShaderBlob, pixelShaderBlob] = CompileShaderPair(
		dxCommon,
		definition.vertexShaderPath,
		definition.pixelShaderPath);
	return owner.CreateAndRegisterPipelineState(
		definition.key,
		*definition.rootSignature,
		{
			definition.rootSignature->Get(),
			inputLayoutDesc,
			vertexShaderBlob.Get(),
			pixelShaderBlob.Get(),
			CreateBlendDesc(definition.blendMode),
			CreateRasterizerDesc(definition),
			CreateDepthStencilDesc(definition.depthMode),
			definition.topology,
			definition.renderTargetCount,
			definition.renderTargetFormat,
			definition.depthStencilFormat,
			definition.failureContext,
		});
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
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);

	RootSignatureCreate();
	graphicsPipelineState = CreatePipelineFromDefinition(*this, *dxCommon, {
		"Object3D",
		&rootSignature,
		L"Resources/Shaders/object/Object3d.VS.hlsl",
		L"Resources/Shaders/object/Object3d.PS.hlsl",
	});

}


void GraphicsPipeline::RootSignatureCreate()
{
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	CreateObjectRootSignature(*dxCommon, rootSignature.GetAddressOf());
}


void GraphicsPipeline::CreateParticle()
{
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);

	RootSignatureParticleCreate();
	graphicsPipelineStateParticle = CreatePipelineFromDefinition(*this, *dxCommon, {
		"Particle",
		&rootSignatureParticle,
		L"Resources/Shaders/particle/Particle.VS.hlsl",
		L"Resources/Shaders/particle/Particle.PS.hlsl",
		PipelineInputLayout::Standard,
		PipelineBlendMode::Additive,
		PipelineDepthMode::ReadOnly,
	});

}

void GraphicsPipeline::RootSignatureParticleCreate()
{
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	CreateParticleRootSignature(*dxCommon, rootSignatureParticle.GetAddressOf());
}





void GraphicsPipeline::CreateSprite()
{
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	RootSignatureSpriteCreate();
	graphicsPipelineStateSprite = CreatePipelineFromDefinition(*this, *dxCommon, {
		"Sprite",
		&rootSignatureSprite,
		L"Resources/Shaders/sprite/Sprite.VS.hlsl",
		L"Resources/Shaders/sprite/Sprite.PS.hlsl",
	});

}


void GraphicsPipeline::RootSignatureLineCreate()
{
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	CreateLineRootSignature(*dxCommon, rootSignatureLine.GetAddressOf());
}

void GraphicsPipeline::RootSignatureSkinningCreate()
{
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	CreateSkinningRootSignature(*dxCommon, rootSignatureSkinning.GetAddressOf());
}

void GraphicsPipeline::CreateShadowMap()
{
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	RootSignatureShadowMapCreate();
	graphicsPipelineStateShadowMap = CreatePipelineFromDefinition(*this, *dxCommon, {
		"ShadowMap",
		&rootSignatureShadowMap,
		L"Resources/Shaders/object/ShadowMap.VS.hlsl",
		nullptr,
		PipelineInputLayout::Standard,
		PipelineBlendMode::Disabled,
		PipelineDepthMode::ReadWrite,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		D3D12_CULL_MODE_NONE,
		0,
		DXGI_FORMAT_UNKNOWN,
		DXGI_FORMAT_D32_FLOAT,
		180,
		1.0f,
		0.002f,
		"ID3D12Device::CreateGraphicsPipelineState shadow map",
	});
}

void GraphicsPipeline::RootSignatureShadowMapCreate()
{
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	CreateShadowMapRootSignature(*dxCommon, rootSignatureShadowMap.GetAddressOf());
}

void GraphicsPipeline::CreateSkinning()
{
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	RootSignatureSkinningCreate();
	graphicsPipelineStateSkinning = CreatePipelineFromDefinition(*this, *dxCommon, {
		"Skinning",
		&rootSignatureSkinning,
		L"Resources/Shaders/object/SkinningObject3d.VS.hlsl",
		L"Resources/Shaders/object/SkinningObject3d.PS.hlsl",
		PipelineInputLayout::Skinning,
	});
}

void GraphicsPipeline::CreateLine()
{
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	RootSignatureLineCreate(); 
	graphicsPipelineStateLine = CreatePipelineFromDefinition(*this, *dxCommon, {
		"Line",
		&rootSignatureLine,
		L"Resources/Shaders/line/Line.VS.hlsl",
		L"Resources/Shaders/line/Line.PS.hlsl",
		PipelineInputLayout::Line,
		PipelineBlendMode::Alpha,
		PipelineDepthMode::ReadWrite,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
	});
}




void GraphicsPipeline::RootSignatureSpriteCreate()
{
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	CreateSpriteRootSignature(*dxCommon, rootSignatureSprite.GetAddressOf());
}
void GraphicsPipeline::CreateCopyImage(PostEffectType type, const std::wstring& psFilename)
{
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	RootSignatureCopyImageCreate();
	const std::string key = "PostEffect." + std::to_string(static_cast<int>(type));
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso =
		CreatePipelineFromDefinition(*this, *dxCommon, {
			key.c_str(),
			&rootSignatureCopyImage,
			L"Resources/Shaders/post/fullscreen/Fullscreen.VS.hlsl",
			psFilename.c_str(),
			PipelineInputLayout::Empty,
			PipelineBlendMode::Disabled,
			PipelineDepthMode::Disabled,
		});
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
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	CreateCopyImageRootSignature(*dxCommon, rootSignatureCopyImage.GetAddressOf());
}




void GraphicsPipeline::CreateSkybox()
{
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	RootSignatureSkyboxCreate();
	graphicsPipelineStateSkybox = CreatePipelineFromDefinition(*this, *dxCommon, {
		"Skybox",
		&rootSignatureSkybox,
		L"Resources/Shaders/skybox/Skybox.VS.hlsl",
		L"Resources/Shaders/skybox/Skybox.PS.hlsl",
		PipelineInputLayout::Standard,
		PipelineBlendMode::Alpha,
		PipelineDepthMode::ReadOnly,
	});
}

void GraphicsPipeline::RootSignatureSkyboxCreate()
{
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	CreateSkyboxRootSignature(*dxCommon, rootSignatureSkybox.GetAddressOf());
}

Microsoft::WRL::ComPtr<ID3D12PipelineState>
GraphicsPipeline::GetGraphicsPipelineStateCopyImageHandle(PostEffectType type) const
{
	auto it = copyImagePipelines_.find(type);
	if (it != copyImagePipelines_.end()) {
		return it->second;
	}
	return nullptr;
}

void GraphicsPipeline::RegisterPipeline(
	const std::string& key,
	const Microsoft::WRL::ComPtr<ID3D12RootSignature>& rootSignature,
	const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState)
{
	pipelineRegistry_[key] = { rootSignature, pipelineState };
}

Microsoft::WRL::ComPtr<ID3D12PipelineState>
GraphicsPipeline::CreateAndRegisterPipelineState(
	const std::string& key,
	const Microsoft::WRL::ComPtr<ID3D12RootSignature>& rootSignature,
	const GraphicsPipelineStateRequest& request)
{
	GraphicsPipelineStateRequest adjustedRequest = request;
	adjustedRequest.rootSignature = rootSignature.Get();
	const auto dxCommon = dxCommon_.lock();
	assert(dxCommon);
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
	CreateGraphicsPipelineState(
		*dxCommon,
		adjustedRequest,
		pipelineState.GetAddressOf());
	RegisterPipeline(key, rootSignature, pipelineState);
	return pipelineState;
}

const GraphicsPipeline::PipelineResourceSet*
GraphicsPipeline::FindPipeline(const std::string& key) const
{
	const auto it = pipelineRegistry_.find(key);
	return it != pipelineRegistry_.end() ? &it->second : nullptr;
}

Microsoft::WRL::ComPtr<ID3D12RootSignature>
GraphicsPipeline::GetRootSignatureHandle(const std::string& key) const
{
	const PipelineResourceSet* pipeline = FindPipeline(key);
	return pipeline ? pipeline->rootSignature : nullptr;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState>
GraphicsPipeline::GetPipelineStateHandle(const std::string& key) const
{
	const PipelineResourceSet* pipeline = FindPipeline(key);
	return pipeline ? pipeline->pipelineState : nullptr;
}

void GraphicsPipeline::Initialize(std::shared_ptr<Engine::Base::DirectXCommon> dxCommon)
{
	assert(dxCommon);
	dxCommon_ = dxCommon;

}

}
