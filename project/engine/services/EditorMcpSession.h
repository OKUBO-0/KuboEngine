#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <sddl.h>
#include <bcrypt.h>
#include "McpCommandBridge.h"
#include "McpDescriptorRetirement.h"
#include "McpProcessIdentity.h"
#include "McpStaleArchive.h"
#include "McpPipeConnection.h"
#include "../editor/EntityHistory.h"
#include <array>
#include <fstream>
#include <functional>
#include <vector>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <cmath>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace Engine::Services {
// Local, opt-in IPC. Tick is called only on the Editor thread; no worker ever
// accesses EntityWorld. The stdio MCP adapter connects to this live world.
class EditorMcpSession final {
public:
    using Value = Scene::SceneValue;
    EditorMcpSession() = default;
    EditorMcpSession(const EditorMcpSession&) = delete;
    EditorMcpSession& operator=(const EditorMcpSession&) = delete;
    ~EditorMcpSession() { Stop(); }
    bool IsRunning() const { return pipe_ != INVALID_HANDLE_VALUE; }
    static std::filesystem::path Descriptor(const std::filesystem::path& root) { return root / "Library/Mcp/editor-session.json"; }

    bool Start(const std::filesystem::path& root, std::string& error)
    {
        if (IsRunning()) { error = "MCP session is already enabled"; return false; }
        try {
            root_ = std::filesystem::absolute(root);
            const auto path = Descriptor(root_);
            std::filesystem::create_directories(path.parent_path());
            // CREATE_NEW remains authoritative across competing startup attempts.
            // Only proven stale metadata may be moved intact to a sibling archive.
            descriptor_ = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (descriptor_ == INVALID_HANDLE_VALUE) {
                const auto code = GetLastError();
                if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS) {
                    std::wstring archivedLeaf;
                    const auto archived = McpStaleArchive::Archive(root_, archivedLeaf);
                    if (archived == McpStaleArchive::State::Archived || archived == McpStaleArchive::State::Absent) {
                        // One bounded retry, still no replacement. A competing Editor
                        // can win this race; its descriptor must remain untouched.
                        descriptor_ = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                        if (archived == McpStaleArchive::State::Archived) {
                            OutputDebugStringW(L"KuboEngine preserved stale MCP descriptor: ");
                            OutputDebugStringW(archivedLeaf.c_str());
                            OutputDebugStringW(L"\n");
                        }
                    }
                }
            }
            if (descriptor_ == INVALID_HANDLE_VALUE) throw std::runtime_error("MCP session descriptor already exists or cannot be created; close the other Editor or inspect Library/Mcp/editor-session.json");
            descriptorOwned_ = true;
            if (!descriptorRetirement_.Capture(path.parent_path(), descriptor_))
                throw std::runtime_error("Cannot capture MCP descriptor ownership");
            std::array<unsigned char, 16> random{};
            if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) throw std::runtime_error("Cannot create MCP session identifier");
            pipeName_ = L"\\\\.\\pipe\\KuboEditor-";
            for (const auto byte : random) {
                constexpr wchar_t digits[] = L"0123456789abcdef";
                pipeName_ += digits[byte >> 4]; pipeName_ += digits[byte & 15];
            }
            TokenHandle token;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) throw std::runtime_error("Cannot query MCP pipe owner");
            DWORD bytes = 0;
            GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes);
            std::vector<unsigned char> user(bytes);
            const auto queried = GetTokenInformation(token.value, TokenUser, user.data(), bytes, &bytes);
            if (!queried) throw std::runtime_error("Cannot query MCP pipe user");
            SidBuffer sid;
            if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid.value)) throw std::runtime_error("Cannot encode MCP pipe user");
            const std::wstring acl = L"D:P(A;;GA;;;" + std::wstring(sid.value) + L")";
            SecurityBuffer security;
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &security.value, nullptr)) throw std::runtime_error("Cannot create MCP pipe ACL");
            SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security.value, FALSE};
            pipe_ = CreateNamedPipeW(pipeName_.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, &attributes);
            if (pipe_ == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create Editor MCP pipe");
            event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!event_) throw std::runtime_error("Cannot create MCP I/O event");
            Value document;
            document["format"] = "kubo-editor-mcp-2";
            std::string asciiName;
            for (const auto character : pipeName_) asciiName += static_cast<char>(character); // Generated ASCII only.
            document["pipe"] = asciiName;
            document["pid"] = double(GetCurrentProcessId());
            McpProcessIdentity identity;
            if (!McpProcessIdentity::Current(identity)) throw std::runtime_error("Cannot capture MCP owner creation time");
            // Two exact uint32 numbers avoid losing FILETIME precision in JSON doubles.
            document["process_creation_high"] = double(identity.creation.dwHighDateTime);
            document["process_creation_low"] = double(identity.creation.dwLowDateTime);
            const auto text = document.dump();
            DWORD written = 0;
            if (!WriteFile(descriptor_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) || written != text.size() || !FlushFileBuffers(descriptor_)) throw std::runtime_error("Cannot publish MCP session descriptor");
            state_ = State::Connect;
            pending_ = false;
            error.clear();
            return true;
        } catch (const std::exception& e) { Stop(); error = e.what(); return false; }
    }

    void Stop()
    {
        CancelPending();
        if (pipe_ != INVALID_HANDLE_VALUE) { DisconnectNamedPipe(pipe_); CloseHandle(pipe_); pipe_ = INVALID_HANDLE_VALUE; }
        if (event_) { CloseHandle(event_); event_ = nullptr; }
        if (descriptor_ != INVALID_HANDLE_VALUE) {
            // Keep the original read-sharing contract until publication ends.
            // Retire relative to the pinned parent, never by a replaced root path.
            CloseHandle(descriptor_); descriptor_ = INVALID_HANDLE_VALUE;
            if (descriptorOwned_ && !descriptorRetirement_.Retire())
                OutputDebugStringA("KuboEngine cannot retire owned MCP descriptor; retained for inspection\n");
        }
        descriptorRetirement_.Reset();
        descriptorOwned_ = false;
        reply_.clear();
    }

    void Tick(Scene::EntityWorld& world, bool editing, Editor::EntityHistory* history = nullptr, Scene::EntityId selection = 0,
        const std::function<void(const std::filesystem::path&, const Scene::EntityWorld&)>& onSaved = {},Editor::EntitySelection* fullSelection=nullptr,const Assets::AssetRegistry* assets=nullptr,
        const std::function<Value()>& readEditorStatus = {})
    {
        if (!IsRunning()) return;
        if (state_ != State::Connect && GetTickCount64() - activeSince_ > 10000) { Reconnect(); return; }
        for (int budget = 0; budget < 4; ++budget) {
            DWORD count = 0;
            if (pending_) {
                if (!GetOverlappedResult(pipe_, &overlapped_, &count, FALSE)) {
                    if (GetLastError() == ERROR_IO_INCOMPLETE) return;
                    Reconnect(); return;
                }
                pending_ = false;
            } else {
                ResetEvent(event_);
                overlapped_ = {};
                overlapped_.hEvent = event_;
                BOOL completed = FALSE;
                if (state_ == State::Connect) completed = ConnectNamedPipe(pipe_, &overlapped_);
                else if (state_ == State::Read) completed = ReadFile(pipe_, request_.data(), static_cast<DWORD>(request_.size()), &count, &overlapped_);
                else completed = WriteFile(pipe_, reply_.data(), static_cast<DWORD>(reply_.size()), &count, &overlapped_);
                if (!completed) {
                    const auto code = GetLastError();
                    if (state_ == State::Connect && code == ERROR_PIPE_CONNECTED) completed = TRUE;
                    else if (code == ERROR_IO_PENDING) { pending_ = true; return; }
                    else { Reconnect(); return; }
                }
            }
            if (state_ == State::Connect) { state_ = State::Read; activeSince_ = GetTickCount64(); }
            else if (state_ == State::Write) {
                if (count != reply_.size()) { Reconnect(); return; }
                state_ = State::Read;
                activeSince_ = GetTickCount64();
            } else {
                if (!count) { Reconnect(); return; }
                try {
                    const auto request = Value::parse(std::string(request_.data(), count));
                    const auto method = request.is_object() && request.contains("method") && request["method"].is_string()
                        ? request["method"].get<crude_json::string>() : std::string{};
                    Value response;
                    if (method == "editor.get_status") {
                        response["jsonrpc"] = "2.0";
                        response["id"] = request.contains("id") ? request["id"] : Value(nullptr);
                        if (request.contains("params") &&
                            (!request["params"].is_object() || !request["params"].get<crude_json::object>().empty())) {
                            response["error"]["code"] = -32602.0;
                            response["error"]["message"] = "editor.get_status accepts no arguments";
                        } else if (readEditorStatus) {
                            response["result"] = readEditorStatus();
                        } else {
                            response["error"]["code"] = -32000.0;
                            response["error"]["message"] = "Editor status is unavailable for this session";
                        }
                    } else if (method == "scene.list_entities") {
                        response = McpCommandBridge(root_, assets).Handle(request, world);
                    } else if (!editing) {
                        response["jsonrpc"] = "2.0";
                        response["id"] = request.is_object() && request.contains("id") ? request["id"] : Value(nullptr);
                        response["error"]["code"] = -32000.0;
                        response["error"]["message"] = "Scene editing is locked by playback, a pending document action or an active authoring operation";
                    } else {
                        Scene::EntityWorld backup;
                        if (editing && history) backup = world;
                        const auto before = editing && history ? backup.Capture() : Value{};
                        Editor::EntitySelection selectedBefore;
                        if(fullSelection)selectedBefore=*fullSelection;else selectedBefore.Replace(selection);
                        response = McpCommandBridge(root_,assets).Handle(request, world);
                        if (!before.is_null() && before.dump() != world.Capture().dump()) {
                            // One command per accepted IPC request, distinct from a UI drag.
                            history->Commit();
                            std::string historyError;
                            auto selectedAfter=selectedBefore;auto primary=selectedAfter.Primary();selectedAfter.Synchronize(world,primary);
                            if (!history->Record(before, world, selectedBefore.Capture(), selectedAfter.Capture(), historyError)) {
                                // Preserve allocation counters and transient component
                                // fields too, not just serialized authored properties.
                                world = std::move(backup);
                                response = Value{};
                                response["jsonrpc"] = "2.0";
                                response["id"] = request.contains("id") ? request["id"] : Value(nullptr);
                                response["error"]["code"] = -32000.0;
                                response["error"]["message"] = "MCP edit rejected: " + historyError;
                            } else {history->Commit();if(fullSelection)*fullSelection=std::move(selectedAfter);selection=primary;}
                        }
                        if (onSaved && response.contains("result") && request["method"].get<crude_json::string>() == "scene.save") {
                            const auto& text = request["params"]["path"].get<crude_json::string>();
                            std::u8string utf8(text.size(), u8'\0');
                            std::memcpy(utf8.data(), text.data(), text.size());
                            onSaved(root_ / std::filesystem::path(utf8), world);
                        }
                    }
                    reply_ = response.dump();
                } catch (const std::exception& e) {
                    Value response;
                    response["jsonrpc"] = "2.0"; response["id"] = nullptr;
                    response["error"]["code"] = -32700.0; response["error"]["message"] = e.what();
                    reply_ = response.dump();
                }
                if (reply_.size() > 4 * 1024 * 1024) { Reconnect(); return; }
                state_ = State::Write;
                activeSince_ = GetTickCount64();
            }
        }
    }

    static Value Call(const std::filesystem::path& root, const Value& request)
    {
        const auto path = Descriptor(std::filesystem::absolute(root));
        if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 4096) throw std::runtime_error("Enable MCP in the opened project's Editor first");
        std::ifstream input(path, std::ios::binary);
        char bytes[4097]{};
        input.read(bytes, sizeof(bytes));
        const auto count = input.gcount();
        if (count <= 0 || count > 4096 || input.bad()) throw std::runtime_error("Invalid Editor MCP descriptor size/read");
        const std::string text(bytes, static_cast<size_t>(count));
        input.close(); // Do not hold a read lease throughout an asynchronous request.
        if (!McpDescriptorValidation::Preflight(text.data(), text.size())) throw std::runtime_error("Invalid Editor MCP descriptor");
        const auto document = Value::parse(text);
        if (document.is_object() && document.contains("format") && document["format"].is_string() &&
            document["format"].get<crude_json::string>() == "kubo-editor-mcp-1")
            throw std::runtime_error("Legacy Editor MCP descriptor; restart the Editor with the matching engine version");
        if (!document.is_object() || !document.contains("format") || !document["format"].is_string() || document["format"].get<crude_json::string>() != "kubo-editor-mcp-2" ||
            !document.contains("pipe") || !document["pipe"].is_string()) throw std::runtime_error("Invalid Editor MCP descriptor");
        const auto integer = [&document](const char* key) -> DWORD {
            if (!document.contains(key) || !document[key].is_number()) throw std::runtime_error("Invalid Editor MCP process identity");
            const auto value = document[key].get<crude_json::number>();
            if (!std::isfinite(value) || value < 0 || value > 4294967295.0 || std::floor(value) != value)
                throw std::runtime_error("Invalid Editor MCP process identity");
            return static_cast<DWORD>(value);
        };
        McpProcessIdentity identity;
        identity.pid = integer("pid");
        identity.creation.dwHighDateTime = integer("process_creation_high");
        identity.creation.dwLowDateTime = integer("process_creation_low");
        if (!identity.pid || (!identity.creation.dwHighDateTime && !identity.creation.dwLowDateTime))
            throw std::runtime_error("Invalid Editor MCP process identity");
        McpProcessLease owner;
        switch (owner.Acquire(identity)) {
        case McpProcessLease::State::Alive: break;
        case McpProcessLease::State::Exited: throw std::runtime_error("Editor MCP owner has exited; inspect the retained session descriptor");
        case McpProcessLease::State::DifferentProcess: throw std::runtime_error("Editor MCP owner creation time changed; inspect the retained session descriptor");
        default: throw std::runtime_error("Cannot verify Editor MCP owner process");
        }
        const auto name = document["pipe"].get<crude_json::string>();
        constexpr const char* prefix = "\\\\.\\pipe\\KuboEditor-";
        if (name.rfind(prefix, 0) != 0 || name.size() != std::strlen(prefix) + 32 ||
            !std::all_of(name.begin() + std::strlen(prefix), name.end(), [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) throw std::runtime_error("Invalid Editor MCP endpoint");
        const std::wstring pipeName(name.begin(), name.end());
        const auto connection=McpPipeConnection::Connect(pipeName.c_str());
        HANDLE pipe=connection.handle;
        if (pipe == INVALID_HANDLE_VALUE) throw std::runtime_error(std::string("Cannot connect to the enabled Editor MCP session before sending request: phase=")+
            McpPipeConnection::PhaseName(connection.phase)+", win32="+std::to_string(connection.error)+", attempts="+std::to_string(connection.attempts));
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event) { CloseHandle(pipe); throw std::runtime_error("Cannot create MCP client event"); }
        try {
            if (!owner.OwnsPipe(pipe)) throw std::runtime_error("Editor MCP endpoint is not owned by the published live process");
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr)) throw std::runtime_error("Cannot configure MCP client");
            auto outgoing = request.dump();
            if (outgoing.empty() || outgoing.size() > 65536) throw std::runtime_error("MCP request exceeds 64 KiB");
            const auto deadline = GetTickCount64() + 5000;
            Transfer(pipe, event, outgoing.data(), static_cast<DWORD>(outgoing.size()), true, deadline);
            std::string incoming(4 * 1024 * 1024, '\0');
            const auto read = Transfer(pipe, event, incoming.data(), static_cast<DWORD>(incoming.size()), false, deadline);
            incoming.resize(read);
            const auto response = Value::parse(incoming);
            CloseHandle(event); CloseHandle(pipe);
            return response;
        } catch (...) { CloseHandle(event); CloseHandle(pipe); throw; }
    }
private:
    struct TokenHandle { HANDLE value = nullptr; ~TokenHandle() { if (value) CloseHandle(value); } };
    struct SidBuffer { LPWSTR value = nullptr; ~SidBuffer() { if (value) LocalFree(value); } };
    struct SecurityBuffer { PSECURITY_DESCRIPTOR value = nullptr; ~SecurityBuffer() { if (value) LocalFree(value); } };
    enum class State { Connect, Read, Write };
    void CancelPending() {
        if (pending_ && pipe_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(pipe_, &overlapped_);
            DWORD ignored = 0;
            GetOverlappedResult(pipe_, &overlapped_, &ignored, TRUE);
        }
        pending_ = false;
    }
    void Reconnect() { CancelPending(); DisconnectNamedPipe(pipe_); state_ = State::Connect; reply_.clear(); }
    static DWORD Transfer(HANDLE pipe, HANDLE event, void* bytes, DWORD length, bool write, ULONGLONG deadline)
    {
        OVERLAPPED operation{}; operation.hEvent = event; ResetEvent(event);
        DWORD count = 0;
        const BOOL completed = write ? WriteFile(pipe, bytes, length, &count, &operation) : ReadFile(pipe, bytes, length, &count, &operation);
        if (!completed) {
            if (GetLastError() != ERROR_IO_PENDING) throw std::runtime_error("Editor MCP transfer failed");
            const auto now = GetTickCount64();
            const auto wait = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
            if (WaitForSingleObject(event, wait) != WAIT_OBJECT_0) {
                CancelIoEx(pipe, &operation);
                GetOverlappedResult(pipe, &operation, &count, TRUE);
                throw std::runtime_error("Editor MCP request timed out");
            }
            if (!GetOverlappedResult(pipe, &operation, &count, FALSE)) throw std::runtime_error("Editor MCP transfer failed");
        }
        if (!count || (write && count != length)) throw std::runtime_error("Incomplete Editor MCP message");
        return count;
    }
    HANDLE pipe_ = INVALID_HANDLE_VALUE, descriptor_ = INVALID_HANDLE_VALUE, event_ = nullptr;
    bool descriptorOwned_ = false, pending_ = false;
    McpDescriptorRetirement descriptorRetirement_;
    std::filesystem::path root_;
    std::wstring pipeName_;
    State state_ = State::Connect;
    OVERLAPPED overlapped_{};
    ULONGLONG activeSince_ = 0;
    std::array<char, 65536> request_{};
    std::string reply_;
};
}
