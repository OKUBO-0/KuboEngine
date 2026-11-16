#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cwchar>
#include <memory>
#include <vector>

namespace Engine::Services {

// Async prebuilt packaging or trusted SDK-native build/export. No shell command
// evaluation; only the native route requires a compatible C++ toolchain.
class ExportJob final {
    friend struct ExportJobChecks; // White-box process membership checks; no runtime API/layout change.
public:
    enum class State { Idle, Running, Succeeded, Failed, Cancelled };
    // Closing Editor keeps existing export behavior: no kill-on-job-close limit.
    ~ExportJob() { if (process_) CloseHandle(process_); if (job_) CloseHandle(job_); }
    ExportJob() = default;
    ExportJob(const ExportJob&) = delete;
    ExportJob& operator=(const ExportJob&) = delete;

    static std::wstring QuoteArgument(const std::wstring& value)
    {
        std::wstring result = L"\"";
        size_t slashes = 0;
        for (const auto ch : value) {
            if (ch == L'\\') { ++slashes; continue; }
            result.append(slashes * (ch == L'"' ? 2 : 1), L'\\');
            slashes = 0;
            if (ch == L'"') result += L'\\';
            result += ch;
        }
        result.append(slashes * 2, L'\\');
        return result + L'"';
    }

    bool Start(const std::filesystem::path& project, const std::filesystem::path& output,
        const std::filesystem::path& script, const std::filesystem::path& runtime, std::string& error)
    {
        try {
            return StartCommand(project, output, script, runtime,
                L" -Configuration Release -RuntimeExecutable " + QuoteArgument(std::filesystem::absolute(runtime).wstring()), error);
        } catch (const std::exception& e) { error = e.what(); return false; }
    }

    bool StartNative(const std::filesystem::path& project, const std::filesystem::path& output,
        const std::filesystem::path& sdk, const std::filesystem::path& nativeProject, bool debug, std::string& error)
    {
        Poll();
        if (state_ == State::Running) { error = "An export is already running"; return false; }
        try {
            const auto root = std::filesystem::absolute(sdk).lexically_normal();
            const auto consumer = std::filesystem::absolute(nativeProject).lexically_normal();
            if (!std::filesystem::is_regular_file(root / "sdk-package.json") || consumer.extension() != L".vcxproj")
                throw std::runtime_error("Select a Runtime SDK and trusted .vcxproj application project");
            return StartCommand(project, output, root / "tools/build_native_game.ps1", consumer,
                L" -Configuration " + std::wstring(debug ? L"Debug" : L"Release") +
                L" -NativeProject " + QuoteArgument(consumer.wstring()) + L" -SdkDirectory " + QuoteArgument(root.wstring()), error);
        } catch (const std::exception& e) { error = e.what(); return false; }
    }

private:
    static std::vector<wchar_t> PowerShellEnvironment(const std::filesystem::path& powershell)
    {
        // Native intermediaries do not apply PowerShell 7's WinPS path fixup.
        // Our bundled scripts use Windows PowerShell built-ins; resolve these
        // only from the chosen host. Do not mutate the Editor/global environment.
        std::unique_ptr<wchar_t,decltype(&FreeEnvironmentStringsW)> inherited(GetEnvironmentStringsW(),&FreeEnvironmentStringsW);
        if(!inherited) throw std::runtime_error("Cannot snapshot export environment");
        std::vector<std::wstring> entries;
        for(const auto* entry=inherited.get();*entry;entry+=std::wcslen(entry)+1) {
            std::wstring value(entry);
            const auto separator=value.find(L'=');
            if(_wcsicmp(value.substr(0,separator).c_str(),L"PSModulePath")!=0) entries.push_back(std::move(value));
        }
        entries.push_back(L"PSModulePath="+(powershell.parent_path()/"Modules").wstring());
        std::sort(entries.begin(),entries.end(),[](const auto& left,const auto& right) {
            return CompareStringOrdinal(left.c_str(),-1,right.c_str(),-1,TRUE)==CSTR_LESS_THAN;
        });
        std::vector<wchar_t> block;
        for(const auto& entry:entries) {block.insert(block.end(),entry.begin(),entry.end());block.push_back(L'\0');}
        block.push_back(L'\0');return block;
    }

    bool StartCommand(const std::filesystem::path& project, const std::filesystem::path& output,
        const std::filesystem::path& script, const std::filesystem::path& requiredInput,
        const std::wstring& arguments, std::string& error)
    {
        Poll();
        if (state_ == State::Running) { error = "An export is already running"; return false; }
        try {
            const auto manifest = std::filesystem::absolute(project);
            const auto packager = std::filesystem::absolute(script);
            const auto player = std::filesystem::absolute(requiredInput);
            if (!std::filesystem::is_regular_file(manifest) || !std::filesystem::is_regular_file(packager) ||
                !std::filesystem::is_regular_file(player)) throw std::runtime_error("Project, export script or executable/native project is missing");
            output_ = std::filesystem::absolute(output);
            const auto logs = manifest.parent_path() / "Library/Logs";
            std::filesystem::create_directories(logs);
            log_ = logs / ("export-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()) + ".txt");
            wchar_t systemDirectory[MAX_PATH]{};
            const auto systemLength=GetSystemDirectoryW(systemDirectory,MAX_PATH);
            if (!systemLength || systemLength>=MAX_PATH) throw std::runtime_error("Cannot find Windows PowerShell");
            const auto powershell = std::filesystem::path(systemDirectory) / "WindowsPowerShell/v1.0/powershell.exe";
            auto environment=PowerShellEnvironment(powershell);
            std::wstring command = QuoteArgument(powershell.wstring()) + L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
                QuoteArgument(packager.wstring()) + L" -ProjectFile " + QuoteArgument(manifest.wstring()) +
                L" -OutputDirectory " + QuoteArgument(output_.wstring()) + arguments;
            SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
            HANDLE logHandle = CreateFileW(log_.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (logHandle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create export log");
            HANDLE inputHandle = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (inputHandle == INVALID_HANDLE_VALUE) { CloseHandle(logHandle); throw std::runtime_error("Cannot open export input"); }
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = inputHandle;
            startup.hStdOutput = startup.hStdError = logHandle;
            PROCESS_INFORMATION process{};
            const auto directory = manifest.parent_path().wstring();
            const BOOL started = CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, TRUE,
                CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, environment.data(), directory.c_str(), &startup, &process);
            const auto lastError = GetLastError();
            CloseHandle(inputHandle);
            CloseHandle(logHandle);
            if (!started) throw std::runtime_error("Cannot start export process: " + std::to_string(lastError));
            const auto ownedJob=CreateJobObjectW(nullptr,nullptr);
            if(!ownedJob || !AssignProcessToJobObject(ownedJob,process.hProcess)) {
                const auto code=GetLastError();TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,5000);
                CloseHandle(process.hThread);CloseHandle(process.hProcess);if(ownedJob) CloseHandle(ownedJob);
                throw std::runtime_error("Cannot contain export process before starting it: "+std::to_string(code));
            }
            if(ResumeThread(process.hThread)==static_cast<DWORD>(-1)) {
                const auto code=GetLastError();TerminateJobObject(ownedJob,1);WaitForSingleObject(process.hProcess,5000);
                CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(ownedJob);
                throw std::runtime_error("Cannot resume contained export process: "+std::to_string(code));
            }
            CloseHandle(process.hThread);
            process_ = process.hProcess;
            job_ = ownedJob;
            state_ = State::Running;
            cancellationRequested_=false;rootFinished_=false;controlError_.clear();
            exitCode_ = STILL_ACTIVE;
            error.clear();
            return true;
        } catch (const std::exception& e) { error = e.what(); return false; }
    }

public:
    bool Cancel(std::string& error)
    {
        Poll();
        if(state_!=State::Running || rootFinished_) {error="Export root process has already finished";return false;}
        if(cancellationRequested_) {error.clear();return true;}
        if(!job_ || !TerminateJobObject(job_,ERROR_CANCELLED)) {
            error="Cannot cancel owned export process tree (Windows error "+std::to_string(GetLastError())+")";return false;
        }
        cancellationRequested_=true;error.clear();return true;
    }
    bool IsCancellationRequested() const {return cancellationRequested_;}
    const std::string& GetControlError() const {return controlError_;}
    void Poll()
    {
        if (!process_ || state_ != State::Running) return;
        if(!rootFinished_) {
            const auto result = WaitForSingleObject(process_, 0);
            if (result == WAIT_TIMEOUT) return;
            if (result == WAIT_FAILED || !GetExitCodeProcess(process_, &exitCode_)) exitCode_ = 1;
            rootFinished_=true;
        }
        if(cancellationRequested_) {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
            if(!QueryInformationJobObject(job_,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr)) {
                controlError_="Cannot confirm owned export process tree completion";return;
            }
            if(accounting.ActiveProcesses!=0) return;
        }
        // Normal completion retains the existing script-exit contract. Do not
        // kill persistent compiler servers merely because a successful script
        // exited; they can be shared by other builds. Only explicit Cancel kills.
        controlError_.clear();
        state_ = cancellationRequested_ ? State::Cancelled : exitCode_ == 0 ? State::Succeeded : State::Failed;
        if(cancellationRequested_) exitCode_=ERROR_CANCELLED;
        CloseHandle(process_);
        process_ = nullptr;
        CloseHandle(job_);job_=nullptr;
    }
    State GetState() const { return state_; }
    DWORD GetExitCode() const { return exitCode_; }
    const std::filesystem::path& GetOutputPath() const { return output_; }
    const std::filesystem::path& GetLogPath() const { return log_; }
    std::string ReadLog() const
    {
        std::ifstream input(log_, std::ios::binary | std::ios::ate);
        if (!input) return {};
        const auto end = input.tellg();
        if (end < 0) return {};
        const auto size = static_cast<size_t>(end);
        const auto count = size < 256 * 1024 ? size : 256 * 1024;
        input.seekg(static_cast<std::streamoff>(size - count));
        std::string text(count, '\0');
        input.read(text.data(), static_cast<std::streamsize>(count));
        text.resize(static_cast<size_t>(input.gcount()));
        return text;
    }
private:
    HANDLE process_ = nullptr;
    HANDLE job_ = nullptr;
    bool cancellationRequested_=false,rootFinished_=false;
    std::string controlError_;
    State state_ = State::Idle;
    DWORD exitCode_ = 0;
    std::filesystem::path output_, log_;
};
}
