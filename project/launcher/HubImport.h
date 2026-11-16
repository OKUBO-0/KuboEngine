#pragma once

struct HubImportResult {
    std::filesystem::path project, retained;
    std::wstring message;
};
std::future<HubImportResult> g_importWorker;
std::atomic_bool g_importCancelled{false};

HubImportResult ImportHubProject(std::wstring url, std::filesystem::path destination)
{
    HubImportResult result;
    try {
        namespace fs = std::filesystem;
        if (!Engine::Base::PlainLocalDirectories(destination.parent_path(),true) || fs::exists(destination)) {
            result.message = L"取得先には新しいローカルフォルダーを指定してください。";
            return result;
        }
        fs::create_directories(destination.parent_path());
        static std::atomic<uint64_t> sequence{0};
        const auto stage = destination.parent_path() / (L".kubo-clone-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence));
        if (!fs::create_directory(stage)) {
            result.message = L"取得用の作業フォルダーを作成できません。";
            return result;
        }
        result.retained = stage;
        std::string error;
        if (!Engine::Services::GitWorkspace::CloneHttps(url,stage,g_importCancelled,error)) {
            result.message = L"Gitの取得に失敗しました。\r\n" + NativeCreationMessage(error);
            return result;
        }
        Engine::Base::ProjectContext project;
        if (!project.Open(stage,error)) {
            result.message = L"取得したリポジトリの直下に開ける .kubo がありません。";
            return result;
        }
        const auto manifest = project.GetProjectFile().filename();
        const auto flags = GetFileAttributesW((stage/manifest).c_str());
        if (flags == INVALID_FILE_ATTRIBUTES || (flags & FILE_ATTRIBUTE_REPARSE_POINT) ||
            g_importCancelled.load(std::memory_order_relaxed) ||
            !MoveFileExW(stage.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH)) {
            result.message = L"取得したフォルダーを公開できませんでした。";
            return result;
        }
        result.retained.clear();
        result.project = destination / manifest;
    } catch (...) {
        result.message = L"Gitからプロジェクトを取得できませんでした。";
    }
    return result;
}

void PollHubImport(HWND window)
{
    if (!g_importWorker.valid() ||
        g_importWorker.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    SetWindowTextW(GetDlgItem(window,kImportStart),L"プロジェクトを取得");
    EnableWindow(GetDlgItem(window,kImportStart),TRUE);
    try {
        auto result = g_importWorker.get();
        if (!result.project.empty()) {
            AddRecent(result.project);
            PopulateRecent();
            SelectHubProject(result.project);
            SwitchHubPage(window,HubPage::Projects);
            SetWindowTextW(GetDlgItem(window,kEditorSessionStatus),L"取得しました。内容を確認してエディタで開けます。");
            SetWindowTextW(GetDlgItem(window,kImportStatus),L"取得が完了しました。");
        } else {
            if (!result.retained.empty()) result.message += L"\r\n作業フォルダー  " + result.retained.wstring();
            SetWindowTextW(GetDlgItem(window,kImportStatus),result.message.c_str());
        }
    } catch (...) {
        SetWindowTextW(GetDlgItem(window,kImportStatus),L"取得結果を表示できません。");
    }
}

void BeginHubImport(HWND window)
{
    if (g_importWorker.valid()) {
        g_importCancelled.store(true,std::memory_order_relaxed);
        EnableWindow(GetDlgItem(window,kImportStart),FALSE);
        SetWindowTextW(GetDlgItem(window,kImportStatus),L"取得の中止を待っています。");
        return;
    }
    auto url = HubText(window,kImportUrl);
    KuboHub::UpdateUrl parsed;
    if (!KuboHub::ParseUpdateUrl(url,parsed) || url.find_first_of(L"@?") != std::wstring::npos || parsed.target == L"/") {
        SetWindowTextW(GetDlgItem(window,kImportStatus),L"認証情報を含まないリポジトリのHTTPS URLを指定してください。");
        return;
    }
    std::filesystem::path manifest;
    std::wstring message;
    if (!HubCreationDestination(HubText(window,kImportName),HubText(window,kImportLocation),false,{},manifest,message)) {
        SetWindowTextW(GetDlgItem(window,kImportStatus),message.c_str());
        return;
    }
    try {
        g_importCancelled.store(false,std::memory_order_relaxed);
        g_importWorker = std::async(std::launch::async,ImportHubProject,std::move(url),manifest.parent_path());
        SetWindowTextW(GetDlgItem(window,kImportStart),L"取得を中止");
        SetWindowTextW(GetDlgItem(window,kImportStatus),L"Gitから取得しています…");
    } catch (...) {
        SetWindowTextW(GetDlgItem(window,kImportStatus),L"取得を開始できませんでした。");
    }
}

void RetireHubImport()
{
    g_importCancelled.store(true,std::memory_order_relaxed);
    if (g_importWorker.valid()) {
        try { g_importWorker.get(); } catch (...) {}
    }
}
