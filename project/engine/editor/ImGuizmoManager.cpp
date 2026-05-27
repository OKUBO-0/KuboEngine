#include "ImGuizmoManager.h"
#include "imgui.h"
#include "ImGuizmo.h"

namespace Engine::Editor {

namespace {

ImGuizmo::OPERATION ToImGuizmoOperation(ImGuizmoManager::Operation operation)
{
	switch (operation) {
	case ImGuizmoManager::Operation::Rotate:
		return ImGuizmo::ROTATE;
	case ImGuizmoManager::Operation::Scale:
		return ImGuizmo::SCALE;
	case ImGuizmoManager::Operation::Translate:
	default:
		return ImGuizmo::TRANSLATE;
	}
}

ImGuizmo::MODE ToImGuizmoMode(ImGuizmoManager::Mode mode)
{
	return mode == ImGuizmoManager::Mode::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
}

}

void ImGuizmoManager::BeginFrame()
{
	ImGuizmo::BeginFrame();
}

void ImGuizmoManager::SetRect(float x, float y, float width, float height)
{
	ImGuizmo::SetRect(x, y, width, height);
}

bool ImGuizmoManager::Manipulate(
	const float* viewMatrix,
	const float* projectionMatrix,
	float* objectMatrix,
	Operation operation,
	Mode mode)
{
	return ImGuizmo::Manipulate(
		viewMatrix,
		projectionMatrix,
		ToImGuizmoOperation(operation),
		ToImGuizmoMode(mode),
		objectMatrix);
}

void ImGuizmoManager::DecomposeMatrixToComponents(
	const float* matrix,
	float* translation,
	float* rotation,
	float* scale)
{
	ImGuizmo::DecomposeMatrixToComponents(matrix, translation, rotation, scale);
}

void ImGuizmoManager::RecomposeMatrixFromComponents(
	const float* translation,
	const float* rotation,
	const float* scale,
	float* matrix)
{
	ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, matrix);
}

bool ImGuizmoManager::IsUsing()
{
	return ImGuizmo::IsUsing();
}

} // namespace Engine::Editor
