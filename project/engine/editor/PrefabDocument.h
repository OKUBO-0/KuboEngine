#pragma once
#include "../scene/prefab/PrefabDocument.h"

// Source-compatible Editor facade. Shared schema/normalization does not depend
// on selection, Undo/history, authoring services, ImGui or project publication.
namespace Engine::Editor {
using Prefab::PrefabValue;
using Prefab::PrefabBounds;
using Prefab::PrefabKnownKeys;
using Prefab::ValidatePrefabDocument;
using Prefab::CapturePrefabSubtree;
using Prefab::ReadPrefabDocument;
using Prefab::SavePrefabDocument;
}
