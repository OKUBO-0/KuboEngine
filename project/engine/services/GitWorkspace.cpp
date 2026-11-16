#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "GitWorkspace.h"
#include "ExportJob.h"
#include "engine/base/AssetPath.h"
#include <Windows.h>
#include <array>
#include <stdexcept>
#include <algorithm>
#include <sstream>

namespace Engine::Services {
namespace {
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { Close(); }
    void Close() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
};
struct Child : Handle {
    ~Child() {
        if (value && WaitForSingleObject(value, 0) == WAIT_TIMEOUT) {
            TerminateProcess(value, 1); // Only this bounded, owned Git child.
            WaitForSingleObject(value, 1000);
        }
    }
};

std::filesystem::path FindGit()
{
    const auto size = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (!size) return {};
    std::wstring paths(size, L'\0');
    const auto read = GetEnvironmentVariableW(L"PATH", paths.data(), size);
    if (!read || read >= size) return {};
    paths.resize(read);
    size_t begin = 0;
    while (begin <= paths.size()) {
        const auto end = paths.find(L';', begin);
        auto part = paths.substr(begin, end == std::wstring::npos ? end : end - begin);
        if (part.size() >= 2 && part.front() == L'"' && part.back() == L'"') part = part.substr(1, part.size() - 2);
        const std::filesystem::path directory(part);
        if (directory.is_absolute()) {
            const auto candidate = directory / L"git.exe";
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) return candidate;
        }
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    return {};
}

bool Run(const std::filesystem::path& git, const std::filesystem::path& root,
    const std::vector<std::wstring>& arguments, std::string& output, std::string& error,DWORD* resultCode=nullptr,
    const std::atomic_bool* cancelled=nullptr, ULONGLONG timeout=10000)
{
    output.clear();
    if(resultCode)*resultCode=DWORD(-1);
    if (git.empty()) { error = "Git executable was not found on absolute PATH entries"; return false; }
    std::wstring command = ExportJob::QuoteArgument(git.wstring()) + L" --no-optional-locks --literal-pathspecs -c core.fsmonitor=false";
    if (!root.empty()) command += L" -C " + ExportJob::QuoteArgument(std::filesystem::absolute(root).wstring());
    for (const auto& argument : arguments) command += L" " + ExportJob::QuoteArgument(argument);
    if(command.size()>30000){error="Git command exceeds bounded Windows argument size";return false;}
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle readPipe, writePipe, input;
    if (!CreatePipe(&readPipe.value, &writePipe.value, &security, 0) ||
        !SetHandleInformation(readPipe.value, HANDLE_FLAG_INHERIT, 0)) { error = "Cannot create Git output pipe"; return false; }
    input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input.value == INVALID_HANDLE_VALUE) { error = "Cannot open Git input"; return false; }
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr,1,0,&attributeBytes);
    if (!attributeBytes) { error = "Cannot size Git process attributes"; return false; }
    std::vector<unsigned char> attributeStorage(attributeBytes);
    const auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    if (!InitializeProcThreadAttributeList(attributes,1,0,&attributeBytes)) {
        error = "Cannot initialize Git process attributes";
        return false;
    }
    struct AttributeOwner {
        LPPROC_THREAD_ATTRIBUTE_LIST value;
        ~AttributeOwner() { DeleteProcThreadAttributeList(value); }
    } ownedAttributes{attributes};
    HANDLE inherited[] = {input.value,writePipe.value};
    if (!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited,sizeof(inherited),nullptr,nullptr)) {
        error = "Cannot isolate inherited Git pipe handles";
        return false;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = writePipe.value;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION information{};
    Handle job;
    if (cancelled) {
        job.value = CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job.value || !SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {
            error = "Cannot isolate Git clone process";
            return false;
        }
    }
    const DWORD flags = CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | (job.value?CREATE_SUSPENDED:0);
    if (!CreateProcessW(git.c_str(), command.data(), nullptr, nullptr, TRUE, flags,
        nullptr, git.parent_path().c_str(), &startup.StartupInfo, &information)) {
        error = "Cannot start Git (Windows error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    Child process;
    process.value = information.hProcess;
    Handle thread;
    thread.value = information.hThread;
    if (job.value && (!AssignProcessToJobObject(job.value,process.value) ||
        ResumeThread(thread.value)==DWORD(-1))) {
        error = "Cannot start isolated Git clone process";
        return false;
    }
    thread.Close();
    writePipe.Close();
    input.Close();
    const auto started = GetTickCount64();
    std::array<char, 8192> buffer;
    for (;;) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) {
            error = "Git clone cancelled";
            return false;
        }
        if (GetTickCount64() - started > timeout) { error = "Git command timed out; inspect the output before retrying"; return false; }
        DWORD available = 0;
        if (!PeekNamedPipe(readPipe.value, nullptr, 0, nullptr, &available, nullptr)) {
            if (GetLastError() != ERROR_BROKEN_PIPE) { error = "Cannot read Git output"; return false; }
            if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) break;
        }
        if (available) {
            DWORD count = 0;
            const auto request = available < buffer.size() ? available : static_cast<DWORD>(buffer.size());
            if (!ReadFile(readPipe.value, buffer.data(), request, &count, nullptr)) { error = "Cannot read Git output"; return false; }
            if (output.size() + count > 2 * 1024 * 1024) { error = "Git output exceeds 2 MiB"; return false; }
            output.append(buffer.data(), count);
        } else if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) break;
        else Sleep(5);
    }
    DWORD exit = 1;
    const bool gotExit=GetExitCodeProcess(process.value,&exit)!=FALSE;if(resultCode && gotExit)*resultCode=exit;
    if (!gotExit || exit != 0) {
        error = output.empty() ? "Git command failed" : output;
        return false;
    }
    error.clear();
    return true;
}

void TrimLines(std::string& text) { while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back(); }

std::string DisplayText(const std::string& bytes)
{
    const bool utf8=bytes.empty() || MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),nullptr,0)>0;
    std::string safe;safe.reserve(bytes.size());constexpr char hex[]="0123456789ABCDEF";
    for(const unsigned char byte:bytes) {
        if((byte<32 && byte!='\n' && byte!='\r' && byte!='\t') || byte==127 || (!utf8 && byte>=128)) {
            if(safe.size()+4>2*1024*1024)throw std::runtime_error("Escaped Git display exceeds 2 MiB");
            safe+="\\x";safe+=hex[byte>>4];safe+=hex[byte&15];
        }else {if(safe.size()+1>2*1024*1024)throw std::runtime_error("Escaped Git display exceeds 2 MiB");safe+=char(byte);}
    }
    return safe;
}
}

bool GitWorkspace::CloneHttps(const std::wstring& url,const std::filesystem::path& directory,
    const std::atomic_bool& cancelled,std::string& error)
{
    if (!url.starts_with(L"https://") || url.size() > 2048 ||
        url.find_first_of(L"@?#\\\r\n\t ") != std::wstring::npos || !directory.is_absolute()) {
        error = "Clone requires a credential-free HTTPS URL and an absolute output directory";
        return false;
    }
    std::string output;
    return Run(FindGit(),{},{
        L"-c",L"protocol.allow=never",L"-c",L"protocol.https.allow=always",
        L"-c",L"credential.interactive=never",L"-c",L"core.hooksPath=NUL",
        L"clone",L"--no-recurse-submodules",L"--template=",L"--",url,directory.wstring()
    },output,error,nullptr,&cancelled,120000);
}

bool GitWorkspace::ParseHistory(const std::string& bytes,std::vector<GitCommitEntry>& entries,std::string& error)
{
    try {
        if(bytes.size()>2*1024*1024)throw std::runtime_error("Git history exceeds 2 MiB");
        std::vector<GitCommitEntry> parsed;size_t offset=0;
        while(offset<bytes.size()) {
            if(parsed.size()>=51)throw std::runtime_error("Git history exceeds page record limit");
            std::array<std::string,4> fields;
            for(auto& field:fields){const auto end=bytes.find('\0',offset);if(end==std::string::npos)throw std::runtime_error("Malformed Git history record");field=bytes.substr(offset,end-offset);offset=end+1;}
            if((fields[0].size()!=40 && fields[0].size()!=64) || fields[0].find_first_not_of("0123456789abcdef")!=std::string::npos || fields[2].empty())throw std::runtime_error("Invalid Git history hash/date");
            parsed.push_back({std::move(fields[0]),DisplayText(fields[1]),DisplayText(fields[2]),DisplayText(fields[3])});
        }
        entries=std::move(parsed);error.clear();return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}

bool GitWorkspace::ReadHistory(const std::filesystem::path& projectRoot,const GitStatus& expected,
    size_t skip,GitHistory& history,std::string& error)
{
    try {
        for(const auto* variable:{L"GIT_DIR",L"GIT_WORK_TREE",L"GIT_INDEX_FILE",L"GIT_COMMON_DIR",L"GIT_OBJECT_DIRECTORY"})
            if(GetEnvironmentVariableW(variable,nullptr,0))throw std::runtime_error("Git location override environment is active; history refused");
        if(!expected.repository || !expected.error.empty() || skip>10000)throw std::runtime_error("Refresh valid Git status; history page offset must be at most 10000");
        const auto repository=std::filesystem::weakly_canonical(Base::PathFromUtf8(expected.repositoryRoot));
        const auto project=std::filesystem::weakly_canonical(std::filesystem::absolute(projectRoot));
        const auto within=[](const auto& path,const auto& root){auto item=path.begin();for(const auto& component:root){if(item==path.end() || _wcsicmp(item->c_str(),component.c_str()))return false;++item;}return true;};
        if(!within(project,repository))throw std::runtime_error("Project is outside the displayed Git repository");
        const auto matches=[&](const GitStatus& current){return current.repository && current.error.empty() && current.repositoryRoot==expected.repositoryRoot && current.branch==expected.branch && current.head==expected.head;};
        if(!matches(ReadStatus(project)))throw std::runtime_error("Git HEAD/branch changed; refresh project history");
        GitHistory candidate;candidate.head=expected.head;candidate.skip=skip;
        if(!expected.head.empty()) {
            if((expected.head.size()!=40 && expected.head.size()!=64) || expected.head.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid history HEAD hash");
            std::string raw;
            if(!Run(FindGit(),project,{L"-c",L"log.follow=false",L"log",L"--no-color",L"--no-decorate",L"--no-show-signature",L"--no-patch",L"--no-abbrev-commit",L"--encoding=UTF-8",L"--topo-order",L"--max-count=51",L"--skip="+std::to_wstring(skip),L"-z",L"--format=tformat:%H%x00%an%x00%aI%x00%s",Base::PathFromUtf8(expected.head).wstring(),L"--",L"."},raw,error) || !ParseHistory(raw,candidate.entries,error))return false;
            candidate.more=candidate.entries.size()>50;if(candidate.more)candidate.entries.resize(50);
        }
        if(!matches(ReadStatus(project)))throw std::runtime_error("Git HEAD/branch changed while reading history; refresh");
        history=std::move(candidate);error.clear();return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}

bool GitWorkspace::ParsePorcelain(const std::string& bytes, std::vector<GitChange>& changes, std::string& error)
{
    std::vector<GitChange> parsed;
    size_t offset = 0;
    while (offset < bytes.size()) {
        const auto end = bytes.find('\0', offset);
        if (end == std::string::npos || end - offset < 4 || bytes[offset + 2] != ' ') {
            error = "Malformed Git porcelain record"; return false;
        }
        constexpr const char* codes = " MTADRCU?!";
        if (std::string(codes).find(bytes[offset]) == std::string::npos ||
            std::string(codes).find(bytes[offset + 1]) == std::string::npos) {
            error = "Invalid Git status code"; return false;
        }
        GitChange change{bytes[offset], bytes[offset + 1], bytes.substr(offset + 3, end - offset - 3), {}};
        offset = end + 1;
        if (change.index == 'R' || change.index == 'C' || change.worktree == 'R' || change.worktree == 'C') {
            const auto originalEnd = bytes.find('\0', offset);
            if (originalEnd == std::string::npos || originalEnd == offset) { error = "Malformed Git rename record"; return false; }
            change.originalPath = bytes.substr(offset, originalEnd - offset);
            offset = originalEnd + 1;
        }
        parsed.push_back(std::move(change));
    }
    changes = std::move(parsed);
    error.clear();
    return true;
}

GitStatus GitWorkspace::ReadStatus(const std::filesystem::path& projectRoot)
{
    GitStatus status;
    try {
        const auto git = FindGit();
        std::string repository;
        if (!Run(git, projectRoot, {L"rev-parse", L"--is-inside-work-tree"}, repository, status.error)) return status;
        TrimLines(repository);
        if (repository != "true") { status.error = "Not a Git working tree"; return status; }
        status.repository = true;
        if (!Run(git, projectRoot, {L"rev-parse", L"--show-toplevel"}, status.repositoryRoot, status.error) ||
            !Run(git, projectRoot, {L"branch", L"--show-current"}, status.branch, status.error)) return status;
        TrimLines(status.repositoryRoot);
        TrimLines(status.branch);
        status.detached = status.branch.empty();
        DWORD headCode=DWORD(-1);
        if(!Run(git,projectRoot,{L"rev-parse",L"--verify",L"--quiet",L"HEAD"},status.head,status.error,&headCode)) {
            if(headCode!=1 || status.detached || !status.head.empty())return status;
            status.error.clear(); // Symbolic unborn branch, no HEAD yet.
        }
        TrimLines(status.head);
        if(!Run(git,Base::PathFromUtf8(status.repositoryRoot),{L"diff",L"--cached",L"--raw",L"-z",L"--no-abbrev",L"--no-renames",L"--no-relative",L"--ignore-submodules=none",L"--no-ext-diff",L"--no-textconv"},status.indexRevision,status.error))return status;
        std::string raw;
        if (!Run(git, projectRoot, {L"status", L"--porcelain=v1", L"-z", L"--untracked-files=normal", L"--", L"."}, raw, status.error) ||
            !ParsePorcelain(raw, status.changes, status.error)) return status;
        for (const auto& change : status.changes) {
            status.porcelain += std::string{change.index, change.worktree} + " " + change.path;
            if (!change.originalPath.empty()) status.porcelain += " <- " + change.originalPath;
            status.porcelain += '\n';
        }
    } catch (const std::exception& e) { status.error = e.what(); }
    return status;
}

bool GitWorkspace::IsAvailable()
{
    try {
        std::string output, error;
        return Run(FindGit(), {}, {L"--version"}, output, error) && output.find("git version ") == 0;
    } catch (...) { return false; }
}

bool GitWorkspace::UpdateIndex(const std::filesystem::path& projectRoot,const GitStatus& expected,
    const GitChange& selected,bool stage,std::string& error)
{
    try {
        for(const auto* variable:{L"GIT_DIR",L"GIT_WORK_TREE",L"GIT_INDEX_FILE",L"GIT_COMMON_DIR",L"GIT_OBJECT_DIRECTORY"})
            if(GetEnvironmentVariableW(variable,nullptr,0))throw std::runtime_error("Git location override environment is active; index operation refused");
        if(!expected.repository || !expected.error.empty())throw std::runtime_error("Refresh a valid Git status before editing the index");
        const auto same=[](const GitChange& a,const GitChange& b){return a.index==b.index && a.worktree==b.worktree && a.path==b.path && a.originalPath==b.originalPath;};
        if(std::none_of(expected.changes.begin(),expected.changes.end(),[&](const auto& change){return same(change,selected);}))throw std::runtime_error("Selected Git entry is not in the displayed status");
        if(selected.index=='U' || selected.worktree=='U' || (selected.index=='A' && selected.worktree=='A') || (selected.index=='D' && selected.worktree=='D'))throw std::runtime_error("Resolve unmerged Git entries externally before editing the index here");
        if(stage ? selected.worktree==' ' : (selected.index==' ' || selected.index=='?' || selected.index=='!'))throw std::runtime_error("Selected Git entry has no requested index operation");
        const auto repository=std::filesystem::weakly_canonical(Base::PathFromUtf8(expected.repositoryRoot));
        const auto project=std::filesystem::weakly_canonical(std::filesystem::absolute(projectRoot));
        const auto within=[](const auto& path,const auto& root){auto item=path.begin();for(const auto& component:root){if(item==path.end() || _wcsicmp(item->c_str(),component.c_str()))return false;++item;}return true;};
        if(!within(project,repository))throw std::runtime_error("Project is outside the displayed Git repository");
        std::vector<std::wstring> paths;
        const auto admit=[&](const std::string& bytes){
            if(bytes.empty() || bytes.size()>8192 || bytes.find('\0')!=std::string::npos || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),nullptr,0))throw std::runtime_error("Invalid UTF-8 Git path");
            const auto path=Base::PathFromUtf8(bytes);
            if(path.has_root_path())throw std::runtime_error("Git path must be repository-relative");
            for(const auto& component:path)if(component==L".." || component==L"." || !_wcsicmp(component.c_str(),L".git"))throw std::runtime_error("Unsafe Git path component");
            const auto target=std::filesystem::weakly_canonical(repository/path);
            if(!within(target,project) || target==project)throw std::runtime_error("Selected Git path escapes the project scope");
            paths.push_back(path.wstring());
        };
        admit(selected.path);
        if((selected.index=='R' || selected.worktree=='R') && !selected.originalPath.empty())admit(selected.originalPath);
        const auto current=ReadStatus(project);
        if(!current.repository || !current.error.empty() || current.repositoryRoot!=expected.repositoryRoot || current.branch!=expected.branch || current.head!=expected.head || current.indexRevision!=expected.indexRevision ||
            std::none_of(current.changes.begin(),current.changes.end(),[&](const auto& change){return same(change,selected);}))throw std::runtime_error("Git status/HEAD/staged content changed; refresh and select again");
        std::vector<std::wstring> arguments;
        if(stage)arguments={L"add",L"-A",L"--"};
        else if(!current.head.empty())arguments={L"restore",L"--staged",L"--"};
        else arguments={L"rm",L"--cached",L"-r",L"--force",L"--ignore-unmatch",L"--"};
        arguments.insert(arguments.end(),paths.begin(),paths.end());std::string output;
        return Run(FindGit(),repository,arguments,output,error);
    }catch(const std::exception& failure){error=failure.what();return false;}
}
bool GitWorkspace::ReadDiff(const std::filesystem::path& projectRoot,const GitStatus& expected,
    const GitChange& selected,bool staged,std::string& display,std::string& error)
{
    try {
        for(const auto* variable:{L"GIT_DIR",L"GIT_WORK_TREE",L"GIT_INDEX_FILE",L"GIT_COMMON_DIR",L"GIT_OBJECT_DIRECTORY"})
            if(GetEnvironmentVariableW(variable,nullptr,0))throw std::runtime_error("Git location override environment is active; diff refused");
        if(!expected.repository || !expected.error.empty())throw std::runtime_error("Refresh a valid Git status before reading a diff");
        const auto same=[](const GitChange& a,const GitChange& b){return a.index==b.index && a.worktree==b.worktree && a.path==b.path && a.originalPath==b.originalPath;};
        if(std::none_of(expected.changes.begin(),expected.changes.end(),[&](const auto& entry){return same(entry,selected);}))throw std::runtime_error("Selected Git entry is not in the displayed status");
        if(selected.index=='U' || selected.worktree=='U' || (selected.index=='A' && selected.worktree=='A') || (selected.index=='D' && selected.worktree=='D'))throw std::runtime_error("Inspect unmerged Git entries externally");
        if(staged ? (selected.index==' ' || selected.index=='?' || selected.index=='!') : selected.worktree==' ')throw std::runtime_error("Selected entry has no requested diff");
        const auto repository=std::filesystem::weakly_canonical(Base::PathFromUtf8(expected.repositoryRoot));
        const auto project=std::filesystem::weakly_canonical(std::filesystem::absolute(projectRoot));
        const auto within=[](const auto& path,const auto& root){auto item=path.begin();for(const auto& component:root){if(item==path.end() || _wcsicmp(item->c_str(),component.c_str()))return false;++item;}return true;};
        if(!within(project,repository))throw std::runtime_error("Project is outside the displayed Git repository");
        std::vector<std::wstring> paths;
        const auto admit=[&](const std::string& bytes){
            if(bytes.empty() || bytes.size()>8192 || bytes.find('\0')!=std::string::npos || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),nullptr,0))throw std::runtime_error("Invalid UTF-8 Git path");
            const auto path=Base::PathFromUtf8(bytes);if(path.has_root_path())throw std::runtime_error("Git path must be repository-relative");
            for(const auto& component:path)if(component==L".." || component==L"." || !_wcsicmp(component.c_str(),L".git"))throw std::runtime_error("Unsafe Git path component");
            const auto target=std::filesystem::weakly_canonical(repository/path);
            if(!within(target,project) || target==project)throw std::runtime_error("Selected Git path escapes the project scope");
            paths.push_back(path.wstring());
        };
        admit(selected.path);if(!selected.originalPath.empty())admit(selected.originalPath);
        const auto matches=[&](const GitStatus& current){return current.repository && current.error.empty() && current.repositoryRoot==expected.repositoryRoot && current.branch==expected.branch && current.head==expected.head && current.indexRevision==expected.indexRevision &&
            std::any_of(current.changes.begin(),current.changes.end(),[&](const auto& entry){return same(entry,selected);});};
        if(!matches(ReadStatus(project)))throw std::runtime_error("Git status/HEAD/staged content changed; refresh before reading a diff");
        const bool untracked=selected.index=='?' && selected.worktree=='?';
        std::vector<std::wstring> arguments{L"diff",L"--no-color",L"--no-ext-diff",L"--no-textconv",L"--no-relative",L"--ignore-submodules=none",L"--unified=3",L"--no-renames"};
        if(untracked) {
            if(staged || paths.size()!=1 || !std::filesystem::is_regular_file(repository/paths.front()))throw std::runtime_error("Untracked directory preview requires staging first; individual new files can be previewed");
            arguments.insert(arguments.end(),{L"--no-index",L"--",L"NUL",(repository/paths.front()).wstring()});
        } else {
            if(staged)arguments.push_back(L"--cached");arguments.push_back(L"--");arguments.insert(arguments.end(),paths.begin(),paths.end());
        }
        std::string output;DWORD code=DWORD(-1);
        if(!Run(FindGit(),repository,arguments,output,error,&code) && !(untracked && code==1))return false;
        if(!matches(ReadStatus(project)))throw std::runtime_error("Git status/HEAD/staged content changed while reading; refresh the diff");
        // ImGui requires bounded valid text; binary summaries remain summaries.
        // Non-UTF8 text bytes and control bytes are visible escapes, not hidden NUL.
        display=DisplayText(output);error.clear();return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}

bool GitWorkspace::Commit(const std::filesystem::path& projectRoot,const GitStatus& expected,
    const std::string& message,std::string& error)
{
    try {
        for(const auto* variable:{L"GIT_DIR",L"GIT_WORK_TREE",L"GIT_INDEX_FILE",L"GIT_COMMON_DIR",L"GIT_OBJECT_DIRECTORY"})
            if(GetEnvironmentVariableW(variable,nullptr,0))throw std::runtime_error("Git location override environment is active; commit refused");
        if(message.empty() || message.size()>4096 || message.find('\0')!=std::string::npos ||
            message.find_first_not_of(" \t\r\n")==std::string::npos ||
            !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,message.data(),int(message.size()),nullptr,0))
            throw std::runtime_error("Commit message must be nonblank valid UTF-8, at most 4096 bytes");
        if(!expected.repository || !expected.error.empty() || expected.detached || expected.branch.empty())
            throw std::runtime_error("Refresh a valid branch status before committing (detached HEAD is not supported)");
        if(expected.indexRevision.empty())throw std::runtime_error("No staged changes to commit");
        const auto repository=std::filesystem::weakly_canonical(Base::PathFromUtf8(expected.repositoryRoot));
        const auto project=std::filesystem::weakly_canonical(std::filesystem::absolute(projectRoot));
        const auto within=[](const auto& path,const auto& root){auto item=path.begin();for(const auto& component:root){if(item==path.end() || _wcsicmp(item->c_str(),component.c_str()))return false;++item;}return true;};
        if(!within(project,repository))throw std::runtime_error("Project is outside the displayed Git repository");
        // --no-renames makes each raw -z record metadata NUL path NUL. Inspect
        // the GLOBAL index, not just the project-filtered porcelain display.
        size_t offset=0;
        while(offset<expected.indexRevision.size()) {
            const auto end=expected.indexRevision.find('\0',offset);
            if(end==std::string::npos)throw std::runtime_error("Malformed staged Git metadata");
            std::istringstream fields(expected.indexRevision.substr(offset,end-offset));
            std::string oldMode,newMode,oldHash,newHash,code,extra;
            fields>>oldMode>>newMode>>oldHash>>newHash>>code;
            const auto mode=[](const std::string& value){return value.size()==6 && value.find_first_not_of("01234567")==std::string::npos;};
            const auto hash=[](const std::string& value){return (value.size()==40 || value.size()==64) && value.find_first_not_of("0123456789abcdef")==std::string::npos;};
            if(oldMode.empty() || oldMode.front()!=':' || !mode(oldMode.substr(1)) || !mode(newMode) || !hash(oldHash) || !hash(newHash) ||
                code.size()!=1 || std::string("ADMT").find(code)==std::string::npos || (fields>>extra))
                throw std::runtime_error("Unsupported or unmerged staged Git entry");
            if(oldMode==":160000" || newMode=="160000")throw std::runtime_error("Commit submodule changes externally");
            offset=end+1;const auto pathEnd=expected.indexRevision.find('\0',offset);
            if(pathEnd==std::string::npos)throw std::runtime_error("Malformed staged Git path");
            const auto bytes=expected.indexRevision.substr(offset,pathEnd-offset);offset=pathEnd+1;
            if(bytes.empty() || bytes.size()>8192 || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),nullptr,0))throw std::runtime_error("Invalid UTF-8 staged Git path");
            const auto path=Base::PathFromUtf8(bytes);
            if(path.has_root_path())throw std::runtime_error("Staged Git path must be repository-relative");
            for(const auto& component:path)if(component==L".." || component==L"." || !_wcsicmp(component.c_str(),L".git"))throw std::runtime_error("Unsafe staged Git path component");
            const auto target=std::filesystem::weakly_canonical(repository/path);
            if(!within(target,project) || target==project)throw std::runtime_error("Staged changes outside the selected project; commit refused");
        }
        const auto git=FindGit();std::string output;
        // Do not finalize externally started merge/rebase/cherry-pick workflows.
        for(const auto* state:{L"MERGE_HEAD",L"CHERRY_PICK_HEAD",L"REVERT_HEAD",L"rebase-merge",L"rebase-apply",L"sequencer"}) {
            if(!Run(git,repository,{L"rev-parse",L"--git-path",state},output,error))return false;
            TrimLines(output);auto path=Base::PathFromUtf8(output);if(path.is_relative())path=repository/path;
            if(std::filesystem::exists(path))throw std::runtime_error("Finish the active Git merge/rebase/sequencer workflow externally");
        }
        const auto current=ReadStatus(project);
        if(!current.repository || !current.error.empty() || current.detached || current.repositoryRoot!=expected.repositoryRoot ||
            current.branch!=expected.branch || current.head!=expected.head || current.indexRevision!=expected.indexRevision)
            throw std::runtime_error("Git status/HEAD/staged content changed; refresh before committing");
        // No paths, -a, --only, --amend or --no-verify. Preserve staged versus
        // unstaged bytes; normal identity, signing and trusted hooks apply.
        return Run(git,repository,{L"commit",L"-m",Base::PathFromUtf8(message).wstring()},output,error);
    }catch(const std::exception& failure){error=failure.what();return false;}
}
}
