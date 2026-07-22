#include "PipelineStateBuilder.h"

#include "DirectXCommon.h"
#include "HResult.h"
#include <cassert>

namespace Engine::Base {

void CreateGraphicsPipelineState(
	DirectXCommon& dxCommon,
	const GraphicsPipelineStateRequest& request,
	ID3D12PipelineState** pipelineState)
{
	assert(request.rootSignature != nullptr);
	assert(request.vertexShader != nullptr);
	assert(pipelineState != nullptr);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = request.rootSignature;
	pipelineDesc.InputLayout = request.inputLayout;
	pipelineDesc.VS = {
		request.vertexShader->GetBufferPointer(),
		request.vertexShader->GetBufferSize(),
	};
	if (request.pixelShader != nullptr) {
		pipelineDesc.PS = {
			request.pixelShader->GetBufferPointer(),
			request.pixelShader->GetBufferSize(),
		};
	}
	pipelineDesc.BlendState = request.blendState;
	pipelineDesc.RasterizerState = request.rasterizerState;
	pipelineDesc.NumRenderTargets = request.renderTargetCount;
	if (request.renderTargetCount > 0) {
		pipelineDesc.RTVFormats[0] = request.renderTargetFormat;
	}
	pipelineDesc.PrimitiveTopologyType = request.primitiveTopologyType;
	pipelineDesc.SampleDesc.Count = 1;
	pipelineDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	pipelineDesc.DepthStencilState = request.depthStencilState;
	pipelineDesc.DSVFormat = request.depthStencilFormat;

	const HRESULT hr = dxCommon.GetDevice()->CreateGraphicsPipelineState(
		&pipelineDesc,
		IID_PPV_ARGS(pipelineState));
	ThrowIfFailed(hr, request.failureContext);
}

}
