#pragma once

// Included inside the Launcher private namespace, after the shared layout.
std::future<KuboHub::UpdateFeedResult> g_updateWorker;
std::future<KuboHub::UpdateInstallation> g_updateInstallation;
std::atomic<uint64_t> g_updateBytes{0};
std::atomic_bool g_updateCancelled{false};
std::wstring g_updateSource;
std::string g_updateCurrentVersion;
std::filesystem::path g_updateEditor;
KuboHub::UpdateFeedResult g_updateResult;

std::filesystem::path HubUpdateFile(std::wstring_view name)
{
    return g_recentFile.parent_path() / name;
}

std::string UpdateSourceBytes(std::wstring_view source)
{
    std::string bytes;
    bytes.reserve(source.size());
    for (wchar_t character : source) bytes.push_back(static_cast<char>(character));
    return bytes;
}

std::string ReadHubUpdatePreference(std::wstring_view name)
{
    std::ifstream input(HubUpdateFile(name), std::ios::binary);
    char bytes[2304]{};
    input.read(bytes, sizeof(bytes));
    if (!input.eof() || input.bad()) return {};
    return {bytes, static_cast<size_t>(input.gcount())};
}

void SetHubUpdateStatus(HWND window, const std::wstring& text, bool available)
{
    SetWindowTextW(GetHubControl(window,kUpdateDetails), text.c_str());
    SetWindowTextW(GetHubControl(window,kUpdateBadge), available ? L"更新があります" : L"更新情報");
    EnableWindow(GetHubControl(window,kLaterUpdate), available);
    EnableWindow(GetHubControl(window,kSkipUpdate), available);
    if (!g_updateInstallation.valid()) EnableWindow(GetHubControl(window,kDownloadUpdate), available);
}

void BeginHubUpdateCheck(HWND window)
{
    if (g_updateWorker.valid()) return;
    g_updateResult = {};
    if (g_updateSource.empty()) {
        SetHubUpdateStatus(window, L"更新元を設定すると、起動時に新版を確認します。", false);
        return;
    }
    std::string error;
    Engine::Services::EnginePackageManifest manifest;
    if (!ResolveSelectedEditor(g_updateEditor,error) ||
        _wcsicmp(g_updateEditor.filename().c_str(), L"KuboEditor.exe") != 0 ||
        Engine::Services::EnginePackageManifest::Read(
            g_updateEditor.parent_path() / L"engine-package.json", manifest, error) !=
            Engine::Services::EnginePackageManifest::ReadResult::Success || !manifest.Versioned()) {
        SetHubUpdateStatus(window, L"選択中のエディタの版情報を確認できません。版情報付きの配布物を選択してください。", false);
        return;
    }
    g_updateCurrentVersion = manifest.engineVersion;
    g_updateCancelled.store(false, std::memory_order_relaxed);
    try {
        g_updateWorker = std::async(std::launch::async, KuboHub::CheckUpdateFeed,
            g_updateSource, g_updateCurrentVersion, std::cref(g_updateCancelled));
        EnableWindow(GetHubControl(window,kCheckUpdates), FALSE);
        EnableWindow(GetHubControl(window,kUpdateSource), FALSE);
        SetHubUpdateStatus(window, L"更新を確認しています…", false);
    } catch (...) {
        SetHubUpdateStatus(window, L"更新確認を開始できませんでした。", false);
    }
}

void InitializeHubUpdate(HWND window)
{
    const auto saved = ReadHubUpdatePreference(L"hub_update_source.txt");
    g_updateSource.assign(saved.begin(), saved.end());
    SetWindowTextW(GetHubControl(window,kUpdateSource),g_updateSource.c_str());
    BeginHubUpdateCheck(window);
}

bool UpdateSelectionMatches()
{
    std::filesystem::path editor;
    std::string error;
    Engine::Services::EnginePackageManifest manifest;
    return ResolveSelectedEditor(editor,error) && KuboHub::EngineCatalogFile::Same(editor,g_updateEditor) &&
        Engine::Services::EnginePackageManifest::Read(editor.parent_path()/L"engine-package.json",manifest,error) ==
            Engine::Services::EnginePackageManifest::ReadResult::Success && manifest.Versioned() &&
        manifest.engineVersion == g_updateCurrentVersion;
}

void RefreshHubUpdateSelection(HWND window)
{
    if (g_updateResult.state != KuboHub::UpdateFeedResult::State::Available || UpdateSelectionMatches()) return;
    g_updateResult = {};
    SetHubUpdateStatus(window,L"エディタの選択が変わりました。更新を再確認できます。",false);
}

void PollHubUpdate(HWND window)
{
    if (g_updateInstallation.valid()) {
        if (g_updateInstallation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            const auto message = g_updateCancelled.load(std::memory_order_relaxed)
                ? L"中止を待っています。"
                : L"新版を取得・検証しています。取得済み " +
                    std::to_wstring(g_updateBytes.load(std::memory_order_relaxed) / 1024) + L" KiB";
            SetWindowTextW(GetHubControl(window,kUpdateDetails),message.c_str());
            return;
        }
        EnableWindow(GetHubControl(window,kCheckUpdates),TRUE);
        EnableWindow(GetHubControl(window,kUpdateSource),TRUE);
        for (int id : {kVerifyPackage,kInstallPackage,kRecoverInstallation})
            EnableWindow(GetHubControl(window,id),TRUE);
        SetWindowTextW(GetHubControl(window,kDownloadUpdate),L"新版を取得して導入…");
        try {
            auto installed = g_updateInstallation.get();
            const bool success = installed.result == KuboHub::EnginePackageInstallation::Result::Installed ||
                installed.result == KuboHub::EnginePackageInstallation::Result::AlreadyPresent;
            if (success) {
                g_lastInstalledEditor = installed.installed.directory / L"KuboEditor.exe";
                EnableWindow(GetHubControl(window,kSelectInstalled),TRUE);
                SetHubUpdateStatus(window,L"新版を導入しました。上の「インストール済みのエディタを選択」で切り替えられます。\r\n" +
                    installed.installed.directory.wstring(),false);
            } else {
                auto message = installed.error;
                if (!installed.staged.empty()) message += L"\r\n取得したファイルの場所  " + installed.staged.wstring();
                SetHubUpdateStatus(window,message,g_updateResult.state == KuboHub::UpdateFeedResult::State::Available && UpdateSelectionMatches());
            }
        } catch (...) {
            SetHubUpdateStatus(window,L"更新結果を読み込めませんでした。",false);
        }
        return;
    }
    if (!g_updateWorker.valid() ||
        g_updateWorker.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    EnableWindow(GetHubControl(window,kCheckUpdates), TRUE);
    EnableWindow(GetHubControl(window,kUpdateSource), TRUE);
    try {
        g_updateResult = g_updateWorker.get();
        if (!UpdateSelectionMatches()) {
            g_updateResult = {};
            SetHubUpdateStatus(window,L"エディタの選択が変わりました。更新を再確認できます。",false);
            return;
        }
        const auto current = L"現在の版  " + NativeCreationMessage(g_updateCurrentVersion);
        using State = KuboHub::UpdateFeedResult::State;
        if (g_updateResult.state == State::Unavailable || g_updateResult.state == State::Invalid) {
            SetHubUpdateStatus(window,current + L"\r\n" + g_updateResult.error,false);
            return;
        }
        if (g_updateResult.state == State::Current) {
            SetHubUpdateStatus(window,current + L"\r\n利用できる新しい版はありません。",false);
            return;
        }
        const auto skip = UpdateSourceBytes(g_updateSource) + "\n" + g_updateResult.version;
        const bool skipped = ReadHubUpdatePreference(L"hub_update_skip.txt") == skip;
        const auto version = NativeCreationMessage(g_updateResult.version);
        SetHubUpdateStatus(window,current + L"\r\n利用可能な版  " + version +
            (skipped ? L"（スキップ済み）" : L"") + L"\r\n\r\n" +
            NativeCreationMessage(g_updateResult.notes),!skipped);
    } catch (...) {
        g_updateResult = {};
        SetHubUpdateStatus(window,L"更新確認の結果を読み込めませんでした。",false);
    }
}

bool HandleHubUpdateCommand(HWND window, int id)
{
    if (id == kUpdateBadge) {
        SwitchHubPage(window,HubPage::Engines);
        ScrollHub(window,SB_BOTTOM);
        SetFocus(GetHubControl(window,kUpdateSource));
        return true;
    }
    if (id == kCheckUpdates) {
        if (g_updateWorker.valid() || g_updateInstallation.valid()) return true;
        auto source = HubText(window,kUpdateSource);
        KuboHub::UpdateUrl parsed;
        if (!source.empty() && !KuboHub::ParseUpdateUrl(source,parsed)) {
            SetHubUpdateStatus(window,L"更新元には認証情報を含まないHTTPS URLを指定してください。",false);
            return true;
        }
        std::string error;
        if (!Engine::Base::AtomicFile::Write(HubUpdateFile(L"hub_update_source.txt"),
            UpdateSourceBytes(source),2048,error)) {
            SetHubUpdateStatus(window,L"更新元の設定を保存できませんでした。",false);
            return true;
        }
        g_updateSource.swap(source);
        BeginHubUpdateCheck(window);
        return true;
    }
    if (id == kDownloadUpdate) {
        if (g_updateInstallation.valid()) {
            g_updateCancelled.store(true,std::memory_order_relaxed);
            EnableWindow(GetHubControl(window,kDownloadUpdate),FALSE);
            return true;
        }
        if (g_updateResult.state != KuboHub::UpdateFeedResult::State::Available ||
            !IsWindowEnabled(GetHubControl(window,kDownloadUpdate)) || !UpdateSelectionMatches()) return true;
        if (g_installationJob.Active() || g_verificationJob.Active()) {
            SetHubUpdateStatus(window,L"ローカルパッケージの処理が終わってから更新できます。",true);
            return true;
        }
        const auto question = L"設定したHTTPS配布元から " + NativeCreationMessage(g_updateResult.version) +
            L" を取得し、別のエンジンとして導入しますか？\n" + g_updateSource;
        if (MessageBoxW(window,question.c_str(),L"エンジンの更新",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2) != IDYES)
            return true;
        try {
            g_updateCancelled.store(false,std::memory_order_relaxed);
            g_updateBytes.store(0,std::memory_order_relaxed);
            g_updateInstallation = std::async(std::launch::async,KuboHub::DownloadEngineUpdate,
                g_updateResult,ManagedEngineStore(),std::cref(g_updateCancelled),std::ref(g_updateBytes),
                &KuboHub::FetchUpdateBytes<KuboHub::UpdateByteSink>);
            SetHubUpdateStatus(window,L"新版を取得しています…",false);
            for (int control : {kCheckUpdates,kUpdateSource,kVerifyPackage,kInstallPackage,kRecoverInstallation})
                EnableWindow(GetHubControl(window,control),FALSE);
            SetWindowTextW(GetHubControl(window,kDownloadUpdate),L"取得・導入を中止");
            EnableWindow(GetHubControl(window,kDownloadUpdate),TRUE);
        } catch (...) {
            SetHubUpdateStatus(window,L"更新の取得を開始できませんでした。",true);
        }
        return true;
    }
    if (id == kLaterUpdate || id == kSkipUpdate) {
        if (g_updateResult.state != KuboHub::UpdateFeedResult::State::Available || !UpdateSelectionMatches()) return true;
        if (id == kSkipUpdate) {
            std::string error;
            const auto skip = UpdateSourceBytes(g_updateSource) + "\n" + g_updateResult.version;
            if (!Engine::Base::AtomicFile::Write(HubUpdateFile(L"hub_update_skip.txt"),skip,2303,error)) {
                SetHubUpdateStatus(window,L"スキップの設定を保存できませんでした。",true);
                return true;
            }
        }
        SetHubUpdateStatus(window,id == kSkipUpdate ? L"この版の通知をスキップしました。"
            : L"更新は後で確認できます。",false);
        return true;
    }
    return false;
}

void RetireHubUpdate()
{
    g_updateCancelled.store(true,std::memory_order_relaxed);
    if (g_updateWorker.valid()) {
        try { g_updateWorker.get(); } catch (...) {}
    }
    if (g_updateInstallation.valid()) {
        try { g_updateInstallation.get(); } catch (...) {}
    }
}
