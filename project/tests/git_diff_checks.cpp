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
Engine::Services::GitChange Find(const Engine::Services::GitStatus& status,const std::string& path){Require(status.repository && status.error.empty(),status.error.c_str());for(const auto& entry:status.changes)if(entry.path==path)return entry;throw std::runtime_error("Expected Git entry missing: "+path);}
int main(){
    namespace fs=std::filesystem;using namespace Engine::Services;
    const auto fixture=fs::temp_directory_path()/(L"kubo-git-diff-日本語 & %-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        for(const auto* variable:{L"GIT_DIR",L"GIT_WORK_TREE",L"GIT_INDEX_FILE",L"GIT_COMMON_DIR",L"GIT_OBJECT_DIRECTORY"})Require(!GetEnvironmentVariableW(variable,nullptr,0),"Fixture refuses inherited Git location override");
        const auto repo=fixture/"Repo",project=repo/"Project";fs::create_directories(project);
        Git(repo,{L"init",L"-b",L"main"});Git(repo,{L"config",L"user.name",L"Kubo test"});Git(repo,{L"config",L"user.email",L"test@example.invalid"});Git(repo,{L"config",L"core.attributesFile",L"NUL"});Git(repo,{L"config",L"core.autocrlf",L"false"});Git(repo,{L"config",L"diff.relative",L"true"});Git(repo,{L"config",L"color.ui",L"always"});
        Write(project/"keep.txt","baseline\n");Write(project/"rename.txt","rename baseline\n");Write(repo/"outside.txt","outside baseline\n");Git(repo,{L"add",L"--",L"."});Git(repo,{L"commit",L"-m",L"owned baseline"});
        std::string display,error;
        const auto preview=[&](const GitStatus& status,const GitChange& entry,bool staged,bool success){
            const auto index=Read(repo/".git/index");const auto timestamp=fs::last_write_time(repo/".git/index");const auto head=GitWorkspace::ReadStatus(project).head;display="unchanged sentinel";
            Require(GitWorkspace::ReadDiff(project,status,entry,staged,display,error)==success,error.c_str());
            Require(Read(repo/".git/index")==index && fs::last_write_time(repo/".git/index")==timestamp && GitWorkspace::ReadStatus(project).head==head,"Read-only diff changed index/HEAD");
            if(!success)Require(display=="unchanged sentinel","Failed diff exposed partial output");
        };
        Write(project/"keep.txt","staged bytes\n");Git(repo,{L"add",L"--",L"Project/keep.txt"});Write(project/"keep.txt","newer disk bytes\n");auto status=GitWorkspace::ReadStatus(project);auto entry=Find(status,"Project/keep.txt");
        preview(status,entry,true,true);Require(display.find("+staged bytes")!=std::string::npos && display.find("newer disk bytes")==std::string::npos,"Staged preview used disk instead of index");
        preview(status,entry,false,true);Require(display.find("-staged bytes")!=std::string::npos && display.find("+newer disk bytes")!=std::string::npos && display.find('\x1b')==std::string::npos,"Unstaged preview/forced no-color failed");Require(Read(project/"keep.txt")=="newer disk bytes\n","Diff rewrote disk");
        Write(project/L"日本語 & %PATH% [literal].txt","new unicode\n");status=GitWorkspace::ReadStatus(project);entry=Find(status,"Project/日本語 & %PATH% [literal].txt");preview(status,entry,false,true);Require(display.find("+new unicode")!=std::string::npos,"Untracked new-file preview missing");preview(status,entry,true,false);
        Write(project/"[literal].txt","selected literal\n");Write(project/"l.txt","unselected sibling\n");Git(repo,{L"add",L"--",L"Project/[literal].txt",L"Project/l.txt"});status=GitWorkspace::ReadStatus(project);preview(status,Find(status,"Project/[literal].txt"),true,true);Require(display.find("selected literal")!=std::string::npos && display.find("unselected sibling")==std::string::npos,"Diff globbed a literal filename");
        fs::create_directory(project/"NewDirectory");Write(project/"NewDirectory/file.txt","child\n");status=GitWorkspace::ReadStatus(project);preview(status,Find(status,"Project/NewDirectory/"),false,false);
        Write(project/"new.bin",std::string("a\0b",3));status=GitWorkspace::ReadStatus(project);preview(status,Find(status,"Project/new.bin"),false,true);Require(display.find("Binary files")!=std::string::npos && display.find('\0')==std::string::npos,"Binary preview not a safe summary");
        Write(project/"legacy.txt",std::string("bad \xff \x1b\n",8));status=GitWorkspace::ReadStatus(project);preview(status,Find(status,"Project/legacy.txt"),false,true);Require(display.find("\\xFF")!=std::string::npos && display.find("\\x1B")!=std::string::npos && display.find('\0')==std::string::npos,"Non-UTF8/control display was not escaped");
        Write(repo/".gitattributes","*.txt diff=probe\n");Write(repo/"probe.sh","#!/bin/sh\nprintf ran > diff-helper-ran\nexit 1\n");const auto helper=L"sh \""+(repo/"probe.sh").wstring()+L"\"";Git(repo,{L"config",L"diff.external",helper});Git(repo,{L"config",L"diff.probe.textconv",helper});status=GitWorkspace::ReadStatus(project);preview(status,Find(status,"Project/keep.txt"),false,true);Require(!fs::exists(repo/"diff-helper-ran"),"External diff/textconv helper executed");
        const auto stale=status;Git(repo,{L"add",L"--",L"outside.txt"});Write(repo/"outside.txt","outside later\n");Git(repo,{L"add",L"--",L"outside.txt"});preview(stale,Find(stale,"Project/keep.txt"),false,false);const auto all=GitWorkspace::ReadStatus(repo);preview(all,Find(all,"outside.txt"),true,false);
        Git(repo,{L"mv",L"--",L"Project/rename.txt",L"Project/moved.txt"});status=GitWorkspace::ReadStatus(project);preview(status,Find(status,"Project/moved.txt"),true,true);Require(display.find("Project/rename.txt")!=std::string::npos && display.find("Project/moved.txt")!=std::string::npos,"Rename diff omitted an endpoint");
        for(const auto& path:std::vector<std::string>{"../escape.txt",".git/config",std::string("bad\0path",8),std::string("bad\xff",4)}) {
            auto forged=status;GitChange unsafe{'M','M',path,{}};forged.changes.push_back(unsafe);preview(forged,unsafe,false,false);
        }
        fs::remove(project/"keep.txt");status=GitWorkspace::ReadStatus(project);preview(status,Find(status,"Project/keep.txt"),false,true);Require(display.find("-staged bytes")!=std::string::npos,"Deleted worktree file preview failed");
        Git(repo,{L"commit",L"-m",L"owned staged rename and outside baseline"});Git(repo,{L"mv",L"--",L"outside.txt",L"Project/incoming.txt"});
        const auto globalRename=GitWorkspace::ReadStatus(repo);entry=Find(globalRename,"Project/incoming.txt");Require(entry.originalPath=="outside.txt","Global cross-project rename fixture was not detected");preview(globalRename,entry,true,false);
        // Path-filtered status may report A rather than R for an incoming rename.
        // That preview must stay on the inside path, never widen to the deletion.
        status=GitWorkspace::ReadStatus(project);entry=Find(status,"Project/incoming.txt");if(entry.originalPath.empty()){preview(status,entry,true,true);Require(display.find("outside.txt")==std::string::npos && display.find("+outside later")!=std::string::npos,"Filtered addition widened to the outside rename endpoint");}else preview(status,entry,true,false);
        Git(repo,{L"checkout",L"--detach",L"HEAD"});status=GitWorkspace::ReadStatus(project);Require(status.detached,"Detached fixture failed");preview(status,Find(status,"Project/keep.txt"),false,true);
        Write(project/"huge.txt",std::string(3*1024*1024,'x')+"\n");status=GitWorkspace::ReadStatus(project);preview(status,Find(status,"Project/huge.txt"),false,false);Require(error.find("2 MiB")!=std::string::npos,"Oversized diff limit missing");
        Require(SetEnvironmentVariableW(L"GIT_INDEX_FILE",(fixture/"external-index").c_str())!=FALSE,"Override setup failed");const bool refused=!GitWorkspace::ReadDiff(project,status,Find(status,"Project/keep.txt"),false,display,error);SetEnvironmentVariableW(L"GIT_INDEX_FILE",nullptr);Require(refused && !fs::exists(fixture/"external-index"),"Location override accepted");
        const auto unborn=fixture/"Unborn";fs::create_directory(unborn);Git(unborn,{L"init",L"-b",L"main"});Write(unborn/"--dash.txt","first\n");Git(unborn,{L"add",L"--",L"--dash.txt"});status=GitWorkspace::ReadStatus(unborn);const auto initialIndex=Read(unborn/".git/index");Require(GitWorkspace::ReadDiff(unborn,status,Find(status,"--dash.txt"),true,display,error) && display.find("+first")!=std::string::npos && Read(unborn/".git/index")==initialIndex,"Initial staged diff failed");
        std::error_code cleanup;fs::remove_all(fixture,cleanup);Require(!cleanup,"Owned fixture cleanup failed");std::cout<<"PASS: read-only staged/unstaged/new/binary/escaped/literal/rename/unborn diffs, index bytes/time and HEAD retention, scope/stale/environment/output-limit refusal and no external diff/textconv\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL: "<<failure.what()<<"; retained fixture "<<fixture<<'\n';return 1;}
}
