#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxcapi.h>

namespace Engine::Base {

class DirectXCommon;

struct GraphicsPipelineStateRequest
{
	ID3D12RootSignature* rootSignature = nullptr;
	D3D12_INPUT_LAYOUT_DESC inputLayout{};
	IDxcBlob* vertexShader = nullptr;
	IDxcBlob* pixelShader = nullptr;
	D3D12_BLEND_DESC blendState{};
	D3D12_RASTERIZER_DESC rasterizerState{};
	D3D12_DEPTH_STENCIL_DESC depthStencilState{};
	D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	UINT renderTargetCount = 1;
	DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	const char* failureContext = "ID3D12Device::CreateGraphicsPipelineState";
};

void CreateGraphicsPipelineState(
	DirectXCommon* dxCommon,
	const GraphicsPipelineStateRequest& request,
	ID3D12PipelineState** pipelineState);

}
