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
    const auto fixture=fs::temp_directory_path()/(L"kubo-git-history-日本語 & %-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        for(const auto* variable:{L"GIT_DIR",L"GIT_WORK_TREE",L"GIT_INDEX_FILE",L"GIT_COMMON_DIR",L"GIT_OBJECT_DIRECTORY"})Require(!GetEnvironmentVariableW(variable,nullptr,0),"Fixture refuses inherited Git location override");
        std::vector<GitCommitEntry> parsed{{"retained","","",""}};std::string error,record=std::string(40,'a');record+='\0';record+="作者 & %PATH%";record+='\0';record+="2026-10-08T09:00:00+09:00";record+='\0';record+="subject ### %s \x1b";record+='\0';
        Require(GitWorkspace::ParseHistory(record,parsed,error) && parsed.size()==1 && parsed[0].author=="作者 & %PATH%" && parsed[0].subject.find("\\x1B")!=std::string::npos,"NUL Unicode/control history parsing failed");
        const auto original=parsed[0].hash;Require(!GitWorkspace::ParseHistory(record.substr(0,record.size()-1),parsed,error) && parsed[0].hash==original,"Malformed history replaced prior output");
        auto invalid=record;invalid[0]='x';Require(!GitWorkspace::ParseHistory(invalid,parsed,error),"Invalid history hash admitted");std::string many;for(int i=0;i<52;++i)many+=record;Require(!GitWorkspace::ParseHistory(many,parsed,error),"Page record limit not enforced");
        const auto repo=fixture/"Repo",project=repo/L"Project [literal]";fs::create_directories(project);
        Git(repo,{L"init",L"-b",L"main"});Git(repo,{L"config",L"user.name",L"作者 & Kubo"});Git(repo,{L"config",L"user.email",L"test@example.invalid"});Git(repo,{L"config",L"core.attributesFile",L"NUL"});Git(repo,{L"config",L"log.follow",L"true"});Git(repo,{L"config",L"log.showSignature",L"true"});Git(repo,{L"config",L"format.pretty",L"raw"});Git(repo,{L"config",L"color.ui",L"always"});
        GitHistory history;auto status=GitWorkspace::ReadStatus(project);Require(GitWorkspace::ReadHistory(project,status,0,history,error) && history.entries.empty() && history.head.empty(),"Unborn history failed");
        for(int i=0;i<55;++i){Write(project/"entry.txt",std::to_string(i));Git(repo,{L"add",L"--",L"Project [literal]/entry.txt"});Git(repo,{L"commit",L"-m",L"project 日本語 & %PATH% "+std::to_wstring(i)});}
        Write(repo/"outside.txt","outside only");Git(repo,{L"add",L"--",L"outside.txt"});Git(repo,{L"commit",L"-m",L"outside-only commit"});status=GitWorkspace::ReadStatus(project);
        const auto index=Read(repo/".git/index");const auto timestamp=fs::last_write_time(repo/".git/index");
        Require(GitWorkspace::ReadHistory(project,status,0,history,error),error.c_str());Require(history.entries.size()==50 && history.more && history.head==status.head && history.entries.front().subject=="project 日本語 & %PATH% 54" && history.entries.back().subject=="project 日本語 & %PATH% 5","First history page/scope/order failed");
        Require(history.entries[0].author=="作者 & Kubo" && history.entries[0].date.find('T')!=std::string::npos && history.entries[0].hash.size()==40,"Live history fields lost metadata");
        const auto first=history.entries[0].hash;Require(GitWorkspace::ReadHistory(project,status,50,history,error) && history.entries.size()==5 && !history.more && history.entries.front().subject=="project 日本語 & %PATH% 4" && history.entries.back().subject=="project 日本語 & %PATH% 0","Older history page failed");
        Require(Read(repo/".git/index")==index && fs::last_write_time(repo/".git/index")==timestamp && GitWorkspace::ReadStatus(project).head==status.head && Read(project/"entry.txt")=="54","History altered disk/index/HEAD");
        const auto previous=history.head;Require(!GitWorkspace::ReadHistory(project,status,10001,history,error) && history.head==previous,"Offset refusal replaced history");
        Require(!GitWorkspace::ReadHistory(fixture,status,0,history,error) && history.head==previous,"Outside project admitted");
        const auto emptyProject=repo/"Empty";fs::create_directory(emptyProject);Require(GitWorkspace::ReadHistory(emptyProject,GitWorkspace::ReadStatus(emptyProject),0,history,error) && history.entries.empty(),"Unchanged nested project leaked sibling history");
        Git(repo,{L"checkout",L"--detach",L"HEAD"});const auto detached=GitWorkspace::ReadStatus(project);Require(detached.detached && GitWorkspace::ReadHistory(project,detached,0,history,error) && history.entries[0].hash==first,"Detached history failed");
        Require(!GitWorkspace::ReadHistory(project,status,0,history,error) && error.find("changed")!=std::string::npos,"Stale branch history admitted");
        Git(repo,{L"checkout",L"main"});status=GitWorkspace::ReadStatus(project);Git(repo,{L"commit",L"--allow-empty",L"-m",L"owned empty head change"});Require(!GitWorkspace::ReadHistory(project,status,0,history,error) && error.find("changed")!=std::string::npos,"Stale HEAD history admitted");
        Require(SetEnvironmentVariableW(L"GIT_INDEX_FILE",(fixture/"external-index").c_str())!=FALSE,"Override setup failed");const bool refused=!GitWorkspace::ReadHistory(project,status,0,history,error);SetEnvironmentVariableW(L"GIT_INDEX_FILE",nullptr);Require(refused && !fs::exists(fixture/"external-index"),"Location override accepted");
        std::error_code cleanup;fs::remove_all(fixture,cleanup);Require(!cleanup,"Owned fixture cleanup failed");std::cout<<"PASS: NUL history parser, project-only literal Unicode 50/5 paging, unborn/detached, preserved disk/index bytes/time/HEAD and scope/stale/environment/offset refusal\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL: "<<failure.what()<<"; retained fixture "<<fixture<<'\n';return 1;}
}
