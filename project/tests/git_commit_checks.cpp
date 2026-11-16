#include "engine/services/GitWorkspace.h"
#include "git_fixture.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Fixture write failed");}
std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void Git(const std::filesystem::path& root,const std::vector<std::wstring>& args){KuboGitFixture::Run(root,args);}
int main(){
    namespace fs=std::filesystem;using namespace Engine::Services;
    const auto fixture=fs::temp_directory_path()/(L"kubo-git-commit-日本語 & %-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        for(const auto* variable:{L"GIT_DIR",L"GIT_WORK_TREE",L"GIT_INDEX_FILE",L"GIT_COMMON_DIR",L"GIT_OBJECT_DIRECTORY"})Require(!GetEnvironmentVariableW(variable,nullptr,0),"Fixture refuses inherited Git location override");
        const auto repo=fixture/"Repo",project=repo/"Project";fs::create_directories(project);
        Git(repo,{L"init",L"-b",L"main"});
        for(const auto& config:std::vector<std::vector<std::wstring>>{{L"config",L"core.attributesFile",L"NUL"},{L"config",L"core.hooksPath",L"NUL"},{L"config",L"commit.gpgsign",L"false"},{L"config",L"user.name",L"Kubo isolated test"},{L"config",L"user.email",L"test@example.invalid"},{L"config",L"diff.relative",L"true"}})Git(repo,config);
        Write(project/"keep.txt","staged first");Git(repo,{L"add",L"--",L"Project/keep.txt"});auto status=GitWorkspace::ReadStatus(project);std::string error;
        Require(status.repository && status.error.empty() && status.head.empty(),"Unborn status failed");
        const auto refuse=[&](const GitStatus& expected,const std::string& message){const auto before=GitWorkspace::ReadStatus(project);const auto index=Read(repo/".git/index");Require(!GitWorkspace::Commit(project,expected,message,error),"Commit unexpectedly admitted");Require(GitWorkspace::ReadStatus(project).head==before.head && Read(repo/".git/index")==index,"Refusal changed HEAD/index");};
        for(const auto& message:std::vector<std::string>{""," \t\n",std::string(4097,'a'),std::string("a\0b",3),std::string("\xff",1)})refuse(status,message);
        Write(project/"keep.txt","newer unstaged bytes");
        Require(GitWorkspace::Commit(project,status,"初回 & %PATH% \"quoted\"\n\nbody",error),error.c_str());
        status=GitWorkspace::ReadStatus(project);Require(!status.head.empty() && status.indexRevision.empty() && status.changes.size()==1 && status.changes[0].index==' ' && status.changes[0].worktree=='M' && Read(project/"keep.txt")=="newer unstaged bytes","Commit implicitly staged new disk bytes");
        Git(repo,{L"show",L"HEAD:Project/keep.txt"});refuse(status,"empty staged commit");
        Git(repo,{L"add",L"--",L"Project/keep.txt"});status=GitWorkspace::ReadStatus(project);
        Write(repo/"outside.txt","outside");Git(repo,{L"add",L"--",L"outside.txt"});refuse(status,"stale index");status=GitWorkspace::ReadStatus(project);refuse(status,"outside scope");Require(error.find("outside")!=std::string::npos,"Outside scope diagnostic missing");
        Git(repo,{L"restore",L"--staged",L"--",L"outside.txt"});status=GitWorkspace::ReadStatus(project);
        Git(repo,{L"commit",L"--allow-empty",L"--only",L"-m",L"owned empty head change"});refuse(status,"stale head");status=GitWorkspace::ReadStatus(project);
        Write(repo/".git/index.lock","owned lock");refuse(status,"lock refusal");fs::remove(repo/".git/index.lock");
        Write(repo/".git/MERGE_HEAD",status.head+"\n");refuse(status,"merge refusal");fs::remove(repo/".git/MERGE_HEAD");
        Require(SetEnvironmentVariableW(L"GIT_INDEX_FILE",(fixture/"external-index").c_str())!=FALSE,"Override setup failed");const bool denied=!GitWorkspace::Commit(project,status,"override",error);SetEnvironmentVariableW(L"GIT_INDEX_FILE",nullptr);Require(denied && !fs::exists(fixture/"external-index"),"Git override accepted");
        Git(repo,{L"checkout",L"--detach",L"HEAD"});status=GitWorkspace::ReadStatus(project);refuse(status,"detached");Git(repo,{L"checkout",L"main"});status=GitWorkspace::ReadStatus(project);
        const auto hooks=repo/"Hooks";fs::create_directory(hooks);Write(hooks/"pre-commit","#!/bin/sh\nexit 1\n");Git(repo,{L"config",L"core.hooksPath",hooks.wstring()});refuse(status,"hook refusal");Git(repo,{L"config",L"core.hooksPath",L"NUL"});
        Git(repo,{L"config",L"user.name",L""});refuse(status,"missing identity");Git(repo,{L"config",L"user.name",L"Kubo isolated test"});
        Require(GitWorkspace::Commit(project,status,"second project commit",error),error.c_str());Require(GitWorkspace::ReadStatus(project).indexRevision.empty() && Read(project/"keep.txt")=="newer unstaged bytes" && Read(repo/"outside.txt")=="outside","Commit completion changed disk");
        status=GitWorkspace::ReadStatus(project);Git(repo,{L"update-index",L"--add",L"--cacheinfo",L"160000,"+std::wstring(status.head.begin(),status.head.end())+L",Project/link"});status=GitWorkspace::ReadStatus(project);refuse(status,"gitlink refusal");Require(error.find("submodule")!=std::string::npos,"Gitlink diagnostic missing");Git(repo,{L"rm",L"--cached",L"--",L"Project/link"});
        Git(repo,{L"add",L"--",L"outside.txt"});Git(repo,{L"commit",L"-m",L"owned outside baseline"});Git(repo,{L"mv",L"--",L"outside.txt",L"Project/moved.txt"});status=GitWorkspace::ReadStatus(project);refuse(status,"cross-project rename refusal");Require(error.find("outside")!=std::string::npos,"Cross-project rename escaped scope");
        std::error_code cleanup;fs::remove_all(fixture,cleanup);Require(!cleanup,"Owned fixture cleanup failed");
        std::cout<<"PASS: staged-only initial/ordinary commits, Unicode/message quoting, scope/stale/lock/environment/detached/workflow/hook refusal and disk preservation\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL: "<<failure.what()<<"; retained fixture "<<fixture<<'\n';return 1;}
}
