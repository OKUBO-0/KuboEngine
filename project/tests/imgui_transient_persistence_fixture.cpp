#include <imgui.h>
#include <imgui_node_editor.h>
#include <Windows.h>
#include <crtdbg.h>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <new>
#include <string_view>

namespace {
bool armed = false, consumed = false;
unsigned saves = 0, nodes = 0, begins = 0, ends = 0;
}
void* operator new(size_t size) {
    if (armed) { armed = false; consumed = true; throw std::bad_alloc(); }
    if (void* ptr = std::malloc(size ? size : 1)) return ptr;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, size_t) noexcept { std::free(ptr); }

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    if (argc != 2) return 2;
    const auto mode = std::string_view(argv[1]);
    if (mode != "positive" && mode != "fault" && mode != "global" && mode != "node" && mode != "session") return 2;
    auto* gui = ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(640,480); io.DeltaTime = 1.0f/60.0f;
    io.Fonts->AddFontDefault(); io.Fonts->Build();
    ax::NodeEditor::Config config{}; config.SettingsFile = nullptr;
    if (mode == "global") config.SaveSettings = [](const char*, size_t, ax::NodeEditor::SaveReasonFlags, void*) { ++saves; return true; };
    if (mode == "node") config.SaveNodeSettings = [](ax::NodeEditor::NodeId, const char*, size_t, ax::NodeEditor::SaveReasonFlags, void*) { ++nodes; return true; };
    if (mode == "session") {
        config.BeginSaveSession = [](void*) { ++begins; };
        config.EndSaveSession = [](void*) { ++ends; };
    }
    auto* editor = ax::NodeEditor::CreateEditor(&config);
    ax::NodeEditor::SetCurrentEditor(editor);
    for (unsigned frame = 0; frame != 2; ++frame) {
        ImGui::NewFrame(); ImGui::Begin("Fixture-owned CPU canvas");
        ax::NodeEditor::Begin("Transient", ImVec2(300,200));
        ax::NodeEditor::BeginNode(ax::NodeEditor::NodeId(1)); ImGui::TextUnformatted("one");
        ax::NodeEditor::BeginPin(ax::NodeEditor::PinId(11), ax::NodeEditor::PinKind::Output);
        ImGui::TextUnformatted("output"); ax::NodeEditor::EndPin(); ax::NodeEditor::EndNode();
        ax::NodeEditor::BeginNode(ax::NodeEditor::NodeId(2)); ImGui::TextUnformatted("two");
        ax::NodeEditor::BeginPin(ax::NodeEditor::PinId(22), ax::NodeEditor::PinKind::Input);
        ImGui::TextUnformatted("input"); ax::NodeEditor::EndPin(); ax::NodeEditor::EndNode();
        ax::NodeEditor::Link(ax::NodeEditor::LinkId(3), ax::NodeEditor::PinId(11), ax::NodeEditor::PinId(22));
        ax::NodeEditor::End(); ImGui::End(); ImGui::EndFrame();
    }
    ax::NodeEditor::SelectNode(ax::NodeEditor::NodeId(1));
    ax::NodeEditor::SetNodePosition(ax::NodeEditor::NodeId(1), ImVec2(95,75));
    const unsigned priorSaves = saves, priorNodes = nodes, priorBegins = begins, priorEnds = ends;
    armed = mode == "fault";
    ax::NodeEditor::DestroyEditor(editor);
    armed = false;
    if (consumed || ax::NodeEditor::GetCurrentEditor()) return 3;
    ImGui::DestroyContext(gui);
    if (mode == "global" && saves != priorSaves + 1) return 4;
    if (mode == "node" && nodes == priorNodes) return 5;
    if (mode == "session" && (begins != priorBegins + 1 || ends != priorEnds + 1)) return 6;
    std::cout << "TRANSIENT_PERSISTENCE mode=" << mode << " consumed=0 current_retired=1 global_delta=" << saves-priorSaves
        << " node_delta=" << nodes-priorNodes << " begin_delta=" << begins-priorBegins << " end_delta=" << ends-priorEnds
        << " nodes=2 pins=2 links=1 selection=1 frames=2\n";
    std::cout << "PASS: actual initialized transient destruction and configured persistence/session routing; no backend/window/device\n";
    return 0;
}
