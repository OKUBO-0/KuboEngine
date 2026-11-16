#include "engine/services/GitWorkspace.h"
#include "engine/services/ExportJob.h"
#include "git_fixture.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Fixture write failed");}
std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void Git(const std::filesystem::path& root,const std::vector<std::wstring>& args){
    KuboGitFixture::Run(root,args);
}
Engine::Services::GitChange Find(const Engine::Services::GitStatus& status,const std::string& path){Require(status.repository && status.error.empty(),status.error.c_str());for(const auto& change:status.changes)if(change.path==path)return change;throw std::runtime_error("Expected Git entry missing: "+path);}
int main(){
    namespace fs=std::filesystem;using namespace Engine::Services;
    const auto fixture=fs::temp_directory_path()/(L"kubo-git-index-日本語 & %-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        for(const auto* variable:{L"GIT_DIR",L"GIT_WORK_TREE",L"GIT_INDEX_FILE",L"GIT_COMMON_DIR",L"GIT_OBJECT_DIRECTORY"})Require(!GetEnvironmentVariableW(variable,nullptr,0),"Fixture refuses inherited Git location override");
        const auto repo=fixture/"Repo",project=repo/"Project";fs::create_directories(project);Git(repo,{L"init",L"-b",L"main"});Git(repo,{L"config",L"core.attributesFile",L"NUL"});Git(repo,{L"config",L"user.name",L"Kubo isolated test"});Git(repo,{L"config",L"user.email",L"test@example.invalid"});
        Write(project/"keep.txt","baseline");Write(project/"rename.txt","rename baseline");Write(repo/"outside.txt","outside baseline");Git(repo,{L"add",L"--",L"."});Git(repo,{L"commit",L"-m",L"isolated fixture"});
        Write(project/"keep.txt","changed");Write(repo/"outside.txt","outside changed");auto status=GitWorkspace::ReadStatus(project);std::string error;
        Require(!status.head.empty() && status.changes.size()==1,"Nested project status escaped scope");Require(GitWorkspace::UpdateIndex(project,status,Find(status,"Project/keep.txt"),true,error),error.c_str());
        status=GitWorkspace::ReadStatus(project);Require(Find(status,"Project/keep.txt").index=='M' && Find(status,"Project/keep.txt").worktree==' ',"Stage did not update selected index");
        Require(GitWorkspace::UpdateIndex(project,status,Find(status,"Project/keep.txt"),false,error),error.c_str());Require(Read(project/"keep.txt")=="changed" && Read(repo/"outside.txt")=="outside changed","Unstage modified working files");
        status=GitWorkspace::ReadStatus(project);Require(Find(status,"Project/keep.txt").index==' ' && Find(status,"Project/keep.txt").worktree=='M',"Unstage did not retain worktree change");
        const auto beforeHead=status;Git(repo,{L"commit",L"--allow-empty",L"-m",L"owned empty commit"});const auto headIndex=Read(repo/".git/index");
        Require(!GitWorkspace::UpdateIndex(project,beforeHead,Find(beforeHead,"Project/keep.txt"),true,error) && Read(repo/".git/index")==headIndex,"Stale HEAD admission changed index");
        Write(project/L"日本語 & %PATH%.txt","unicode");Write(project/"[literal].txt","literal");Write(project/"l.txt","not selected");
        status=GitWorkspace::ReadStatus(project);Require(GitWorkspace::UpdateIndex(project,status,Find(status,"Project/[literal].txt"),true,error),error.c_str());status=GitWorkspace::ReadStatus(project);Require(Find(status,"Project/l.txt").index=='?' && Find(status,"Project/[literal].txt").index=='A',"Pathspec globbing staged unrelated file");
        Require(GitWorkspace::UpdateIndex(project,status,Find(status,"Project/日本語 & %PATH%.txt"),true,error),error.c_str());
        status=GitWorkspace::ReadStatus(project);const auto expected=status;Git(repo,{L"add",L"--",L"outside.txt"});const auto index=Read(repo/".git/index");
        Require(!GitWorkspace::UpdateIndex(project,expected,Find(expected,"Project/keep.txt"),true,error) && Read(repo/".git/index")==index && error.find("changed")!=std::string::npos,"Stale global index admission wrote index");
        status=GitWorkspace::ReadStatus(project);const auto all=GitWorkspace::ReadStatus(repo);
        Require(!GitWorkspace::UpdateIndex(project,all,Find(all,"outside.txt"),false,error) && Read(repo/".git/index")==index,"Outside-project selection changed index");
        Write(repo/".git/index.lock","owned lock");Require(!GitWorkspace::UpdateIndex(project,status,Find(status,"Project/keep.txt"),true,error) && Read(repo/".git/index")==index,"Git lock failure altered index");fs::remove(repo/".git/index.lock");
        Require(SetEnvironmentVariableW(L"GIT_INDEX_FILE",(fixture/"external-index").c_str())!=FALSE,"Override setup failed");const bool refused=!GitWorkspace::UpdateIndex(project,status,Find(status,"Project/keep.txt"),true,error);SetEnvironmentVariableW(L"GIT_INDEX_FILE",nullptr);Require(refused && !fs::exists(fixture/"external-index"),"Git location override accepted");
        Git(repo,{L"mv",L"--",L"Project/rename.txt",L"Project/new.txt"});status=GitWorkspace::ReadStatus(project);const auto rename=Find(status,"Project/new.txt");Require(rename.index=='R' && rename.originalPath=="Project/rename.txt","Rename fixture not recognized");
        Require(GitWorkspace::UpdateIndex(project,status,rename,false,error) && Read(project/"new.txt")=="rename baseline" && !fs::exists(project/"rename.txt"),"Rename unstage restored/deleted worktree paths");
        Git(repo,{L"checkout",L"--detach",L"HEAD"});status=GitWorkspace::ReadStatus(project);Require(status.detached && GitWorkspace::UpdateIndex(project,status,Find(status,"Project/keep.txt"),true,error),"Detached index operation failed");
        const auto unborn=fixture/"Unborn";fs::create_directory(unborn);Git(unborn,{L"init",L"-b",L"main"});Write(unborn/"--dash.txt","first");status=GitWorkspace::ReadStatus(unborn);Require(status.head.empty() && GitWorkspace::UpdateIndex(unborn,status,Find(status,"--dash.txt"),true,error),error.c_str());
        Write(unborn/"--dash.txt","changed after staging");status=GitWorkspace::ReadStatus(unborn);Require(GitWorkspace::UpdateIndex(unborn,status,Find(status,"--dash.txt"),false,error) && Read(unborn/"--dash.txt")=="changed after staging","Unborn unstage failed/modified disk");
        std::error_code cleanup;fs::remove_all(fixture,cleanup);Require(!cleanup,"Owned fixture cleanup failed");std::cout<<"PASS: real nested/Unicode/literal/leading-dash index edits, worktree preservation, rename/detached/unborn handling and stale/global-scope/lock/environment refusal\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL: "<<failure.what()<<"; retained fixture "<<fixture<<'\n';return 1;}
}
