#include "ImGuizmoManager.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include "GizmoSnapSettings.h"
#include <stdexcept>

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
	Mode mode, const float* snap)
{
	if (snap) for (int i=0;i<3;++i)
		if (!ValidGizmoSnapStep(static_cast<int>(operation),snap[i])) throw std::invalid_argument("Invalid gizmo snap step");
	return ImGuizmo::Manipulate(
		viewMatrix,
		projectionMatrix,
		ToImGuizmoOperation(operation),
		ToImGuizmoMode(mode),
		objectMatrix,nullptr,snap);
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
bool ImGuizmoManager::IsOver() { return ImGuizmo::IsOver(); }
void ImGuizmoManager::SetDrawlist() { ImGuizmo::SetDrawlist(); }
void ImGuizmoManager::SetOrthographic(bool enabled) { ImGuizmo::SetOrthographic(enabled); }
void ImGuizmoManager::Enable(bool enabled) { ImGuizmo::Enable(enabled); }

} // namespace Engine::Editor
