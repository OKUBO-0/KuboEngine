#include "Model.h"
#include "DirectXCommon.h"
#include "ModelAssetResolver.h"
#include "ModelMeshConverter.h"
#include "HResult.h"
#include "ModelCommon.h"
#include "MyMath.h"
#include <Windows.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>
#include <assert.h>
#include "TextureManager.h"
#include "SrvManager.h"

namespace {

Engine::Graphics3D::ModelLoadDiagnostics g_modelLoadDiagnostics{};
constexpr float kDefaultAnimationTicksPerSecond = 25.0f;
constexpr const char* kFallbackTexturePath = "Resources/DirectXGame/white1x1.png";

float ResolveAnimationTicksPerSecond(const aiAnimation& animation)
{
    const float ticksPerSecond =
        static_cast<float>(animation.mTicksPerSecond);
    return std::isfinite(ticksPerSecond) && ticksPerSecond > 0.0f
        ? ticksPerSecond
        : kDefaultAnimationTicksPerSecond;
}

std::string ToLowerAscii(std::string value)
{
    for (char& c : value) {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string ExtractAnimationLeafName(const std::string& animationName)
{
    const size_t separator = animationName.find_last_of('|');
    if (separator == std::string::npos || separator + 1 >= animationName.size()) {
        return animationName;
    }
    return animationName.substr(separator + 1);
}

std::string ResolveAnimationAlias(const std::string& animationName)
{
    const std::string leaf = ToLowerAscii(ExtractAnimationLeafName(animationName));
	if (leaf == "idle") {
		return "idle";
	}
	if (leaf == "idle_hold") {
		return "idle_hold";
	}
	if (leaf == "walk") {
		return "walk";
	}
	if (leaf == "walk_hold") {
		return "walk_hold";
	}
    if (leaf == "run") {
        return "run";
    }
    if (leaf == "jump") {
        return "jump";
    }
    if (leaf == "fall" || leaf == "falling") {
        return "fall";
    }
    if (leaf == "land" || leaf == "landing") {
        return "land";
    }
    if (leaf == "attack" || leaf == "punch" || leaf == "headbutt") {
        return "attack";
    }
    if (leaf == "hitreact" || leaf == "hitrecieve" || leaf == "hitreceive") {
        return "hit";
    }
    if (leaf == "death") {
        return "death";
    }
    return {};
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

bool ShouldSkipMeshForCurrentRenderer(
    const std::string& filename,
    const aiMesh& mesh)
{
    (void)filename;
    (void)mesh;
    return false;
}

std::string ExtractSkinPrefix(const std::string& jointName)
{
    const size_t separator = jointName.find("::");
    if (separator == std::string::npos) {
        return {};
    }
    return jointName.substr(0, separator);
}

bool HasExistingSkinJointConflict(
    const aiMesh& mesh,
    const ModelData& modelData)
{
    for (uint32_t boneIndex = 0; boneIndex < mesh.mNumBones; ++boneIndex) {
        const std::string jointName = mesh.mBones[boneIndex]->mName.C_Str();
        if (modelData.skinClusterData.contains(jointName)) {
            return true;
        }
    }
    return false;
}

std::string MakeSkinPrefix(const aiMesh& mesh, uint32_t meshIndex)
{
    std::string prefix = mesh.mName.C_Str();
    if (prefix.empty()) {
        prefix = "mesh" + std::to_string(meshIndex);
    }
    return prefix;
}

void AddSkinJointAliases(
    Skeleton& skeleton,
    const ModelData& modelData)
{
    std::vector<std::string> prefixes;
    for (const auto& jointWeight : modelData.skinClusterData) {
        const std::string prefix = ExtractSkinPrefix(jointWeight.first);
        if (!prefix.empty() &&
            std::find(prefixes.begin(), prefixes.end(), prefix) == prefixes.end()) {
            prefixes.push_back(prefix);
        }
    }
    if (prefixes.empty()) {
        return;
    }

    const size_t originalJointCount = skeleton.joints.size();
    for (const std::string& prefix : prefixes) {
        std::vector<int32_t> remap(originalJointCount, -1);
        for (size_t sourceIndex = 0; sourceIndex < originalJointCount; ++sourceIndex) {
            Joint duplicate = skeleton.joints[sourceIndex];
            duplicate.name = prefix + "::" + duplicate.name;
            duplicate.index = static_cast<int32_t>(skeleton.joints.size());
            duplicate.children.clear();
            if (duplicate.parent.has_value()) {
                duplicate.parent = remap[static_cast<size_t>(*duplicate.parent)];
            }
            remap[sourceIndex] = duplicate.index;
            skeleton.jointMap[duplicate.name] = duplicate.index;
            skeleton.joints.push_back(std::move(duplicate));
        }
        for (size_t sourceIndex = 0; sourceIndex < originalJointCount; ++sourceIndex) {
            const int32_t duplicateIndex = remap[sourceIndex];
            for (int32_t childIndex : skeleton.joints[sourceIndex].children) {
                if (childIndex >= 0 &&
                    static_cast<size_t>(childIndex) < originalJointCount) {
                    skeleton.joints[duplicateIndex].children.push_back(
                        remap[static_cast<size_t>(childIndex)]);
                }
            }
        }
    }
}

bool TryParseEmbeddedTextureIndex(const std::string& texturePath, uint32_t& textureIndex)
{
    if (texturePath.size() < 2 || texturePath.front() != '*') {
        return false;
    }

    char* end = nullptr;
    const unsigned long parsedIndex = std::strtoul(texturePath.c_str() + 1, &end, 10);
    if (end == texturePath.c_str() + 1 || *end != '\0') {
        return false;
    }

    textureIndex = static_cast<uint32_t>(parsedIndex);
    return true;
}

std::string EmbeddedTextureKey(
    const std::string& directoryPath,
    const std::string& filename,
    const std::string& texturePath)
{
    std::filesystem::path keyPath(directoryPath);
    keyPath /= filename;
    return keyPath.generic_string() + "#embedded_texture_" + texturePath.substr(1);
}

void LoadEmbeddedTexture(
    const aiScene* scene,
    uint32_t textureIndex,
    const std::string& textureKey)
{
    if (!scene || textureIndex >= scene->mNumTextures) {
        return;
    }

    const aiTexture* texture = scene->mTextures[textureIndex];
    if (!texture) {
        return;
    }

    Engine::Base::TextureManager* textureManager =
        Engine::Base::TextureManager::GetInstance();
    if (texture->mHeight == 0) {
        textureManager->LoadTextureFromMemory(
            textureKey,
            texture->pcData,
            static_cast<size_t>(texture->mWidth));
        return;
    }

    std::vector<uint8_t> rgbaPixels(
        static_cast<size_t>(texture->mWidth) *
        static_cast<size_t>(texture->mHeight) * 4ull);
    for (uint32_t y = 0; y < texture->mHeight; ++y) {
        for (uint32_t x = 0; x < texture->mWidth; ++x) {
            const aiTexel& source =
                texture->pcData[static_cast<size_t>(y) * texture->mWidth + x];
            uint8_t* destination =
                &rgbaPixels[(static_cast<size_t>(y) * texture->mWidth + x) * 4ull];
            destination[0] = source.r;
            destination[1] = source.g;
            destination[2] = source.b;
            destination[3] = source.a;
        }
    }
    textureManager->LoadTextureFromRGBA(
        textureKey,
        texture->mWidth,
        texture->mHeight,
        rgbaPixels.data(),
        static_cast<size_t>(texture->mWidth) * 4ull);
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
	LoadEmbeddedAnimationClips(directorypath, filename);
	skeleton = CreateSkeleton(modelData.rootNode);
    AddSkinJointAliases(skeleton, modelData);
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
	Engine::Base::TextureManager* textureManager =
		Engine::Base::TextureManager::GetInstance();
	for (MaterialData& material : modelData.materials) {
		textureManager->LoadTexture(material.textureFilePath);
		material.textureIndex =
			textureManager->GetTextureIndexByFilePath(material.textureFilePath);
	}
	if (modelData.materials.empty()) {
		textureManager->LoadTexture(modelData.material.textureFilePath);
		modelData.material.textureIndex =
			textureManager->GetTextureIndexByFilePath(modelData.material.textureFilePath);
		return;
	}
	modelData.material = modelData.materials.front();
}

void Model::Draw(
	D3D12_GPU_VIRTUAL_ADDRESS materialAddress,
	const Material* materialOverride)
{
	DrawInstanced(1, materialAddress, materialOverride);
}

void Model::DrawInstanced(
	UINT instanceCount,
	D3D12_GPU_VIRTUAL_ADDRESS materialAddress,
	const Material* materialOverride)
{
	const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
		modelCommon_->GetDxCommon();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
	const Material& baseMaterial =
		materialOverride ? *materialOverride : materialData_;
	if (materialAddress == 0) {
		const Engine::Base::DirectXCommon::FrameUploadAllocation materialAllocation =
			dxCommon->AllocateFrameUpload(sizeof(Material), 256);
		std::memcpy(
			materialAllocation.cpuAddress,
			&baseMaterial,
			sizeof(baseMaterial));
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

	if (!modelData.submeshes.empty() && !modelData.materials.empty()) {
		for (const SubmeshData& submesh : modelData.submeshes) {
			const MaterialData& submeshMaterial =
				modelData.materials[(std::min)(
					static_cast<size_t>(submesh.materialIndex),
					modelData.materials.size() - 1)];
			Material drawMaterial = baseMaterial;
			drawMaterial.color.x *= submeshMaterial.diffuseColor.x;
			drawMaterial.color.y *= submeshMaterial.diffuseColor.y;
			drawMaterial.color.z *= submeshMaterial.diffuseColor.z;
			drawMaterial.color.w *= submeshMaterial.diffuseColor.w;
			const Engine::Base::DirectXCommon::FrameUploadAllocation submeshMaterialAllocation =
				dxCommon->AllocateFrameUpload(sizeof(Material), 256);
			std::memcpy(
				submeshMaterialAllocation.cpuAddress,
				&drawMaterial,
				sizeof(drawMaterial));
			commandList->SetGraphicsRootConstantBufferView(
				0,
				submeshMaterialAllocation.gpuAddress);
			modelCommon_->GetSRVManager()->SetGraphicsRootDescriptorTable(
				2,
				Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(
					submeshMaterial.textureFilePath));
			commandList->DrawIndexedInstanced(
				submesh.indexCount,
				instanceCount,
				submesh.startIndex,
				0,
				0);
		}
		return;
	}

	// テクスチャSRV設定
	modelCommon_->GetSRVManager()->SetGraphicsRootDescriptorTable(2, Engine::Base::TextureManager::GetInstance()->GetTextureIndexByFilePath(modelData.material.textureFilePath));

	// インデックス付き描画（インスタンス数 = 1）
	commandList->DrawIndexedInstanced(
		static_cast<UINT>(modelData.indices.size()), // インデックス数
		instanceCount,  // インスタンス数
		0,  // 開始インデックス
		0,  // 基準頂点
		0   // 開始インスタンス
	);
}

void Model::DrawGeometry()
{
	DrawGeometryInstanced(1);
}

void Model::DrawGeometryInstanced(UINT instanceCount)
{
	const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
		modelCommon_->GetDxCommon();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
	D3D12_VERTEX_BUFFER_VIEW vertexView = vertexBufferView;
	commandList->IASetVertexBuffers(0, 1, &vertexView);
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->DrawIndexedInstanced(
		static_cast<UINT>(modelData.indices.size()), instanceCount, 0, 0, 0);
}

void Model::DrawSkinnedGeometry()
{
	DrawSkinnedGeometryInstanced(1);
}

void Model::DrawSkinnedGeometryInstanced(UINT instanceCount)
{
	const std::shared_ptr<Engine::Base::DirectXCommon> dxCommon =
		modelCommon_->GetDxCommon();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
	D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {
		vertexBufferView,
		skinCluster.influenceBufferView,
	};
	commandList->IASetVertexBuffers(0, 2, vbvs);
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->DrawIndexedInstanced(
		static_cast<UINT>(modelData.indices.size()), instanceCount, 0, 0, 0);
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
	result.transform.translate = { -translate.x, translate.y, translate.z };

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
        if (ShouldSkipMeshForCurrentRenderer(filename, *mesh)) {
            continue;
        }
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
        if (ShouldSkipMeshForCurrentRenderer(filename, *mesh)) {
            continue;
        }
        if (!mesh->HasNormals()) {
            LogMeshFallback(path, meshIndex, "mesh has no normals; using fallback normal");
        }
        if (!mesh->HasTextureCoords(0)) {
            LogMeshFallback(path, meshIndex, "mesh has no texture coordinates; using fallback uv");
        }

        const uint32_t baseVertex =
            static_cast<uint32_t>(modelData.vertices.size());
        const uint32_t startIndex =
            static_cast<uint32_t>(modelData.indices.size());
        LoadVerticesFromMesh(mesh, modelData);
        LoadIndicesFromMesh(mesh, baseVertex, modelData);
        const bool needsSkinPrefix =
            mesh->HasBones() && HasExistingSkinJointConflict(*mesh, modelData);
        LoadSkinClusterDataFromMesh(
            mesh,
            baseVertex,
            modelData,
            needsSkinPrefix ? MakeSkinPrefix(*mesh, meshIndex) : std::string{});
        const uint32_t indexCount =
            static_cast<uint32_t>(modelData.indices.size()) - startIndex;
        if (indexCount > 0) {
            modelData.submeshes.push_back({
                startIndex,
                indexCount,
                mesh->mMaterialIndex,
                });
        }
    }
    CalculateBounds(modelData);

    // 先頭のディフューズテクスチャをモデル側の代表マテリアルとして採用する
    // マテリアル解析
    LoadMaterialFromScene(scene, directoryPath, filename, modelData);

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
    animation = BuildAnimationClip(animationAssimp);
    return animation;
}

void Model::LoadEmbeddedAnimationClips(
    const std::string& directoryPath,
    const std::string& filename)
{
    Assimp::Importer importer;
    const std::string filepath = ResolveModelAssetPath(directoryPath, filename);
    const aiScene* scene = importer.ReadFile(filepath.c_str(), 0);
    if (scene == nullptr || scene->mNumAnimations == 0) {
        return;
    }

    for (uint32_t animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex) {
        aiAnimation* animationAssimp = scene->mAnimations[animationIndex];
        if (!animationAssimp) {
            continue;
        }
        Animation clip = BuildAnimationClip(animationAssimp);
        if (clip.nodeAnimations.empty()) {
            continue;
        }
        const std::string fullName = animationAssimp->mName.C_Str();
        const std::string alias = ResolveAnimationAlias(fullName);
        if (!alias.empty()) {
            animationClips_[alias] = clip;
        }
        if (animationIndex == 0) {
            animation = clip;
            animationClips_["default"] = clip;
        }
    }
}

Animation Model::BuildAnimationClip(const aiAnimation* animationAssimp)
{
    Animation animation;
    if (!animationAssimp) {
        return animation;
    }
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

const Animation* Model::FindAnimationClip(const std::string& clipName) const
{
    const auto it = animationClips_.find(clipName);
    if (it != animationClips_.end()) {
        return &it->second;
    }
    if (clipName == "default" && !animation.nodeAnimations.empty()) {
        return &animation;
    }
    return nullptr;
}

bool Model::HasAnimationClip(const std::string& clipName) const
{
    return FindAnimationClip(clipName) != nullptr;
}

bool Model::LoadAnimationClip(
    const std::string& clipName,
    const std::string& directoryPath,
    const std::string& filename)
{
    Animation loadedAnimation;
    Assimp::Importer importer;
    const std::string filepath = ResolveModelAssetPath(directoryPath, filename);
    const aiScene* scene = importer.ReadFile(filepath.c_str(), 0);
    if (scene == nullptr) {
        std::ostringstream message;
        message << "[Model::LoadAnimationClip] optional clip load failed"
            << " clip=\"" << clipName
            << "\" path=\"" << filepath
            << "\" detail=\"" << importer.GetErrorString() << "\"\n";
        OutputDebugStringA(message.str().c_str());
        return false;
    }
    if (scene->mNumAnimations == 0) {
        std::ostringstream message;
        message << "[Model::LoadAnimationClip] optional clip has no animations"
            << " clip=\"" << clipName
            << "\" path=\"" << filepath << "\"\n";
        OutputDebugStringA(message.str().c_str());
        return false;
    }
    loadedAnimation = BuildAnimationClip(scene->mAnimations[0]);
    if (loadedAnimation.nodeAnimations.empty()) {
        return false;
    }
    animationClips_[clipName] = std::move(loadedAnimation);
    if (animation.nodeAnimations.empty()) {
        animation = animationClips_.at(clipName);
        animationClips_["default"] = animation;
    }
    return true;
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
    ModelData& modelData,
    const std::string& skinPrefix)
{
    AppendSkinClusterDataFromMesh(*mesh, baseVertex, modelData, skinPrefix);
}

void Model::LoadMaterialFromScene(
	const aiScene* scene,
	const std::string& directoryPath,
	const std::string& filename,
	ModelData& modelData)
{
	for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
		aiMaterial* material = scene->mMaterials[materialIndex];
		MaterialData materialData;
		materialData.textureFilePath = kFallbackTexturePath;

		aiColor4D diffuseColor;
		if (AI_SUCCESS == aiGetMaterialColor(
			material,
			AI_MATKEY_BASE_COLOR,
			&diffuseColor) ||
			AI_SUCCESS == aiGetMaterialColor(
			material,
			AI_MATKEY_COLOR_DIFFUSE,
			&diffuseColor)) {
			materialData.diffuseColor = {
				diffuseColor.r,
				diffuseColor.g,
				diffuseColor.b,
				diffuseColor.a,
			};
		}

		const aiTextureType textureType =
			material->GetTextureCount(aiTextureType_BASE_COLOR) > 0
				? aiTextureType_BASE_COLOR
				: aiTextureType_DIFFUSE;
		if (material->GetTextureCount(textureType) > 0) {
			aiString texturePath;
			material->GetTexture(textureType, 0, &texturePath);
			const std::string texturePathString = texturePath.C_Str();
			uint32_t embeddedTextureIndex = 0;
			if (TryParseEmbeddedTextureIndex(texturePathString, embeddedTextureIndex)) {
				materialData.textureFilePath =
					EmbeddedTextureKey(directoryPath, filename, texturePathString);
				LoadEmbeddedTexture(
					scene,
					embeddedTextureIndex,
					materialData.textureFilePath);
			} else if (!texturePathString.empty()) {
				materialData.textureFilePath =
					ResolveModelResourcePath(directoryPath, texturePathString);
			}
		}

		modelData.materials.push_back(materialData);
	}
	if (modelData.materials.empty()) {
		modelData.materials.push_back({
			kFallbackTexturePath,
			0,
			{ 1.0f, 1.0f, 1.0f, 1.0f },
			});
	}
	modelData.material = modelData.materials.front();
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
