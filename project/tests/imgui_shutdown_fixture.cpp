#include "pinned_imgui_impl_dx12.cpp"
#include <imgui_node_editor.h>
#include <cstdlib>
#include <iostream>
#include <new>
#include <type_traits>

namespace {
bool tracking = false;
unsigned attempts = 0, failAt = 0;
void* ownedArrays[2] = {};
unsigned Outstanding() noexcept { return unsigned(ownedArrays[0] != nullptr) + unsigned(ownedArrays[1] != nullptr); }
}
void* operator new(size_t bytes) {
    if (auto* memory = std::malloc(bytes ? bytes : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](size_t bytes) {
    if (tracking && ++attempts == failAt) throw std::bad_alloc();
    void* memory = ::operator new(bytes);
    if (tracking) for (auto& owned : ownedArrays) if (!owned) { owned = memory; break; }
    return memory;
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept {
    if (tracking) for (auto& owned : ownedArrays) if (owned == memory) owned = nullptr;
    std::free(memory);
}
void operator delete(void* memory, size_t) noexcept { ::operator delete(memory); }
void operator delete[](void* memory, size_t) noexcept { ::operator delete[](memory); }

// Failed-construction recovery releases only fixture-tracked trivial arrays.
static_assert(std::is_trivially_destructible_v<ImGui_ImplDX12_FrameContext>);
static_assert(std::is_trivially_destructible_v<ImGui_ImplDX12_RenderBuffers>);
int main() {
    ImGuiContext* context = nullptr;
    ax::NodeEditor::EditorContext* editor = nullptr;
    ax::NodeEditor::EditorContext* other = nullptr;
    const auto recoverArrays = [] {
        tracking = true;
        for (auto& memory : ownedArrays) if (memory) ::operator delete[](memory);
        tracking = false;
    };
    try {
        tracking = true;
        { ImGui_ImplDX12_ViewportData control(3); }
        const bool normalClean = attempts == 2 && Outstanding() == 0;
        tracking = false;
        bool arrayChecks = normalClean;
        for (unsigned position = 1; position <= 2; ++position) {
            attempts = 0; failAt = position; tracking = true;
            bool faulted = false;
            try { ImGui_ImplDX12_ViewportData viewport(3); }
            catch (const std::bad_alloc&) { faulted = true; }
            const auto retained = Outstanding();
            tracking = false;
            arrayChecks = arrayChecks && faulted && attempts == position && retained == 0;
            std::cout << "IMGUI_VIEWPORT_CHECK position=" << position << " fault=" << faulted
                << " attempts=" << attempts << " retained_arrays=" << retained << '\n';
            recoverArrays();
        }
        context = ImGui::CreateContext(); ImGui::GetIO().IniFilename = nullptr;
        ax::NodeEditor::Config config{}; config.SettingsFile = nullptr;
        editor = ax::NodeEditor::CreateEditor(&config);
        ax::NodeEditor::SetCurrentEditor(editor);
        ax::NodeEditor::DestroyEditor(editor); editor = nullptr;
        const bool clearedCurrent = ax::NodeEditor::GetCurrentEditor() == nullptr;
        ax::NodeEditor::SetCurrentEditor(nullptr); // Before-control recovery; no stale access.
        editor = ax::NodeEditor::CreateEditor(&config);
        other = ax::NodeEditor::CreateEditor(&config);
        ax::NodeEditor::SetCurrentEditor(other);
        ax::NodeEditor::DestroyEditor(editor); editor = nullptr;
        const bool preservedOther = ax::NodeEditor::GetCurrentEditor() == other;
        ax::NodeEditor::DestroyEditor(other); other = nullptr;
        const bool clearedOther = ax::NodeEditor::GetCurrentEditor() == nullptr;
        ax::NodeEditor::SetCurrentEditor(nullptr);
        ImGui::DestroyContext(context); context = nullptr;
        std::cout << "NODE_EDITOR_CHECK current_cleared=" << clearedCurrent
            << " unrelated_preserved=" << preservedOther << " final_cleared=" << clearedOther << '\n'
            << "IMGUI_FIXTURE_RECOVERED normal_clean=" << normalClean
            << " outstanding_arrays=" << Outstanding() << " current_null=" << (ax::NodeEditor::GetCurrentEditor() == nullptr) << '\n';
        if (!arrayChecks || !clearedCurrent || !preservedOther || !clearedOther) {
            std::cout << "FAIL: viewport array rollback or current-editor retirement contract\n";
            return 1;
        }
        std::cout << "PASS: viewport allocation rollback and current/inactive NodeEditor retirement; no window/device/backend Init\n";
        return 0;
    } catch (const std::exception& error) {
        recoverArrays(); ax::NodeEditor::SetCurrentEditor(nullptr);
        if (editor) ax::NodeEditor::DestroyEditor(editor);
        if (other) ax::NodeEditor::DestroyEditor(other);
        if (context) ImGui::DestroyContext(context);
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
