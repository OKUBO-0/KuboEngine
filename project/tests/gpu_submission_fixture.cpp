#include "DirectXCommon.h"
#include "WinApp.h"
#include <Windows.h>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>

bool presentFailure = true;
unsigned signalFailures = 0, signalAttempts = 0;
void FixtureBeforePresent() { if (presentFailure) throw std::runtime_error("owned post-submit operation failure"); }
HRESULT FixtureQueueSignal(ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64 value) {
    ++signalAttempts;
    if (signalFailures) { --signalFailures; return E_FAIL; }
    return queue->Signal(fence, value);
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto mode = std::string_view(argv[1]);
    const bool endSignalFailure = mode == "signal" || mode == "signal-repeated";
    if (mode != "normal" && mode != "present" && mode != "transient" && mode != "repeated" && !endSignalFailure) return 2;
    presentFailure = mode != "normal" && !endSignalFailure;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Engine::Base::WinApp window; window.Initialize(); ShowWindow(window.GetHwnd(), SW_HIDE);
    auto graphics = std::make_shared<Engine::Base::DirectXCommon>(); graphics->Initialize(&window);
    if (mode == "normal") {
        for (unsigned frame = 0; frame != 2; ++frame) {
            graphics->BeginGpuFrameTiming(); graphics->Begin(); graphics->End();
        }
        graphics->WaitForAllFrames();
        if (graphics->HasUnfencedSubmission() || graphics->GetPendingSubmissionFenceValue() != 3 ||
            graphics->GetCompletedFenceValue() != 2 || signalAttempts != 2) return 3;
        graphics.reset(); window.Finalize();
        std::cout << "GPU_LEDGER_NORMAL frames=2 signal_attempts=2 pending=3 completed=2 unfenced=0 retired=1\n";
        return 0;
    }
    Microsoft::WRL::ComPtr<ID3D12Fence> gate;
    if (FAILED(graphics->GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)))) return 4;
    // Present controls hold execution before End. Signal controls first reach
    // the real successful Present, then gate the cleanup fence separately.
    if (!endSignalFailure && FAILED(graphics->GetCommandQueue()->Wait(gate.Get(), 1))) return 4;
    signalFailures = endSignalFailure ? 1 : 0;
    bool original = false;
    try { graphics->BeginGpuFrameTiming(); graphics->Begin(); graphics->End(); }
    catch (const std::runtime_error& error) {
        const auto diagnostic = std::string_view(error.what());
        original = endSignalFailure
            ? diagnostic.starts_with("ID3D12CommandQueue::Signal failed with HRESULT 0x80004005")
            : diagnostic == "owned post-submit operation failure";
    }
    if (!original || !graphics->HasUnfencedSubmission() || graphics->GetPendingSubmissionFenceValue() != 1)
        std::_Exit(80); // Never destruct while the fixture's GPU queue is held.
    if (endSignalFailure) {
        if (signalAttempts != 1 || graphics->GetLastSubmissionFenceFailure() != E_FAIL ||
            graphics->GetCompletedFenceValue() != 0 ||
            FAILED(graphics->GetCommandQueue()->Wait(gate.Get(), 1))) std::_Exit(83);
        std::cout << "GPU_LEDGER_END_SIGNAL mode=" << mode
            << " original_signal_failure=1 unpublished_fence=1 submitted_unfenced=1 owners_retained=1\n" << std::flush;
    }
    const unsigned requestedFailures = mode == "transient" ? 1 : (mode == "repeated" || mode == "signal-repeated") ? 2 : 0;
    signalFailures = requestedFailures;
    for (unsigned attempt = 0; attempt != requestedFailures; ++attempt) {
        bool refused = false;
        try { graphics->WaitForAllFrames(); } catch (const std::runtime_error&) { refused = true; }
        if (!refused || !graphics->HasUnfencedSubmission() || graphics->GetPendingSubmissionFenceValue() != 1 ||
            graphics->GetCompletedFenceValue() != 0 || graphics->GetLastSubmissionFenceFailure() != E_FAIL ||
            !IsWindow(window.GetHwnd()) || !graphics->GetDevice()) std::_Exit(81);
        std::cout << "GPU_LEDGER_REFUSAL mode=" << mode << " attempt=" << attempt + 1
            << " unpublished_fence=1 submitted_unfenced=1 owners_retained=1 original_operation_preserved=1\n" << std::flush;
    }
    std::atomic<bool> waitEntered{false}, waitReturned{false}, early{false}, entryObserved{false};
    std::atomic<HRESULT> gateResult{E_FAIL};
    std::thread releaseGate([&] {
        const auto deadline = GetTickCount64() + 10000;
        while (!waitEntered.load() && GetTickCount64() < deadline) Sleep(1);
        entryObserved = waitEntered.load(); Sleep(250); early = waitReturned.load();
        gateResult = gate->Signal(1);
    });
    waitEntered = true;
    try { graphics->WaitForAllFrames(); } catch (...) { std::_Exit(82); }
    waitReturned = true; releaseGate.join();
    const bool valid = original && entryObserved.load() && !early.load() && SUCCEEDED(gateResult.load()) &&
        !graphics->HasUnfencedSubmission() && graphics->GetPendingSubmissionFenceValue() == 2 &&
        graphics->GetCompletedFenceValue() == 1 && signalAttempts == requestedFailures + 1 + (endSignalFailure ? 1 : 0) &&
        (requestedFailures == 0 || graphics->GetLastSubmissionFenceFailure() == E_FAIL);
    graphics->WaitForAllFrames();
    graphics.reset(); gate.Reset(); window.Finalize();
    if (!valid) return 1;
    std::cout << "GPU_LEDGER_RECOVERY mode=" << mode << " failures=" << requestedFailures
        << " fence_published_once=1 completed=1 wait_blocked=1 repeated_wait=1 owners_retired=1\n";
    return 0;
}
