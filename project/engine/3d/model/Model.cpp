#include "Model.h"
#include "DirectXCommon.h"
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

std::string ResolveModelFilePath(const std::string& directoryPath, const std::string& filename)
{
    const std::filesystem::path resourceRoot = directoryPath;
    const std::filesystem::path resourceDirectPath = resourceRoot / filename;
    if (std::filesystem::exists(resourceDirectPath)) {
        return resourceDirectPath.generic_string();
    }

    const std::filesystem::path modelRoot = std::filesystem::path(directoryPath) / "models";
    const std::filesystem::path directPath = modelRoot / filename;
    if (std::filesystem::exists(directPath)) {
        return directPath.generic_string();
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(modelRoot)) {
        if (entry.is_regular_file() && entry.path().filename() == filename) {
            return entry.path().generic_string();
        }
    }

    return directPath.generic_string();
}

std::string ResolveResourceFilePath(const std::string& directoryPath, const std::string& filename)
{
    const std::filesystem::path resourceRoot = directoryPath;
    const std::filesystem::path directPath = resourceRoot / filename;
    if (std::filesystem::exists(directPath)) {
        return directPath.generic_string();
    }

    const std::filesystem::path targetFilename = std::filesystem::path(filename).filename();
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(resourceRoot)) {
        if (entry.is_regular_file() && entry.path().filename() == targetFilename) {
            return entry.path().generic_string();
        }
    }

    return directPath.generic_string();
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
	const size_t vertexBytes = sizeof(VertexData) * modelData.vertices.size();
	vertexResource = modelCommon_->GetDxCommon()->CreateDefaultBufferResource(
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
	const size_t indexBytes = sizeof(uint32_t) * modelData.indices.size();
	indexResource = modelCommon_->GetDxCommon()->CreateDefaultBufferResource(
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
	if (materialAddress == 0) {
		const Engine::Base::DirectXCommon::FrameUploadAllocation materialAllocation =
			modelCommon_->GetDxCommon()->AllocateFrameUpload(sizeof(Material), 256);
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
	modelCommon_->GetDxCommon()->GetCommandList()->IASetVertexBuffers(0, 2, vbvs);

	// インデックスバッファ設定
	modelCommon_->GetDxCommon()->GetCommandList()->IASetIndexBuffer(&indexBufferView);

	// マテリアルCBV設定
	modelCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(
		0,
		materialAddress);

	// テクスチャSRV設定
	modelCommon_->GetSRVManager()->SetGraphicsRootDescriptorTable(2, Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(modelData.material.textureFilePath));

	// インデックス付き描画（インスタンス数 = 1）
	modelCommon_->GetDxCommon()->GetCommandList()->DrawIndexedInstanced(
		static_cast<UINT>(modelData.indices.size()), // インデックス数
		1,  // インスタンス数
		0,  // 開始インデックス
		0,  // 基準頂点
		0   // 開始インスタンス
	);
}

void Model::DrawGeometry()
{
	D3D12_VERTEX_BUFFER_VIEW vertexView = vertexBufferView;
	modelCommon_->GetDxCommon()->GetCommandList()->IASetVertexBuffers(0, 1, &vertexView);
	modelCommon_->GetDxCommon()->GetCommandList()->IASetIndexBuffer(&indexBufferView);
	modelCommon_->GetDxCommon()->GetCommandList()->DrawIndexedInstanced(
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
            materialData.textureFilePath = ResolveResourceFilePath(directorypath, textureFilename);
        }
    }
    return materialData;
}

ModelData Model::LoadModelFile(const std::string& directoryPath, const std::string& filename)
{
    ModelData modelData; // 構築するモデルデータ
    Assimp::Importer importer;
    std::string path = ResolveModelFilePath(directoryPath, filename);

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
    std::string filepath = ResolveModelFilePath(directoryPath, filename);
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
    const size_t baseVertex = modelData.vertices.size();
    modelData.vertices.resize(baseVertex + mesh->mNumVertices);

    // 頂点情報の読み込み
    for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
        aiVector3D& position = mesh->mVertices[vertexIndex];
        const aiVector3D normal = mesh->HasNormals() ? mesh->mNormals[vertexIndex] : aiVector3D{ 0.0f, 1.0f, 0.0f };
        const aiVector3D texcoord = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][vertexIndex] : aiVector3D{ 0.0f, 0.0f, 0.0f };

        // 右手系 → 左手系変換
        VertexData& vertex = modelData.vertices[baseVertex + vertexIndex];
        vertex.position = { -position.x, position.y, position.z, 1.0f };
        vertex.normal = { -normal.x, normal.y, normal.z };
        vertex.texcoord = { texcoord.x, texcoord.y };
    }
}

void Model::CalculateBounds(ModelData& modelData)
{
    if (modelData.vertices.empty()) {
        modelData.localAabbMin = { 0.0f, 0.0f, 0.0f };
        modelData.localAabbMax = { 0.0f, 0.0f, 0.0f };
        modelData.localAabbCenter = { 0.0f, 0.0f, 0.0f };
        modelData.localBoundingRadius = 1.0f;
        modelData.hasBounds = false;
        return;
    }

    Vector3 min{
        modelData.vertices.front().position.x,
        modelData.vertices.front().position.y,
        modelData.vertices.front().position.z,
    };
    Vector3 max = min;
    for (const VertexData& vertex : modelData.vertices) {
        min.x = (std::min)(min.x, vertex.position.x);
        min.y = (std::min)(min.y, vertex.position.y);
        min.z = (std::min)(min.z, vertex.position.z);
        max.x = (std::max)(max.x, vertex.position.x);
        max.y = (std::max)(max.y, vertex.position.y);
        max.z = (std::max)(max.z, vertex.position.z);
    }

    const Vector3 center{
        (min.x + max.x) * 0.5f,
        (min.y + max.y) * 0.5f,
        (min.z + max.z) * 0.5f,
    };
    float radiusSq = 0.0f;
    for (const VertexData& vertex : modelData.vertices) {
        const float dx = vertex.position.x - center.x;
        const float dy = vertex.position.y - center.y;
        const float dz = vertex.position.z - center.z;
        radiusSq = (std::max)(radiusSq, dx * dx + dy * dy + dz * dz);
    }

    modelData.localAabbMin = min;
    modelData.localAabbMax = max;
    modelData.localAabbCenter = center;
    modelData.localBoundingRadius = std::sqrt(radiusSq);
    modelData.hasBounds = true;
}

void Model::LoadIndicesFromMesh(
    aiMesh* mesh,
    uint32_t baseVertex,
    ModelData& modelData)
{
    // インデックス情報の読み込み（三角形のみ対応）
    for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
        aiFace& face = mesh->mFaces[faceIndex];
        if (face.mNumIndices != 3) {
            LogSkippedNonTriangleFace(mesh->mName.C_Str(), faceIndex, face.mNumIndices);
            continue;
        }
        for (uint32_t element = 0; element < face.mNumIndices; ++element) {
            modelData.indices.push_back(
                baseVertex + face.mIndices[element]);
        }
    }
}

void Model::LoadSkinClusterDataFromMesh(
    aiMesh* mesh,
    uint32_t baseVertex,
    ModelData& modelData)
{
    // ボーンごとの逆バインド姿勢と頂点ウェイトを収集する
    for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
        aiBone* bone = mesh->mBones[boneIndex];
        std::string jointName = bone->mName.C_Str();
        JointWeightData& jointWeightData = modelData.skinClusterData[jointName];

        // バインドポーズ行列を分解する
        aiMatrix4x4 bindPoseMatrixAssimp = bone->mOffsetMatrix.Inverse();
        aiVector3D scale, translate;
        aiQuaternion rotation;
        bindPoseMatrixAssimp.Decompose(scale, rotation, translate);

        Matrix4x4 bindposeMatrix = MyMath::MakeAffineMatrix(
            { scale.x, scale.y, scale.z },
            { rotation.x, -rotation.y, -rotation.z, rotation.w },
            { -translate.x, translate.y, translate.z }
        );
        jointWeightData.inverseBindPoseMatrix = bindposeMatrix.Inverse();

        // 頂点ごとの重みを格納する
        for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
            jointWeightData.vertexWeights.push_back({
                bone->mWeights[weightIndex].mWeight,
                baseVertex + bone->mWeights[weightIndex].mVertexId
                });
        }
    }
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
        modelData.material.textureFilePath = ResolveResourceFilePath(directoryPath, texturePath.C_Str());
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
    skinCluster.influenceResource =
        modelCommon_->GetDxCommon()->CreateDefaultBufferResource(
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
