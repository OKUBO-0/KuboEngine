#include "DirectXCommon.h"
#include "WinApp.h"
#include <Windows.h>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>

bool presentFailure = false;
unsigned closeFailures = 0, signalFailures = 0;
unsigned closes = 0, executions = 0, signals = 0;
unsigned allocatorFailures = 0, listFailures = 0, allocatorResets = 0, listResets = 0;
HRESULT FixtureAllocatorReset(ID3D12CommandAllocator* allocator) {
    ++allocatorResets;
    if (allocatorFailures) { --allocatorFailures; return E_FAIL; }
    return allocator->Reset();
}
HRESULT FixtureListReset(ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* allocator) {
    ++listResets;
    if (listFailures) { --listFailures; return E_FAIL; }
    return list->Reset(allocator, nullptr);
}
void FixtureBeforePresent() { if (presentFailure) throw std::runtime_error("owned post-submit failure"); }
HRESULT FixtureClose(ID3D12GraphicsCommandList* list) {
    ++closes;
    if (closeFailures) { --closeFailures; return E_FAIL; }
    return list->Close();
}
void FixtureExecute(ID3D12CommandQueue* queue, ID3D12CommandList* list) {
    ++executions; queue->ExecuteCommandLists(1, &list);
}
HRESULT FixtureQueueSignal(ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64 value) {
    ++signals;
    if (signalFailures) { --signalFailures; return E_FAIL; }
    return queue->Signal(fence, value);
}
using Graphics = Engine::Base::DirectXCommon;
using State = Graphics::CommandState;
bool RecordingRefused(Graphics& graphics) {
    unsigned refused = 0;
    try { graphics.Begin(); } catch (const std::logic_error&) { ++refused; }
    try { graphics.End(); } catch (const std::logic_error&) { ++refused; }
    try { graphics.BeginGpuFrameTiming(); } catch (const std::logic_error&) { ++refused; }
    return refused == 3;
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto mode = std::string_view(argv[1]);
    if (mode != "cold" && mode != "normal" && mode != "recorded" && mode != "close" &&
        mode != "present-recover" && mode != "signal-recover" && mode != "shutdown-retry" &&
        mode != "allocator-reset" && mode != "list-reset") return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (mode == "cold") {
        Graphics graphics;
        bool kickRefused = false;
        try { graphics.CommandKick(); } catch (const std::logic_error&) { kickRefused = true; }
        graphics.PrepareForShutdown(); graphics.PrepareForShutdown(); graphics.CommandKick();
        if (!kickRefused || graphics.GetCommandState() != State::Retired || !RecordingRefused(graphics) ||
            closes || executions || signals) return 3;
        std::cout << "GPU_COMMAND_STATE cold=1 retired=1 repeated=1 native_calls=0\n"; return 0;
    }
    Engine::Base::WinApp window; window.Initialize(); ShowWindow(window.GetHwnd(), SW_HIDE);
    auto graphics = std::make_shared<Graphics>(); graphics->Initialize(&window);
    if (graphics->GetCommandState() != State::Recording) return 4;
    if (mode == "normal") {
        for (unsigned frame = 0; frame != 2; ++frame) {
            graphics->BeginGpuFrameTiming(); graphics->Begin(); graphics->End();
            if (graphics->GetCommandState() != State::Recording) return 5;
        }
    } else if (mode == "allocator-reset" || mode == "list-reset") {
        allocatorFailures = mode == "allocator-reset" ? 1 : 0;
        listFailures = mode == "list-reset" ? 1 : 0;
        bool original = false;
        try { graphics->BeginGpuFrameTiming(); graphics->Begin(); graphics->End(); }
        catch (const std::runtime_error& error) {
            const auto diagnostic = std::string_view(error.what());
            original = mode == "allocator-reset"
                ? diagnostic.starts_with("ID3D12CommandAllocator::Reset failed with HRESULT 0x80004005")
                : diagnostic.starts_with("ID3D12GraphicsCommandList::Reset failed with HRESULT 0x80004005");
        }
        if (!original || graphics->GetCommandState() != State::Failed || graphics->HasUnfencedSubmission() ||
            closes != 1 || executions != 1 || signals != 1 || !RecordingRefused(*graphics) ||
            allocatorResets != 1 || listResets != (mode == "list-reset" ? 1u : 0u)) return 13;
        bool refused = false;
        try { graphics->CommandKick(); } catch (const std::logic_error&) { refused = true; }
        if (!refused || closes != 1 || allocatorResets != 1) return 14;
    } else if (mode == "recorded" || mode == "close") {
        graphics->BeginGpuFrameTiming(); graphics->Begin();
        if (mode == "close") {
            closeFailures = 1; bool original = false;
            try { graphics->CommandKick(); } catch (const std::runtime_error& error) {
                original = std::string_view(error.what()).starts_with("ID3D12GraphicsCommandList::Close failed with HRESULT 0x80004005");
            }
            if (!original || graphics->GetCommandState() != State::Failed || !RecordingRefused(*graphics)) return 6;
            bool kickRefused = false;
            try { graphics->CommandKick(); } catch (const std::logic_error&) { kickRefused = true; }
            if (!kickRefused || closes != 1) return 7;
        }
        if (executions || signals || graphics->HasUnfencedSubmission()) return 8;
    } else {
        presentFailure = mode != "signal-recover";
        signalFailures = mode == "signal-recover" ? 1 : 0;
        bool original = false;
        try { graphics->BeginGpuFrameTiming(); graphics->Begin(); graphics->End(); }
        catch (const std::runtime_error& error) {
            const auto message = std::string_view(error.what());
            original = presentFailure ? message == "owned post-submit failure"
                : message.starts_with("ID3D12CommandQueue::Signal failed with HRESULT 0x80004005");
        }
        if (!original || graphics->GetCommandState() != State::Submitted || !graphics->HasUnfencedSubmission() ||
            !RecordingRefused(*graphics) || closes != 1 || executions != 1) std::_Exit(80);
        presentFailure = false;
        if (mode == "shutdown-retry") {
            signalFailures = 1; bool refused = false;
            auto* retainedList = graphics->GetCommandList();
            try { graphics->PrepareForShutdown(); } catch (const std::runtime_error&) { refused = true; }
            if (!refused || graphics->GetCommandState() != State::Submitted ||
                graphics->GetCommandList() != retainedList || !graphics->HasUnfencedSubmission() ||
                !IsWindow(window.GetHwnd())) std::_Exit(81);
            Microsoft::WRL::ComPtr<ID3D12Fence> gate;
            if (FAILED(graphics->GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))) ||
                FAILED(graphics->GetCommandQueue()->Wait(gate.Get(), 1))) std::_Exit(82);
            std::atomic<bool> entered{false}, returned{false}, early{false};
            std::atomic<HRESULT> released{E_FAIL};
            std::thread release([&] { while (!entered.load()) Sleep(1); Sleep(250); early=returned.load(); released=gate->Signal(1); });
            entered = true;
            try { graphics->PrepareForShutdown(); } catch (...) { std::_Exit(83); }
            returned = true; release.join();
            if (early.load() || FAILED(released.load()) || graphics->GetCommandState() != State::Retired ||
                graphics->GetCommandList() || graphics->GetCompletedFenceValue() != 1) return 9;
        } else {
            graphics->CommandKick(); // Recover submitted work, never Close/Execute it again.
            if (closes != 1 || executions != 1 || graphics->GetCommandState() != State::Recording ||
                graphics->HasUnfencedSubmission() || graphics->GetCompletedFenceValue() != 1) return 10;
            graphics->BeginGpuFrameTiming(); graphics->Begin(); graphics->End();
            if (closes != 2 || executions != 2 || graphics->GetCommandState() != State::Recording) return 11;
        }
    }
    const auto beforeCloses = closes, beforeExecutions = executions;
    graphics->PrepareForShutdown(); graphics->PrepareForShutdown(); graphics->CommandKick();
    if (graphics->GetCommandState() != State::Retired || graphics->GetCommandList() ||
        !RecordingRefused(*graphics) || closes != beforeCloses || executions != beforeExecutions) return 12;
    graphics.reset(); window.Finalize();
    std::cout << "GPU_COMMAND_STATE mode=" << mode << " retired=1 repeated=1 closes=" << closes
        << " executions=" << executions << " signals=" << signals << " owners_retired=1\n";
    return 0;
}
