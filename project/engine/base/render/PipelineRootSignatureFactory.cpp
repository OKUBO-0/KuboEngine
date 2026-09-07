#include "PipelineRootSignatureFactory.h"

#include "DirectXCommon.h"
#include "HResult.h"
#include "Logger.h"
#include <array>
#include <wrl.h>

namespace {

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

D3D12_STATIC_SAMPLER_DESC CreateShadowComparisonSamplerDesc()
{
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 1;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	return sampler;
}

void CreateRootSignatureFromDesc(
	Engine::Base::DirectXCommon& dxCommon,
	const D3D12_ROOT_SIGNATURE_DESC& descriptionRootSignature,
	ID3D12RootSignature** rootSignature)
{
	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(
		&descriptionRootSignature,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&signatureBlob,
		&errorBlob);
	if (FAILED(hr) && errorBlob) {
		Engine::Base::Logger::Log(static_cast<char*>(errorBlob->GetBufferPointer()));
	}
	Engine::Base::ThrowIfFailed(hr, "D3D12SerializeRootSignature");

	hr = dxCommon.GetDevice()->CreateRootSignature(
		0,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(rootSignature));
	Engine::Base::ThrowIfFailed(hr, "ID3D12Device::CreateRootSignature");
}

void CreateRootSignatureWithParameters(
	Engine::Base::DirectXCommon& dxCommon,
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
		CreateDescriptorTableRootParameter(&descriptorRanges[1], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(3, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateDescriptorTableRootParameter(&descriptorRanges[2], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(4, D3D12_SHADER_VISIBILITY_ALL),
	};
}

std::array<D3D12_ROOT_PARAMETER, 10> CreateObjectInstancingRootParameters(
	std::array<D3D12_DESCRIPTOR_RANGE, 4>& descriptorRanges)
{
	descriptorRanges[0] = CreateSrvDescriptorRange(0);
	descriptorRanges[1] = CreateSrvDescriptorRange(1);
	descriptorRanges[2] = CreateSrvDescriptorRange(2);
	descriptorRanges[3] = CreateSrvDescriptorRange(3);
	return {
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_VERTEX),
		CreateDescriptorTableRootParameter(&descriptorRanges[0], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(2, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateDescriptorTableRootParameter(&descriptorRanges[1], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(3, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateDescriptorTableRootParameter(&descriptorRanges[2], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(4, D3D12_SHADER_VISIBILITY_ALL),
		CreateDescriptorTableRootParameter(&descriptorRanges[3], 1, D3D12_SHADER_VISIBILITY_VERTEX),
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
	std::array<D3D12_DESCRIPTOR_RANGE, 4>& descriptorRanges)
{
	descriptorRanges[0] = CreateSrvDescriptorRange(0);
	descriptorRanges[1] = CreateSrvDescriptorRange(1);
	descriptorRanges[2] = CreateSrvDescriptorRange(2);
	descriptorRanges[3] = CreateSrvDescriptorRange(3);
	return {
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_VERTEX),
		CreateDescriptorTableRootParameter(&descriptorRanges[0], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(2, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateDescriptorTableRootParameter(&descriptorRanges[2], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(3, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateDescriptorTableRootParameter(&descriptorRanges[1], 1, D3D12_SHADER_VISIBILITY_VERTEX),
		CreateDescriptorTableRootParameter(&descriptorRanges[3], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(4, D3D12_SHADER_VISIBILITY_ALL),
	};
}

std::array<D3D12_ROOT_PARAMETER, 11> CreateSkinningInstancingRootParameters(
	std::array<D3D12_DESCRIPTOR_RANGE, 5>& descriptorRanges)
{
	descriptorRanges[0] = CreateSrvDescriptorRange(0);
	descriptorRanges[1] = CreateSrvDescriptorRange(1);
	descriptorRanges[2] = CreateSrvDescriptorRange(2);
	descriptorRanges[3] = CreateSrvDescriptorRange(3);
	descriptorRanges[4] = CreateSrvDescriptorRange(4);
	return {
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_VERTEX),
		CreateDescriptorTableRootParameter(&descriptorRanges[0], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(2, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateDescriptorTableRootParameter(&descriptorRanges[2], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(3, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateDescriptorTableRootParameter(&descriptorRanges[1], 1, D3D12_SHADER_VISIBILITY_VERTEX),
		CreateDescriptorTableRootParameter(&descriptorRanges[3], 1, D3D12_SHADER_VISIBILITY_PIXEL),
		CreateCbvRootParameter(4, D3D12_SHADER_VISIBILITY_ALL),
		CreateDescriptorTableRootParameter(&descriptorRanges[4], 1, D3D12_SHADER_VISIBILITY_VERTEX),
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

}

namespace Engine::Base {

void CreateObjectRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature)
{
	std::array<D3D12_DESCRIPTOR_RANGE, 3> descriptorRanges{};
	const auto rootParameters = CreateObjectRootParameters(descriptorRanges);
	const std::array<D3D12_STATIC_SAMPLER_DESC, 2> staticSamplers = {
		CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE_WRAP),
		CreateShadowComparisonSamplerDesc(),
	};
	CreateRootSignatureWithParameters(dxCommon, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		staticSamplers.data(), static_cast<UINT>(staticSamplers.size()), rootSignature);
}

void CreateObjectInstancingRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature)
{
	std::array<D3D12_DESCRIPTOR_RANGE, 4> descriptorRanges{};
	const auto rootParameters = CreateObjectInstancingRootParameters(descriptorRanges);
	const std::array<D3D12_STATIC_SAMPLER_DESC, 2> staticSamplers = {
		CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE_WRAP),
		CreateShadowComparisonSamplerDesc(),
	};
	CreateRootSignatureWithParameters(dxCommon, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		staticSamplers.data(), static_cast<UINT>(staticSamplers.size()), rootSignature);
}

void CreateParticleRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature)
{
	D3D12_DESCRIPTOR_RANGE descriptorRange{};
	const auto rootParameters = CreateParticleRootParameters(descriptorRange);
	const D3D12_STATIC_SAMPLER_DESC staticSampler =
		CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	CreateRootSignatureWithParameters(dxCommon, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		&staticSampler, 1, rootSignature);
}

void CreateLineRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature)
{
	D3D12_DESCRIPTOR_RANGE descriptorRange{};
	const auto rootParameters = CreateLineRootParameters(descriptorRange);
	CreateRootSignatureWithParameters(dxCommon, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		nullptr, 0, rootSignature);
}

void CreateSkinningRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature)
{
	std::array<D3D12_DESCRIPTOR_RANGE, 4> descriptorRanges{};
	const auto rootParameters = CreateSkinningRootParameters(descriptorRanges);
	const std::array<D3D12_STATIC_SAMPLER_DESC, 2> staticSamplers = {
		CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE_WRAP),
		CreateShadowComparisonSamplerDesc(),
	};
	CreateRootSignatureWithParameters(dxCommon, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		staticSamplers.data(), static_cast<UINT>(staticSamplers.size()), rootSignature);
}

void CreateSkinningInstancingRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature)
{
	std::array<D3D12_DESCRIPTOR_RANGE, 5> descriptorRanges{};
	const auto rootParameters = CreateSkinningInstancingRootParameters(descriptorRanges);
	const std::array<D3D12_STATIC_SAMPLER_DESC, 2> staticSamplers = {
		CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE_WRAP),
		CreateShadowComparisonSamplerDesc(),
	};
	CreateRootSignatureWithParameters(dxCommon, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		staticSamplers.data(), static_cast<UINT>(staticSamplers.size()), rootSignature);
}

void CreateShadowMapRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature)
{
	const std::array<D3D12_ROOT_PARAMETER, 2> rootParameters = {
		CreateCbvRootParameter(0, D3D12_SHADER_VISIBILITY_VERTEX),
		CreateCbvRootParameter(4, D3D12_SHADER_VISIBILITY_VERTEX),
	};
	CreateRootSignatureWithParameters(dxCommon, rootParameters.data(),
		static_cast<UINT>(rootParameters.size()), nullptr, 0, rootSignature);
}

void CreateSpriteRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature)
{
	D3D12_DESCRIPTOR_RANGE descriptorRange{};
	const auto rootParameters = CreateSpriteRootParameters(descriptorRange);
	const D3D12_STATIC_SAMPLER_DESC staticSampler =
		CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	CreateRootSignatureWithParameters(dxCommon, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		&staticSampler, 1, rootSignature);
}

void CreateCopyImageRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature)
{
	D3D12_DESCRIPTOR_RANGE descriptorRange{};
	const auto rootParameters = CreateCopyImageRootParameters(descriptorRange);
	const D3D12_STATIC_SAMPLER_DESC staticSampler =
		CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	CreateRootSignatureWithParameters(dxCommon, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		&staticSampler, 1, rootSignature);
}

void CreateSkyboxRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature)
{
	D3D12_DESCRIPTOR_RANGE descriptorRange{};
	const auto rootParameters = CreateSkyboxRootParameters(descriptorRange);
	const D3D12_STATIC_SAMPLER_DESC staticSampler =
		CreateLinearStaticSamplerDesc(D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	CreateRootSignatureWithParameters(dxCommon, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
		&staticSampler, 1, rootSignature);
}

}
