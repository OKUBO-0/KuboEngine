#pragma once

#include <d3d12.h>

namespace Engine::Base {

class DirectXCommon;

void CreateObjectRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature);
void CreateObjectInstancingRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature);
void CreateParticleRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature);
void CreateLineRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature);
void CreateSkinningRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature);
void CreateSkinningInstancingRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature);
void CreateShadowMapRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature);
void CreateSpriteRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature);
void CreateCopyImageRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature);
void CreateSkyboxRootSignature(DirectXCommon& dxCommon, ID3D12RootSignature** rootSignature);

}
