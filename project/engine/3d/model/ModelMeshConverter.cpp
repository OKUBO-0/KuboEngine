#include "ModelMeshConverter.h"

#include "MyMath.h"
#include <algorithm>
#include <assimp/mesh.h>
#include <cmath>
#include <string>

namespace Engine::Graphics3D {

void AppendVerticesFromMesh(const aiMesh& mesh, ModelData& modelData)
{
	const size_t baseVertex = modelData.vertices.size();
	modelData.vertices.resize(baseVertex + mesh.mNumVertices);

	for (uint32_t vertexIndex = 0; vertexIndex < mesh.mNumVertices; ++vertexIndex) {
		const aiVector3D& position = mesh.mVertices[vertexIndex];
		const aiVector3D normal = mesh.HasNormals()
			? mesh.mNormals[vertexIndex]
			: aiVector3D{ 0.0f, 1.0f, 0.0f };
		const aiVector3D texcoord = mesh.HasTextureCoords(0)
			? mesh.mTextureCoords[0][vertexIndex]
			: aiVector3D{ 0.0f, 0.0f, 0.0f };

		VertexData& vertex = modelData.vertices[baseVertex + vertexIndex];
		vertex.position = { -position.x, position.y, position.z, 1.0f };
		vertex.normal = { -normal.x, normal.y, normal.z };
		vertex.texcoord = { texcoord.x, texcoord.y };
	}
}

void CalculateModelBounds(ModelData& modelData)
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

void AppendIndicesFromMesh(
	const aiMesh& mesh,
	uint32_t baseVertex,
	ModelData& modelData,
	SkippedFaceCallback skippedFaceCallback)
{
	for (uint32_t faceIndex = 0; faceIndex < mesh.mNumFaces; ++faceIndex) {
		const aiFace& face = mesh.mFaces[faceIndex];
		if (face.mNumIndices != 3) {
			if (skippedFaceCallback) {
				skippedFaceCallback(mesh.mName.C_Str(), faceIndex, face.mNumIndices);
			}
			continue;
		}
		for (uint32_t element = 0; element < face.mNumIndices; ++element) {
			modelData.indices.push_back(baseVertex + face.mIndices[element]);
		}
	}
}

void AppendSkinClusterDataFromMesh(const aiMesh& mesh, uint32_t baseVertex, ModelData& modelData)
{
	for (uint32_t boneIndex = 0; boneIndex < mesh.mNumBones; ++boneIndex) {
		const aiBone* bone = mesh.mBones[boneIndex];
		std::string jointName = bone->mName.C_Str();
		JointWeightData& jointWeightData = modelData.skinClusterData[jointName];

		aiMatrix4x4 bindPoseMatrixAssimp = bone->mOffsetMatrix;
		bindPoseMatrixAssimp.Inverse();
		aiVector3D scale, translate;
		aiQuaternion rotation;
		bindPoseMatrixAssimp.Decompose(scale, rotation, translate);

		Matrix4x4 bindposeMatrix = MyMath::MakeAffineMatrix(
			{ scale.x, scale.y, scale.z },
			{ rotation.x, -rotation.y, -rotation.z, rotation.w },
			{ -translate.x, translate.y, translate.z });
		jointWeightData.inverseBindPoseMatrix = bindposeMatrix.Inverse();

		for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
			jointWeightData.vertexWeights.push_back({
				bone->mWeights[weightIndex].mWeight,
				baseVertex + bone->mWeights[weightIndex].mVertexId,
				});
		}
	}
}

}
