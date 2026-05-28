#pragma once

namespace Engine::Editor {

class ImGuizmoManager {
public:
	enum class Operation {
		Translate,
		Rotate,
		Scale,
	};

	enum class Mode {
		Local,
		World,
	};

	static void BeginFrame();
	static void SetRect(float x, float y, float width, float height);
	static bool Manipulate(
		const float* viewMatrix,
		const float* projectionMatrix,
		float* objectMatrix,
		Operation operation,
		Mode mode);
	static void DecomposeMatrixToComponents(
		const float* matrix,
		float* translation,
		float* rotation,
		float* scale);
	static void RecomposeMatrixFromComponents(
		const float* translation,
		const float* rotation,
		const float* scale,
		float* matrix);
	static bool IsUsing();
};

} // namespace Engine::Editor
