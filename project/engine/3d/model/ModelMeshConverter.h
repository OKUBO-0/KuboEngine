#pragma once

#include "RenderingData.h"
#include <cstdint>

struct aiMesh;

namespace Engine::Graphics3D {

using SkippedFaceCallback = void (*)(const char* meshName, uint32_t faceIndex, uint32_t indexCount);

void AppendVerticesFromMesh(const aiMesh& mesh, ModelData& modelData);
void AppendIndicesFromMesh(
	const aiMesh& mesh,
	uint32_t baseVertex,
	ModelData& modelData,
	SkippedFaceCallback skippedFaceCallback);
void AppendSkinClusterDataFromMesh(const aiMesh& mesh, uint32_t baseVertex, ModelData& modelData);
void CalculateModelBounds(ModelData& modelData);

}
