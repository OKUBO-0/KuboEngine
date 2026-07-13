#include "Model.h"
#include "DirectXCommon.h"
#include "ModelAssetResolver.h"
#include "ModelMeshConverter.h"
#include "HResult.h"
#include "ModelCommon.h"
#include "MyMath.h"
#include <Windows.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <fstream>
#include <limits>
#include <sstream>
#include <assert.h>
#include "TextureManager.h"
#include "SrvManager.h"

namespace {

Engine::Graphics3D::ModelLoadDiagnostics g_modelLoadDiagnostics{};
constexpr float kDefaultAnimationTicksPerSecond = 25.0f;

float ResolveAnimationTicksPerSecond(const aiAnimation& animation)
{
    const float ticksPerSecond =
        static_cast<float>(animation.mTicksPerSecond);
    return std::isfinite(ticksPerSecond) && ticksPerSecond > 0.0f
        ? ticksPerSecond
        : kDefaultAnimationTicksPerSecond;
}

void FailModelLoad(const std::string& operation, const std::string& path, const std::string& detail)
{
    std::ostringstream message;
    message << "[Model::" << operation << "] " << detail
        << " path=\"" << path << "\"\n";
    OutputDebugStringA(message.str().c_str());
    assert(false && "Model load failed; see OutputDebugString for path and Assimp details");
    std::abort();
}

void LogSkippedNonTriangleFace(const char* meshName, uint32_t faceIndex, uint32_t indexCount)
{
    ++g_modelLoadDiagnostics.skippedNonTriangleFaceCount;
    std::ostringstream message;
    message << "[Model::LoadIndicesFromMesh] skipped non-triangle face"
        << " mesh=\"" << (meshName ? meshName : "")
        << "\" face=" << faceIndex
        << " indices=" << indexCount << "\n";
    OutputDebugStringA(message.str().c_str());
}

void LogMeshFallback(const std::string& path, uint32_t meshIndex, const char* detail)
{
    ++g_modelLoadDiagnostics.meshFallbackCount;
    std::ostringstream message;
    message << "[Model::LoadModelFile] " << detail
        << " path=\"" << path
        << "\" meshIndex=" << meshIndex << "\n";
    OutputDebugStringA(message.str().c_str());
}

}

namespace Engine::Graphics3D {

ModelLoadDiagnostics Model::GetLoadDiagnostics()
{
    return g_modelLoadDiagnostics;
}

void Model::Initialize(
	ModelCommon* modelCommon,
	const std::string& directorypath,
	const std::string& filename,
	bool loadMaterialTexture)
{
	modelCommon_ = modelCommon;
	LoadRuntimeAssets(directorypath, filename);
	CreateVertexBuffer();
	CreateIndexBuffer();
	CreateMaterialBuffer();
	if (loadMaterialTexture) {
		LoadMaterialTexture();
	}
}

void Model::Finalize()
{
	if (modelCommon_ && modelCommon_->GetDxCommon()) {
		auto dxCommon = modelCommon_->GetDxCommon();
		dxCommon->UntrackResourceState(vertexResource.Get());
		dxCommon->UntrackResourceState(indexResource.Get());
		dxCommon->UntrackResourceState(skinCluster.influenceResource.Get());
	}
	vertexResource.Reset();
	indexResource.Reset();
	skinCluster.influenceResource.Reset();
}

void Model::LoadRuntimeAssets(const std::string& directorypath, const std::string& filename)
{
	// モデルデータ・アニメーション・スケルトン・スキンクラスターを読み込み/生成
	modelData = LoadModelFile(directorypath, filename);
	animation = LoadAnimationFile(directorypath, filename);
	skeleton = CreateSkeleton(modelData.rootNode);
	skinCluster = CreateSkinCluster();
}

void Model::CreateVertexBuffer()
{
	// 頂点バッファ生成とデータ転送
	const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
		modelCommon_->GetDxCommon();
	const size_t vertexBytes = sizeof(VertexData) * modelData.vertices.size();
	vertexResource = dxCommon->CreateDefaultBufferResource(
		modelData.vertices.data(),
		vertexBytes,
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = static_cast<UINT>(vertexBytes);
	vertexBufferView.StrideInBytes = sizeof(VertexData);
}

void Model::CreateIndexBuffer()
{
	// インデックスバッファ生成とデータ転送
	const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
		modelCommon_->GetDxCommon();
	const size_t indexBytes = sizeof(uint32_t) * modelData.indices.size();
	indexResource = dxCommon->CreateDefaultBufferResource(
		modelData.indices.data(),
		indexBytes,
		D3D12_RESOURCE_STATE_INDEX_BUFFER);
	indexBufferView.BufferLocation = indexResource->GetGPUVirtualAddress();
	indexBufferView.SizeInBytes = static_cast<UINT>(indexBytes);
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;
}

void Model::CreateMaterialBuffer()
{
	materialData_.color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData_.enableLighting = true;
	materialData_.uvTransform = materialData_.uvTransform.MakeIdentity4x4();
	materialData_.shininess = 60.0f;
}

void Model::LoadMaterialTexture()
{
	// テクスチャ読み込みとインデックス取得
	Engine::Base::TextureManager::GetInstance()->LoadTexture(modelData.material.textureFilePath);
	modelData.material.textureIndex = Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(modelData.material.textureFilePath);
}

void Model::Draw(D3D12_GPU_VIRTUAL_ADDRESS materialAddress)
{
	const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
		modelCommon_->GetDxCommon();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
	if (materialAddress == 0) {
		const Engine::Base::DirectXCommon::FrameUploadAllocation materialAllocation =
			dxCommon->AllocateFrameUpload(sizeof(Material), 256);
		std::memcpy(
			materialAllocation.cpuAddress,
			&materialData_,
			sizeof(materialData_));
		materialAddress = materialAllocation.gpuAddress;
	}

	// 頂点バッファビュー（通常頂点 + スキンクラスター影響情報）
	D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {
		vertexBufferView,
		skinCluster.influenceBufferView
	};

	// 頂点バッファ設定
	commandList->IASetVertexBuffers(0, 2, vbvs);

	// インデックスバッファ設定
	commandList->IASetIndexBuffer(&indexBufferView);

	// マテリアルCBV設定
	commandList->SetGraphicsRootConstantBufferView(
		0,
		materialAddress);

	// テクスチャSRV設定
	modelCommon_->GetSRVManager()->SetGraphicsRootDescriptorTable(2, Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(modelData.material.textureFilePath));

	// インデックス付き描画（インスタンス数 = 1）
	commandList->DrawIndexedInstanced(
		static_cast<UINT>(modelData.indices.size()), // インデックス数
		1,  // インスタンス数
		0,  // 開始インデックス
		0,  // 基準頂点
		0   // 開始インスタンス
	);
}

void Model::DrawGeometry()
{
	const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
		modelCommon_->GetDxCommon();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
	D3D12_VERTEX_BUFFER_VIEW vertexView = vertexBufferView;
	commandList->IASetVertexBuffers(0, 1, &vertexView);
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->DrawIndexedInstanced(
		static_cast<UINT>(modelData.indices.size()), 1, 0, 0, 0);
}

Node Model::ReadNode(aiNode* node)
{
	Node result;
	aiVector3D scale, translate;
	aiQuaternion rotation;

	// ノード変換を分解（スケール・回転・平行移動）
	node->mTransformation.Decompose(scale, rotation, translate);
	result.transform.scale = { scale.x, scale.y, scale.z };
	result.transform.rotate = { rotation.x, -rotation.y, -rotation.z, rotation.w };
	result.transform.translate = { translate.x, translate.y, translate.z };

	// ローカル行列生成
	result.localMatrix = MyMath::MakeAffineMatrix(result.transform.scale, result.transform.rotate, result.transform.translate);

	// ノード名と子ノード読み込み
	result.name = node->mName.C_Str();
	result.children.resize(node->mNumChildren);
	for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
		result.children[childIndex] = ReadNode(node->mChildren[childIndex]);
	}
	return result;
}

Skeleton Model::CreateSkeleton(const Node& rootNode)
{
	Skeleton skeleton;

	// ルートノードからジョイントツリー構築
	skeleton.root = CreateJoint(rootNode, {}, skeleton.joints);

	// 名前とインデックスのマッピング
	for (const Joint& joint : skeleton.joints) {
		skeleton.jointMap.emplace(joint.name, joint.index);
	}
	return skeleton;
}

int32_t Model::CreateJoint(const Node& node, std::optional<int32_t> parent, std::vector<Joint>& joints)
{
	Joint joint;
	joint.name = node.name;
	joint.localMatrix = node.localMatrix;
	joint.skeletonSpaceMatrix = joint.skeletonSpaceMatrix.MakeIdentity4x4();
	joint.transform = node.transform;
	joint.index = int32_t(joints.size());
	joint.parent = parent;

	joints.push_back(joint);

	// 子ジョイントを再帰的に生成
	for (const Node& child : node.children) {
		int32_t childIndex = CreateJoint(child, joint.index, joints);
		joints[joint.index].children.push_back(childIndex);
	}
	return joint.index;
}

MaterialData Model::LoadMaterialTemplateFile(const std::string& directorypath, const std::string& filename)
{
    MaterialData materialData; // 構築するマテリアルデータ
    std::string line;          // ファイルから読み込む1行
    std::ifstream file(directorypath + "/" + filename); // ファイルを開く
    assert(file.is_open());    // 開けなかった場合は停止

    while (std::getline(file, line)) {
        std::string identifier;
        std::stringstream s(line);
        s >> identifier;

        // マテリアルファイルの識別子に応じた処理
        if (identifier == "map_Kd") {
            std::string textureFilename;
            s >> textureFilename;
            materialData.textureFilePath = ResolveModelResourcePath(directorypath, textureFilename);
        }
    }
    return materialData;
}

ModelData Model::LoadModelFile(const std::string& directoryPath, const std::string& filename)
{
    ModelData modelData; // 構築するモデルデータ
    Assimp::Importer importer;
    std::string path = ResolveModelAssetPath(directoryPath, filename);

    // 頂点、法線、UV を揃えたうえでエンジン形式へ変換する
    // Assimpでモデルファイル読み込み
    const aiScene* scene = importer.ReadFile(path.c_str(), aiProcess_Triangulate | aiProcess_FlipWindingOrder | aiProcess_FlipUVs);
    if (scene == nullptr) {
        FailModelLoad("LoadModelFile", path, std::string("Assimp::ReadFile failed: ") + importer.GetErrorString());
    }
    if (!scene->HasMeshes()) {
        FailModelLoad("LoadModelFile", path, "model has no meshes");
    }

    size_t totalVertexCount = 0;
    size_t maximumIndexCount = 0;
    for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        totalVertexCount += mesh->mNumVertices;
        maximumIndexCount += static_cast<size_t>(mesh->mNumFaces) * 3;
    }
    if (totalVertexCount > (std::numeric_limits<uint32_t>::max)()) {
        FailModelLoad(
            "LoadModelFile",
            path,
            "model vertex count exceeds 32-bit index range");
    }
    modelData.vertices.reserve(totalVertexCount);
    modelData.indices.reserve(maximumIndexCount);

    // メッシュごとの処理
    for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        aiMesh* mesh = scene->mMeshes[meshIndex];
        if (!mesh->HasNormals()) {
            LogMeshFallback(path, meshIndex, "mesh has no normals; using fallback normal");
        }
        if (!mesh->HasTextureCoords(0)) {
            LogMeshFallback(path, meshIndex, "mesh has no texture coordinates; using fallback uv");
        }

        const uint32_t baseVertex =
            static_cast<uint32_t>(modelData.vertices.size());
        LoadVerticesFromMesh(mesh, modelData);
        LoadIndicesFromMesh(mesh, baseVertex, modelData);
        LoadSkinClusterDataFromMesh(mesh, baseVertex, modelData);
    }
    CalculateBounds(modelData);

    // 先頭のディフューズテクスチャをモデル側の代表マテリアルとして採用する
    // マテリアル解析
    LoadMaterialFromScene(scene, directoryPath, modelData);

    // ルートノード読み込み
    modelData.rootNode = ReadNode(scene->mRootNode);

    return modelData;
}

Animation Model::LoadAnimationFile(const std::string& directoryPath, const std::string& filename)
{
    Animation animation;
    Assimp::Importer importer;
    std::string filepath = ResolveModelAssetPath(directoryPath, filename);
    const aiScene* scene = importer.ReadFile(filepath.c_str(), 0);
    if (scene == nullptr) {
        FailModelLoad("LoadAnimationFile", filepath, std::string("Assimp::ReadFile failed: ") + importer.GetErrorString());
    }

    // アニメーションが存在しない場合は空を返す
    if (scene->mNumAnimations == 0) {
        return animation;
    }

    // 現状は最初の 1 クリップだけを読み込んで再生対象にしている
    aiAnimation* animationAssimp = scene->mAnimations[0]; // 最初のアニメーションのみ採用
    const float ticksPerSecond =
        ResolveAnimationTicksPerSecond(*animationAssimp);
    const float duration =
        static_cast<float>(animationAssimp->mDuration) /
        ticksPerSecond;
    animation.duration =
        std::isfinite(duration) && duration >= 0.0f
        ? duration
        : 0.0f;
    LoadAnimationChannels(animationAssimp, animation);
    return animation;
}

void Model::LoadVerticesFromMesh(aiMesh* mesh, ModelData& modelData)
{
    AppendVerticesFromMesh(*mesh, modelData);
}

void Model::CalculateBounds(ModelData& modelData)
{
    CalculateModelBounds(modelData);
}

void Model::LoadIndicesFromMesh(
    aiMesh* mesh,
    uint32_t baseVertex,
    ModelData& modelData)
{
    AppendIndicesFromMesh(*mesh, baseVertex, modelData, LogSkippedNonTriangleFace);
}

void Model::LoadSkinClusterDataFromMesh(
    aiMesh* mesh,
    uint32_t baseVertex,
    ModelData& modelData)
{
    AppendSkinClusterDataFromMesh(*mesh, baseVertex, modelData);
}

void Model::LoadMaterialFromScene(const aiScene* scene, const std::string& directoryPath, ModelData& modelData)
{
    for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
        aiMaterial* material = scene->mMaterials[materialIndex];
        if (material->GetTextureCount(aiTextureType_DIFFUSE) == 0) {
            continue;
        }

        aiString texturePath;
        material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath);
        modelData.material.textureFilePath = ResolveModelResourcePath(directoryPath, texturePath.C_Str());
    }
}

SkinCluster Model::CreateSkinCluster()
{
    SkinCluster skinCluster;
    InitializeInfluenceResources(skinCluster);
    InitializeInverseBindPoseMatrices(skinCluster);
    ApplyJointWeightsToSkinCluster(skinCluster);
    return skinCluster;
}

void Model::LoadAnimationChannels(const aiAnimation* animationAssimp, Animation& animation)
{
    const float ticksPerSecond =
        ResolveAnimationTicksPerSecond(*animationAssimp);
    for (uint32_t channelIndex = 0; channelIndex < animationAssimp->mNumChannels; ++channelIndex) {
        aiNodeAnim* nodeAnimationAssimp = animationAssimp->mChannels[channelIndex];
        NodeAnimation& nodeAnimation = animation.nodeAnimations[nodeAnimationAssimp->mNodeName.C_Str()];
        LoadTranslateKeys(nodeAnimationAssimp, ticksPerSecond, nodeAnimation);
        LoadRotateKeys(nodeAnimationAssimp, ticksPerSecond, nodeAnimation);
        LoadScaleKeys(nodeAnimationAssimp, ticksPerSecond, nodeAnimation);
    }
}

void Model::LoadTranslateKeys(
    const aiNodeAnim* nodeAnimationAssimp,
    float ticksPerSecond,
    NodeAnimation& nodeAnimation)
{
    for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumPositionKeys; ++keyIndex) {
        aiVectorKey& keyAssimp = nodeAnimationAssimp->mPositionKeys[keyIndex];
        KeyframeVector3 keyframe;
        keyframe.time = float(keyAssimp.mTime / ticksPerSecond);
        keyframe.value = { -keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z };
        nodeAnimation.translate.push_back(keyframe);
    }
}

void Model::LoadRotateKeys(
    const aiNodeAnim* nodeAnimationAssimp,
    float ticksPerSecond,
    NodeAnimation& nodeAnimation)
{
    for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumRotationKeys; ++keyIndex) {
        aiQuatKey& keyAssimp = nodeAnimationAssimp->mRotationKeys[keyIndex];
        KeyframeQuaternion keyframe;
        keyframe.time = float(keyAssimp.mTime / ticksPerSecond);
        keyframe.value = { keyAssimp.mValue.x, -keyAssimp.mValue.y, -keyAssimp.mValue.z, keyAssimp.mValue.w };
        nodeAnimation.rotate.push_back(keyframe);
    }
}

void Model::LoadScaleKeys(
    const aiNodeAnim* nodeAnimationAssimp,
    float ticksPerSecond,
    NodeAnimation& nodeAnimation)
{
    for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumScalingKeys; ++keyIndex) {
        aiVectorKey& keyAssimp = nodeAnimationAssimp->mScalingKeys[keyIndex];
        KeyframeVector3 keyframe;
        keyframe.time = float(keyAssimp.mTime / ticksPerSecond);
        keyframe.value = { keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z };
        nodeAnimation.scale.push_back(keyframe);
    }
}

void Model::InitializeInfluenceResources(SkinCluster& skinCluster)
{
    skinCluster.influenceData.resize(modelData.vertices.size());
}

void Model::InitializeInverseBindPoseMatrices(SkinCluster& skinCluster)
{
    skinCluster.inverseBindPoseMatrices.resize(skeleton.joints.size());
    std::generate(
        skinCluster.inverseBindPoseMatrices.begin(),
        skinCluster.inverseBindPoseMatrices.end(),
        [] { return MyMath::MakeIdentity4x4(); });
}

void Model::ApplyJointWeightsToSkinCluster(SkinCluster& skinCluster)
{
    for (const auto& jointWeight : modelData.skinClusterData) {
        auto it = skeleton.jointMap.find(jointWeight.first);
        if (it == skeleton.jointMap.end()) {
            continue;
        }

        const uint32_t jointIndex = (*it).second;
        skinCluster.inverseBindPoseMatrices[jointIndex] = jointWeight.second.inverseBindPoseMatrix;
        for (const auto& vertexWeight : jointWeight.second.vertexWeights) {
            auto& currentInfluence = skinCluster.influenceData[vertexWeight.vectorIndex];
            for (uint32_t index = 0; index < kNumMaxInfluence; ++index) {
                if (currentInfluence.weights[index] == 0.0f) {
                    currentInfluence.weights[index] = vertexWeight.weight;
                    currentInfluence.jointIndices[index] = jointIndex;
                    break;
                }
            }
        }
    }

    const size_t influenceBytes =
        sizeof(VertexInfluence) * skinCluster.influenceData.size();
    const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
        modelCommon_->GetDxCommon();
    skinCluster.influenceResource =
        dxCommon->CreateDefaultBufferResource(
            skinCluster.influenceData.data(),
            influenceBytes,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    skinCluster.influenceBufferView.BufferLocation =
        skinCluster.influenceResource->GetGPUVirtualAddress();
    skinCluster.influenceBufferView.SizeInBytes =
        static_cast<UINT>(influenceBytes);
    skinCluster.influenceBufferView.StrideInBytes = sizeof(VertexInfluence);
    std::vector<VertexInfluence>().swap(skinCluster.influenceData);
}

}
