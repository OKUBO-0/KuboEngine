#include "SkyBox.h"
#include "TextureManager.h"
#include "CameraManager.h"
#include <imgui.h>

namespace {

constexpr std::array<std::array<Vector4, 4>, 6> kSkyBoxFacePositions = {{
	{ Vector4{ 1.0f, 1.0f, -1.0f, 1.0f }, Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }, Vector4{ 1.0f, -1.0f, -1.0f, 1.0f }, Vector4{ 1.0f, -1.0f, 1.0f, 1.0f } },
	{ Vector4{ -1.0f, 1.0f, 1.0f, 1.0f }, Vector4{ -1.0f, 1.0f, -1.0f, 1.0f }, Vector4{ -1.0f, -1.0f, 1.0f, 1.0f }, Vector4{ -1.0f, -1.0f, -1.0f, 1.0f } },
	{ Vector4{ -1.0f, 1.0f, 1.0f, 1.0f }, Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }, Vector4{ -1.0f, -1.0f, 1.0f, 1.0f }, Vector4{ 1.0f, -1.0f, 1.0f, 1.0f } },
	{ Vector4{ 1.0f, 1.0f, -1.0f, 1.0f }, Vector4{ -1.0f, 1.0f, -1.0f, 1.0f }, Vector4{ 1.0f, -1.0f, -1.0f, 1.0f }, Vector4{ -1.0f, -1.0f, -1.0f, 1.0f } },
	{ Vector4{ -1.0f, 1.0f, -1.0f, 1.0f }, Vector4{ 1.0f, 1.0f, -1.0f, 1.0f }, Vector4{ -1.0f, 1.0f, 1.0f, 1.0f }, Vector4{ 1.0f, 1.0f, 1.0f, 1.0f } },
	{ Vector4{ -1.0f, -1.0f, 1.0f, 1.0f }, Vector4{ 1.0f, -1.0f, 1.0f, 1.0f }, Vector4{ -1.0f, -1.0f, -1.0f, 1.0f }, Vector4{ 1.0f, -1.0f, -1.0f, 1.0f } },
}};

}

namespace Engine::Skybox {

SkyBox::~SkyBox()
{

}

void SkyBox::Initialize(const std::string& textureFilePath)
{
	dxCommon_ = SkyBoxCommon::GetInstance()->GetDxCommon();// DirectXの共通処理クラス
	srvManager_ = SkyBoxCommon::GetInstance()->GetSrvManager();// SRV管理クラス
	textureFilePath_ = textureFilePath;// テクスチャファイルパス

	InitializeGeometry();
	InitializeVertexBuffer();
	InitializeIndexBuffer();
	InitializeTexture();
	InitializeMaterial();
	InitializeTransformBuffer();

	// Transform 変数
	transform = { {30.0f,30.0f,30.0f},{0.0f,0.0f,0.0f} ,{0.0f,0.0f,0.0f} };
}

void SkyBox::InitializeGeometry()
{
	vertices.resize(24);
	for (size_t faceIndex = 0; faceIndex < kSkyBoxFacePositions.size(); ++faceIndex) {
		InitializeFaceVertices(faceIndex * 4, kSkyBoxFacePositions[faceIndex]);
	}
	InitializeIndices();
}

void SkyBox::InitializeFaceVertices(size_t startIndex, const std::array<Vector4, 4>& positions)
{
	for (size_t i = 0; i < positions.size(); ++i) {
		vertices[startIndex + i].position = positions[i];
	}
}

void SkyBox::InitializeIndices()
{
	indices = {
		0, 1, 2, 2, 1, 3,
		4, 5, 6, 6, 5, 7,
		8, 9, 10, 10, 9, 11,
		12, 13, 14, 14, 13, 15,
		16, 17, 18, 18, 17, 19,
		20, 21, 22, 22, 21, 23,
	};
}

void SkyBox::InitializeVertexBuffer()
{
	vertexResource = dxCommon_->CreateBufferResource(sizeof(VertexData) * vertices.size());
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * vertices.size());
	vertexBufferView.StrideInBytes = sizeof(VertexData);
	VertexData* vertexData = nullptr;
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, vertices.data(), sizeof(VertexData) * vertices.size());
	vertexResource->Unmap(0, nullptr);
}

void SkyBox::InitializeIndexBuffer()
{
	indexResource = dxCommon_->CreateBufferResource(sizeof(uint16_t) * indices.size());
	indexBufferView.BufferLocation = indexResource->GetGPUVirtualAddress();
	indexBufferView.Format = DXGI_FORMAT_R16_UINT;
	indexBufferView.SizeInBytes = static_cast<UINT>(sizeof(uint16_t) * indices.size());
	indexResource->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
	std::memcpy(indexData, indices.data(), sizeof(uint16_t) * indices.size());
	indexResource->Unmap(0, nullptr);
}

void SkyBox::InitializeTexture()
{
	Engine::Base::TextureManager::GetInstance()->LoadTexture(textureFilePath_);//テクスチャファイルの読み込み
	textureIndex_ = Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(textureFilePath_);	//テクスチャ番号の取得
}

void SkyBox::InitializeMaterial()
{
	materialResource = dxCommon_->CreateBufferResource(sizeof(Material));
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	materialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	materialData->enableLighting = false;//有効にするか否か
	materialData->uvTransform = materialData->uvTransform.MakeIdentity4x4();
}

void SkyBox::InitializeTransformBuffer()
{
	transformationMatrixResource = SkyBoxCommon::GetInstance()->GetDxCommon()->CreateBufferResource(sizeof(TransformationMatrix));
	transformationMatrixResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData_));
	transformationMatrixData_->WVP = transformationMatrixData_->WVP.MakeIdentity4x4();
	transformationMatrixData_->World = transformationMatrixData_->World.MakeIdentity4x4();
	transformationMatrixData_->worldInverseTranspose = transformationMatrixData_->worldInverseTranspose.MakeIdentity4x4();
}

void SkyBox::Update()
{
	worldMatrix = MyMath::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	Engine::CameraSystem::Camera* activeCamera = Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();

	if (activeCamera) {

		const Matrix4x4& viewProjectionMatrix = activeCamera->GetViewProjectionMatrix();
		worldViewProjectionMatrix = worldMatrix * viewProjectionMatrix;
		transformationMatrixData_->WVP = worldViewProjectionMatrix;
		transformationMatrixData_->World = worldMatrix;

	} else {
		worldViewProjectionMatrix = worldMatrix;
		transformationMatrixData_->WVP = worldViewProjectionMatrix;
		transformationMatrixData_->World = worldMatrix;
	}
}

void SkyBox::Draw()
{
	SkyBoxCommon::GetInstance()->commonDraw();//共通描画処理を呼び出す
	//頂点バッファビューをセット
	SkyBoxCommon::GetInstance()->GetDxCommon()->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView);
	//Materialをセット
	SkyBoxCommon::GetInstance()->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
	//トランスフォームをセット
	SkyBoxCommon::GetInstance()->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(1, transformationMatrixResource->GetGPUVirtualAddress());
	//テクスチャをセット
	SkyBoxCommon::GetInstance()->GetSrvManager()->SetGraficsRootDescriptorTable(2, Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(textureFilePath_));
	//インデックスバッファビューをセット
	SkyBoxCommon::GetInstance()->GetDxCommon()->GetCommandList()->IASetIndexBuffer(&indexBufferView);
	//描画
	SkyBoxCommon::GetInstance()->GetDxCommon()->GetCommandList()->DrawIndexedInstanced(static_cast<UINT>(indices.size()), 1, 0, 0, 0);
}

void SkyBox::DrawImGuiDebug()
{
#ifdef _DEBUG

	// ImGuiのデバッグウィンドウを表示
	if (ImGui::Begin("SkyBox Debug")) {

		//transformの調整
		ImGui::Text("SkyBox Transform");
		ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, 0.0f, 10.0f);




		ImGui::End();
	}




#endif // _DEBUG
}

}
