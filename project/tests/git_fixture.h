#pragma once
#include "engine/services/ExportJob.h"
#include <Windows.h>
#include <filesystem>
#include <vector>
#include <stdexcept>

namespace KuboGitFixture {
inline void Run(const std::filesystem::path& root,const std::vector<std::wstring>& args){
    const auto absolute=std::filesystem::absolute(root).lexically_normal();const auto temp=std::filesystem::temp_directory_path();
    const auto relative=absolute.lexically_relative(temp);
    if(relative.empty() || relative.begin()->wstring()==L".." || !relative.begin()->wstring().starts_with(L"kubo-git-"))throw std::runtime_error("Git fixture must be in an owned temporary kubo-git directory");
    for(const auto* variable:{L"GIT_DIR",L"GIT_WORK_TREE",L"GIT_INDEX_FILE",L"GIT_COMMON_DIR",L"GIT_OBJECT_DIRECTORY"})if(GetEnvironmentVariableW(variable,nullptr,0))throw std::runtime_error("Fixture refuses inherited Git location override");
    wchar_t executable[32768]{};if(!SearchPathW(nullptr,L"git.exe",nullptr,32768,executable,nullptr))throw std::runtime_error("Fixture Git missing");
    auto command=Engine::Services::ExportJob::QuoteArgument(executable)+L" --literal-pathspecs -c core.hooksPath=NUL -c commit.gpgsign=false -c core.attributesFile=NUL -C "+Engine::Services::ExportJob::QuoteArgument(absolute.wstring());
    for(const auto& arg:args)command+=L" "+Engine::Services::ExportJob::QuoteArgument(arg);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))throw std::runtime_error("Fixture Git process failed");CloseHandle(process.hThread);
    const auto waited=WaitForSingleObject(process.hProcess,30000);if(waited!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,99);WaitForSingleObject(process.hProcess,5000);}DWORD code=1;const bool got=GetExitCodeProcess(process.hProcess,&code)!=FALSE;CloseHandle(process.hProcess);
    if(waited!=WAIT_OBJECT_0 || !got || code)throw std::runtime_error("Fixture Git command failed");
}
}
