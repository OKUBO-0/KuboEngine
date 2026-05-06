#include "GraphicsPipeline.h"
#include "DirectXCommon.h"
#include "Logger.h"
#include "OffscreenRenderManager.h"
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
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
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

D3D12_DESCRIPTOR_RANGE CreateSrvDescriptorRange(UINT shaderRegister)
{
	D3D12_DESCRIPTOR_RANGE descriptorRange{};
	descriptorRange.BaseShaderRegister = shaderRegister;
	descriptorRange.NumDescriptors = 1;
	descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	return descriptorRange;
}

D3D12_ROOT_PARAMETER CreateCbvRootParameter(UINT shaderRegister, D3D12_SHADER_VISIBILITY shaderVisibility)
{
	D3D12_ROOT_PARAMETER rootParameter{};
	rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameter.ShaderVisibility = shaderVisibility;
	rootParameter.Descriptor.ShaderRegister = shaderRegister;
	return rootParameter;
}

D3D12_ROOT_PARAMETER CreateDescriptorTableRootParameter(
	const D3D12_DESCRIPTOR_RANGE* descriptorRanges,
	UINT descriptorRangeCount,
	D3D12_SHADER_VISIBILITY shaderVisibility)
{
	D3D12_ROOT_PARAMETER rootParameter{};
	rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameter.ShaderVisibility = shaderVisibility;
	rootParameter.DescriptorTable.pDescriptorRanges = descriptorRanges;
	rootParameter.DescriptorTable.NumDescriptorRanges = descriptorRangeCount;
	return rootParameter;
}

D3D12_STATIC_SAMPLER_DESC CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE addressMode)
{
	D3D12_STATIC_SAMPLER_DESC staticSampler{};
	staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSampler.AddressU = addressMode;
	staticSampler.AddressV = addressMode;
	staticSampler.AddressW = addressMode;
	staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
	staticSampler.ShaderRegister = 0;
	staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	return staticSampler;
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

std::pair<IDxcBlob*, IDxcBlob*> CompileShaderPair(
	Engine::Base::DirectXCommon* dxCommon,
	const wchar_t* vertexShaderPath,
	const wchar_t* pixelShaderPath)
{
	IDxcBlob* vertexShaderBlob = dxCommon->CompileShader(vertexShaderPath, L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	IDxcBlob* pixelShaderBlob = dxCommon->CompileShader(pixelShaderPath, L"ps_6_0");
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

void CreateRootSignatureFromDesc(
	Engine::Base::DirectXCommon* dxCommon,
	const D3D12_ROOT_SIGNATURE_DESC& descriptionRootSignature,
	ID3D12RootSignature** rootSignature)
{
	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(
		&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		Engine::Base::Logger::Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}

	hr = dxCommon->GetDevice()->CreateRootSignature(
		0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(rootSignature));
	assert(SUCCEEDED(hr));
}

void CreateRootSignatureWithParameters(
	Engine::Base::DirectXCommon* dxCommon,
	const D3D12_ROOT_PARAMETER* rootParameters,
	UINT rootParameterCount,
	const D3D12_STATIC_SAMPLER_DESC* staticSamplers,
	UINT staticSamplerCount,
	ID3D12RootSignature** rootSignature)
{
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = rootParameterCount;
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = staticSamplerCount;
	CreateRootSignatureFromDesc(dxCommon, descriptionRootSignature, rootSignature);
}

std::array<D3D12_ROOT_PARAMETER, 9> CreateObjectRootParameters(
	std::array<D3D12_DESCRIPTOR_RANGE, 2>& descriptorRanges)
{
	descriptorRanges[0] = CreateSrvDescriptorRange(0);
	descriptorRanges[1] = CreateSrvDescriptorRange(1);
	return {
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_VERTEX),
		CreateDescriptorTableRootParameter(&descriptorRanges[0], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(2, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(3, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(4, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateDescriptorTableRootParameter(&descriptorRanges[1], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(5, D3D12_SHADER_VISIBILITY_PIXEL),
	};
}

std::array<D3D12_ROOT_PARAMETER, 3> CreateParticleRootParameters(D3D12_DESCRIPTOR_RANGE& descriptorRange)
{
	descriptorRange = CreateSrvDescriptorRange(0);
	return {
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateDescriptorTableRootParameter(&descriptorRange, 1, D3D12_SHADER_VISIBILITY_VERTEX),
		CreateDescriptorTableRootParameter(&descriptorRange, 1, D3D12_SHADER_VISIBILITY_PIXEL),
	};
}

std::array<D3D12_ROOT_PARAMETER, 2> CreateLineRootParameters(D3D12_DESCRIPTOR_RANGE& descriptorRange)
{
	descriptorRange = CreateSrvDescriptorRange(0);
	return {
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_VERTEX),
		CreateDescriptorTableRootParameter(&descriptorRange, 1, D3D12_SHADER_VISIBILITY_VERTEX),
	};
}

std::array<D3D12_ROOT_PARAMETER, 10> CreateSkinningRootParameters(
	std::array<D3D12_DESCRIPTOR_RANGE, 3>& descriptorRanges)
{
	descriptorRanges[0] = CreateSrvDescriptorRange(0);
	descriptorRanges[1] = CreateSrvDescriptorRange(1);
	descriptorRanges[2] = CreateSrvDescriptorRange(2);
	return {
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_VERTEX),
		CreateDescriptorTableRootParameter(&descriptorRanges[0], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(2, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(3, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(4, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateDescriptorTableRootParameter(&descriptorRanges[1], 1, D3D12_SHADER_VISIBILITY_VERTEX),
		CreateDescriptorTableRootParameter(&descriptorRanges[2], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(5, D3D12_SHADER_VISIBILITY_PIXEL),
	};
}

std::array<D3D12_ROOT_PARAMETER, 3> CreateSpriteRootParameters(D3D12_DESCRIPTOR_RANGE& descriptorRange)
{
	descriptorRange = CreateSrvDescriptorRange(0);
	return {
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateDescriptorTableRootParameter(&descriptorRange, 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_VERTEX),
	};
}

std::array<D3D12_ROOT_PARAMETER, 3> CreateSkyboxRootParameters(D3D12_DESCRIPTOR_RANGE& descriptorRange)
{
	descriptorRange = CreateSrvDescriptorRange(0);
	return {
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_VERTEX),
		CreateDescriptorTableRootParameter(&descriptorRange, 1, D3D12_SHADER_VISIBILITY_PIXEL),
	};
}

std::array<D3D12_ROOT_PARAMETER, 1> CreateCopyImageRootParameters(D3D12_DESCRIPTOR_RANGE& descriptorRange)
{
	descriptorRange = CreateSrvDescriptorRange(0);
	return {
		CreateDescriptorTableRootParameter(&descriptorRange, 1, D3D12_SHADER_VISIBILITY_PIXEL),
	};
}

void CreateGraphicsPipelineStateFromDesc(
	Engine::Base::DirectXCommon* dxCommon,
	ID3D12RootSignature* rootSignature,
	const D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc,
	IDxcBlob* vertexShaderBlob,
	IDxcBlob* pixelShaderBlob,
	const D3D12_BLEND_DESC& blendDesc,
	const D3D12_RASTERIZER_DESC& rasterizerDesc,
	const D3D12_DEPTH_STENCIL_DESC& depthStencilDesc,
	D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopologyType,
	ID3D12PipelineState** pipelineState)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = rootSignature;
	pipelineDesc.InputLayout = inputLayoutDesc;
	pipelineDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
	pipelineDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
	pipelineDesc.BlendState = blendDesc;
	pipelineDesc.RasterizerState = rasterizerDesc;
	pipelineDesc.NumRenderTargets = 1;
	pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	pipelineDesc.PrimitiveTopologyType = primitiveTopologyType;
	pipelineDesc.SampleDesc.Count = 1;
	pipelineDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	pipelineDesc.DepthStencilState = depthStencilDesc;
	pipelineDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	HRESULT hr = dxCommon->GetDevice()->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(pipelineState));
	assert(SUCCEEDED(hr));
}

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
	CreateGraphicsPipelineStateFromDesc(dxCommon_, rootSignature.Get(), inputLayoutDesc, vertexshaderBlob,
		pixelShaderBlob, blendDesc, rasterizerDesc, depthStencilDesc,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, graphicsPipelineState.GetAddressOf());

}


void GraphicsPipeline::RootSignatureCreate()
{
	// 3D オブジェクト描画で使う CBV/SRV の並びをここで固定する
	std::array<D3D12_DESCRIPTOR_RANGE, 2> descriptorRanges{};
	const auto rootParameters = CreateObjectRootParameters(descriptorRanges);
	const D3D12_STATIC_SAMPLER_DESC staticSampler =
		CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	CreateRootSignatureWithParameters(dxCommon_, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		&staticSampler, 1, rootSignature.GetAddressOf());
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
	CreateGraphicsPipelineStateFromDesc(dxCommon_, rootSignatureParticle.Get(), inputLayoutDesc, vertexshaderBlob,
		pixelShaderBlob, blendDesc, rasterizerDesc, depthStencilDesc,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, graphicsPipelineStateParticle.GetAddressOf());

}

void GraphicsPipeline::RootSignatureParticleCreate()
{
	// パーティクルは material、instance SRV、texture SRV の最小構成で組む
	D3D12_DESCRIPTOR_RANGE descriptorRange{};
	const auto rootParameters = CreateParticleRootParameters(descriptorRange);
	const D3D12_STATIC_SAMPLER_DESC staticSampler =
		CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	CreateRootSignatureWithParameters(dxCommon_, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		&staticSampler, 1, rootSignatureParticle.GetAddressOf());
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
	CreateGraphicsPipelineStateFromDesc(dxCommon_, rootSignatureSprite.Get(), inputLayoutDesc, vertexshaderBlob,
		pixelShaderBlob, blendDesc, rasterizerDesc, depthStencilDesc,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, graphicsPipelineStateSprite.GetAddressOf());

}


void GraphicsPipeline::RootSignatureLineCreate()
{
	// ラインはカメラ CBV とインスタンス StructuredBuffer だけで描ける構成にする
	D3D12_DESCRIPTOR_RANGE descriptorRange{};
	const auto rootParameters = CreateLineRootParameters(descriptorRange);
	CreateRootSignatureWithParameters(dxCommon_, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		nullptr, 0, rootSignatureLine.GetAddressOf());
}

void GraphicsPipeline::RootSignatureSkinningCreate()
{
	std::array<D3D12_DESCRIPTOR_RANGE, 3> descriptorRanges{};
	const auto rootParameters = CreateSkinningRootParameters(descriptorRanges);
	const D3D12_STATIC_SAMPLER_DESC staticSampler =
		CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	CreateRootSignatureWithParameters(dxCommon_, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		&staticSampler, 1, rootSignatureSkinning.GetAddressOf());
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
	CreateGraphicsPipelineStateFromDesc(dxCommon_, rootSignatureSkinning.Get(), inputLayoutDesc, vertexshaderBlob,
		pixelShaderBlob, blendDesc, rasterizerDesc, depthStencilDesc,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, graphicsPipelineStateSkinning.GetAddressOf());
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
	CreateGraphicsPipelineStateFromDesc(dxCommon_, rootSignatureLine.Get(), inputLayoutDesc, vertexShaderBlob,
		pixelShaderBlob, blendDesc, rasterizerDesc, depthStencilDesc,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE, graphicsPipelineStateLine.GetAddressOf());
}




void GraphicsPipeline::RootSignatureSpriteCreate()
{
	D3D12_DESCRIPTOR_RANGE descriptorRange{};
	const auto rootParameters = CreateSpriteRootParameters(descriptorRange);
	const D3D12_STATIC_SAMPLER_DESC staticSampler =
		CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	CreateRootSignatureWithParameters(dxCommon_, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		&staticSampler, 1, rootSignatureSprite.GetAddressOf());
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
	CreateGraphicsPipelineStateFromDesc(dxCommon_, rootSignatureCopyImage.Get(), inputLayoutDesc,
		vertexShaderBlob, pixelShaderBlob, blendDesc, rasterizerDesc, depthStencilDesc,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, pso.GetAddressOf());

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
	D3D12_DESCRIPTOR_RANGE descriptorRange{};
	const auto rootParameters = CreateCopyImageRootParameters(descriptorRange);
	const D3D12_STATIC_SAMPLER_DESC staticSampler =
		CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	CreateRootSignatureWithParameters(dxCommon_, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		&staticSampler, 1, rootSignatureCopyImage.GetAddressOf());
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
	CreateGraphicsPipelineStateFromDesc(dxCommon_, rootSignatureSkybox.Get(), inputLayoutDesc, vertexshaderBlob,
		pixelShaderBlob, blendDesc, rasterizerDesc, depthStencilDesc,
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, graphicsPipelineStateSkybox.GetAddressOf());
}

void GraphicsPipeline::RootSignatureSkyboxCreate()
{
	D3D12_DESCRIPTOR_RANGE descriptorRange{};
	const auto rootParameters = CreateSkyboxRootParameters(descriptorRange);
	const D3D12_STATIC_SAMPLER_DESC staticSampler =
		CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	CreateRootSignatureWithParameters(dxCommon_, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		&staticSampler, 1, rootSignatureSkybox.GetAddressOf());
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
