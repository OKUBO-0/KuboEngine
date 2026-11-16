#pragma once

#include "engine/services/EnginePackageManifest.h"
#include "engine/services/UpdateService.h"
#include <winhttp.h>
#include <atomic>
#include <memory>

#pragma comment(lib, "Winhttp.lib")

namespace KuboHub {

struct UpdateUrl {
    std::wstring host, target;
    INTERNET_PORT port = 0;
};

inline bool ParseUpdateUrl(const std::wstring& url, UpdateUrl& output)
{
    if (url.size() > 2048 || !url.starts_with(L"https://") ||
        std::any_of(url.begin(), url.end(), [](wchar_t c) {
            return c <= 32 || c >= 127 || c == L'#' || c == L'\\';
        })) return false;
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
    parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || !parts.dwHostNameLength ||
        parts.dwUserNameLength || parts.dwPasswordLength) return false;
    output.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    output.target = parts.dwUrlPathLength ? std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) : L"/";
    if (parts.dwExtraInfoLength) output.target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    output.port = parts.nPort;
    return true;
}

struct CloseHttpHandle {
    void operator()(HINTERNET handle) const noexcept { WinHttpCloseHandle(handle); }
};
using HttpHandle = std::unique_ptr<void, CloseHttpHandle>;

template<class Sink>
bool FetchUpdateBytes(const UpdateUrl& url, uint64_t limit, const std::atomic_bool& cancelled,
    Sink write, std::wstring& error, ULONGLONG timeout = 8000)
{
    const auto expires = GetTickCount64() + timeout;
    const auto stopped = [&] { return cancelled.load(std::memory_order_relaxed) || GetTickCount64() >= expires; };
    const auto fail = [&](const wchar_t* message) { error = message; return false; };
    if (stopped()) return fail(L"更新の確認を中止しました。");
    HttpHandle session(WinHttpOpen(L"KuboEngine Hub/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session || !WinHttpSetTimeouts(session.get(), 2000, 2000, 2000, 2000))
        return fail(L"更新の接続を初期化できませんでした。");
    HttpHandle connection(WinHttpConnect(session.get(), url.host.c_str(), url.port, 0));
    if (!connection) return fail(L"更新元に接続できませんでした。");
    HttpHandle request(WinHttpOpenRequest(connection.get(), L"GET", url.target.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE | WINHTTP_FLAG_ESCAPE_DISABLE));
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    DWORD autoLogon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if (!request || !WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)) ||
        !WinHttpSetOption(request.get(), WINHTTP_OPTION_AUTOLOGON_POLICY, &autoLogon, sizeof(autoLogon)) ||
        !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
        stopped() || !WinHttpReceiveResponse(request.get(), nullptr))
        return fail(L"更新元へ接続できませんでした。ネットワークと配布元を確認してください。");
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200)
        return fail(L"更新元から有効な応答がありませんでした。");
    uint64_t total = 0;
    char buffer[16384];
    while (!stopped()) {
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), buffer, sizeof(buffer), &read))
            return fail(L"更新データの読み込みに失敗しました。");
        if (!read) return true;
        if (read > limit - total || !write(buffer, read))
            return fail(L"更新データが上限を超えたか、保存できませんでした。");
        total += read;
    }
    return fail(L"更新の取得を中止しました。");
}

struct UpdateFeedResult {
    enum class State { Unavailable, Invalid, Current, Available };
    State state = State::Unavailable;
    std::string version, notes, manifestUrl, manifestHash;
    std::wstring error;
};

inline constexpr size_t UpdateFeedLimit = 64 * 1024;

inline UpdateFeedResult DecodeUpdateFeed(std::string_view bytes, const std::string& current,
    const UpdateUrl& feed)
{
    UpdateFeedResult result;
    result.state = UpdateFeedResult::State::Invalid;
    result.error = L"更新情報の形式が正しくありません。";
    try {
        std::string error;
        if (bytes.starts_with("\xEF\xBB\xBF")) bytes.remove_prefix(3);
        if (!Engine::Base::BoundedJson::Validate(bytes, {UpdateFeedLimit, 4, 32}, error)) return result;
        const auto data = crude_json::value::parse(std::string(bytes));
        if (!data.is_object()) return result;
        const auto read = [&](const char* key) {
            return data.contains(key) && data[key].is_string() ? data[key].get<crude_json::string>() : std::string{};
        };
        if (read("format") != "kubo-hub-update-1" || read("platform") != "Windows-x64") return result;
        result.version = read("version");
        result.notes = read("notes");
        result.manifestUrl = read("manifestUrl");
        result.manifestHash = read("manifestSha256");
        UpdateUrl package;
        if (!Engine::Services::EnginePackageManifest::ValidVersion(result.version) ||
            !Engine::Services::EnginePackageManifest::ValidVersion(current) ||
            result.notes.size() > 8192 || result.notes.find('\0') != std::string::npos ||
            !ParseUpdateUrl(std::wstring(result.manifestUrl.begin(), result.manifestUrl.end()), package) ||
            CompareStringOrdinal(package.host.c_str(), -1, feed.host.c_str(), -1, TRUE) != CSTR_EQUAL ||
            package.port != feed.port || package.target.find(L'?') != std::wstring::npos ||
            !package.target.ends_with(L"/engine-package.json") ||
            result.manifestHash.size() != 64 ||
            !std::all_of(result.manifestHash.begin(), result.manifestHash.end(), [](unsigned char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            })) return result;
        result.state = Engine::Services::UpdateService::CompareVersions(result.version, current) > 0
            ? UpdateFeedResult::State::Available : UpdateFeedResult::State::Current;
        result.error.clear();
    } catch (...) {}
    return result;
}

inline UpdateFeedResult CheckUpdateFeed(std::wstring source, std::string current,
    const std::atomic_bool& cancelled)
{
    UpdateFeedResult result;
    UpdateUrl url;
    if (!ParseUpdateUrl(source, url)) {
        result.state = UpdateFeedResult::State::Invalid;
        result.error = L"更新元には認証情報を含まないHTTPS URLを指定してください。";
        return result;
    }
    std::string bytes;
    if (!FetchUpdateBytes(url, UpdateFeedLimit, cancelled, [&](const char* data, DWORD length) {
        bytes.append(data, length);
        return true;
    }, result.error)) return result;
    return DecodeUpdateFeed(bytes, current, url);
}

} // namespace KuboHub
