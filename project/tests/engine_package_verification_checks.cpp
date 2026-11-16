#include "../launcher/PackageVerificationJob.h"
#include <fstream>
#include <iostream>
#include <thread>

namespace {
using Verify=KuboHub::EnginePackageVerification;
using Report=KuboHub::PackageVerificationReport;
const std::string hash="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Write(const std::filesystem::path& path,const std::string& text){std::ofstream file(path,std::ios::binary);file<<text;Need(bool(file),"Fixture write failed");}
std::string Declaration(bool versioned=true){
    crude_json::value value(crude_json::object{});
    value["format"]=versioned?"kubo-portable-engine-2":"kubo-portable-engine-1";
    value["launcher"]="KuboLauncher.exe";value["editor"]="KuboEditor.exe";value["player"]="KuboPlayer.exe";value["configuration"]="Release";
    if(versioned){value["engineVersion"]="1.2.3";value["platform"]="Windows-x64";}
    value["files"]=crude_json::array{};
    for(const auto* path:{"KuboLauncher.exe","KuboEditor.exe","KuboPlayer.exe","Assets/日本語.txt"}){
        crude_json::value entry(crude_json::object{});entry["path"]=path;entry["sha256"]=hash;value["files"].push_back(entry);
    }
    return value.dump();
}
struct Fixture {
    std::filesystem::path root=std::filesystem::temp_directory_path()/
        (L"kubo-package-verify-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Fixture(){Need(std::filesystem::create_directories(root/"Assets"),"Fixture directory already exists");Reset();}
    void Reset(){for(const auto* file:{L"KuboLauncher.exe",L"KuboEditor.exe",L"KuboPlayer.exe",L"Assets/日本語.txt"})Write(root/file,"abc");Write(root/"engine-package.json",Declaration());}
    ~Fixture()=default; // Retain fixture, including diagnostics, for inspection.
};
Report Old(){Report report;report.declaredVersion="old";report.manifestSha256="old-hash";report.totalBytes=17;report.payloadFiles=99;return report;}
void Refused(const std::filesystem::path& path,KuboHub::PackageVerificationLimits limits={}){
    auto report=Old();std::string error;
    Need(Verify::Verify(path,report,error,limits)==Verify::Result::Invalid && !error.empty(),"Invalid source accepted");
    Need(report.declaredVersion=="old" && report.manifestSha256=="old-hash" && report.totalBytes==17 && report.payloadFiles==99,"Failed verification partially published report");
}
void Wait(KuboHub::PackageVerificationJob& job){
    const auto end=GetTickCount64()+10000;
    while(!job.Ready() && GetTickCount64()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Need(job.Ready(),"Owned package job did not complete");
}
}
int wmain(int argc,wchar_t** argv){
    try{
        if(argc==2){Report report;std::string error;Need(Verify::Verify(std::filesystem::absolute(argv[1]),report,error)==Verify::Result::Success,error.c_str());std::cout<<"PASS: real package payloads="<<report.payloadFiles<<" bytes="<<report.totalBytes<<" version="<<report.declaredVersion<<" manifest="<<report.manifestSha256<<" (not publisher authentication)\n";return 0;}
        Fixture fixture;const auto manifest=fixture.root/"engine-package.json";Report report;std::string error;std::atomic<uint64_t> progress{0};
        Need(Verify::Verify(manifest,report,error,{},nullptr,&progress)==Verify::Result::Success,error.c_str());
        Need(report.declaredVersion=="1.2.3" && report.payloadFiles==4 && report.totalBytes==Declaration().size()+12 && progress==report.totalBytes && report.manifestSha256.size()==64,"Successful report is incomplete");
        Write(manifest,Declaration(false));Need(Verify::Verify(manifest,report,error)==Verify::Result::Success && report.declaredVersion.empty(),"Legacy package lost compatibility");fixture.Reset();
        Refused("engine-package.json");Refused(fixture.root/"other.json");
        Write(fixture.root/"KuboEditor.exe","tampered");Refused(manifest);fixture.Reset();
        std::filesystem::remove(fixture.root/"KuboPlayer.exe");Refused(manifest);fixture.Reset();
        Write(fixture.root/"extra.txt","abc");Refused(manifest);std::filesystem::remove(fixture.root/"extra.txt");
        std::filesystem::create_directories(fixture.root/"Empty");Need(Verify::Verify(manifest,report,error)==Verify::Result::Success,"Ordinary empty directory rejected");
        auto limits=KuboHub::PackageVerificationLimits{};limits.fileBytes=2;Refused(manifest,limits);
        limits={};limits.totalBytes=Declaration().size()+11;Refused(manifest,limits);
        limits={};limits.entries=5;Refused(manifest,limits);limits={};limits.directories=1;Refused(manifest,limits);
        limits={};limits.entries=8193;Refused(manifest,limits);limits={};limits.fileBytes=(uint64_t{1}<<30)+1;Refused(manifest,limits);
        std::atomic_bool cancel{true};report=Old();
        Need(Verify::Verify(manifest,report,error,{},&cancel)==Verify::Result::Cancelled && report.declaredVersion=="old","Cancellation published partial report");
        HANDLE writer=CreateFileW((fixture.root/"KuboEditor.exe").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        Need(writer!=INVALID_HANDLE_VALUE,"Cannot hold writer");Refused(manifest);CloseHandle(writer);
        const auto alias=fixture.root.parent_path()/(fixture.root.filename().wstring()+L"-hardlink");
        Need(CreateHardLinkW(alias.c_str(),(fixture.root/"KuboEditor.exe").c_str(),nullptr),"Cannot create hardlink fixture");Refused(manifest);Need(DeleteFileW(alias.c_str()),"Cannot retire fixture hardlink");
        Need(CreateHardLinkW(alias.c_str(),manifest.c_str(),nullptr),"Cannot create manifest hardlink");Refused(manifest);Need(DeleteFileW(alias.c_str()),"Cannot retire manifest hardlink");
        const auto stream=fixture.root/L"KuboEditor.exe:unexpected";Write(stream,"hidden stream");Refused(manifest);Need(DeleteFileW(stream.c_str()),"Cannot retire alternate stream");
        // A symbolic directory link is tested when the OS grants this privilege.
        const auto link=fixture.root/"Linked";
        if(CreateSymbolicLinkW(link.c_str(),(fixture.root/"Assets").c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|0x2)){
            Refused(manifest);Need(RemoveDirectoryW(link.c_str()),"Cannot retire symbolic directory fixture");
            const auto ancestorAlias=fixture.root.parent_path()/(fixture.root.filename().wstring()+L"-alias");
            Need(CreateSymbolicLinkW(ancestorAlias.c_str(),fixture.root.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|0x2),"Cannot create ancestor alias");
            Refused(ancestorAlias/"engine-package.json");Need(RemoveDirectoryW(ancestorAlias.c_str()),"Cannot retire ancestor alias");
            const auto original=fixture.root/L"KuboPlayer.exe",outside=fixture.root.parent_path()/(fixture.root.filename().wstring()+L"-outside");
            std::filesystem::rename(original,outside);
            Need(CreateSymbolicLinkW(original.c_str(),outside.c_str(),0x2),"Cannot create payload symlink");Refused(manifest);
            Need(DeleteFileW(original.c_str()),"Cannot retire payload symlink");std::filesystem::rename(outside,original);
            std::cout<<"PASS: child directory, ancestor and payload reparse refusals\n";
        }else std::cout<<"SKIP: directory symlink fixture privilege unavailable; no reparse runtime claim\n";
        KuboHub::PackageVerificationJob job;
        Need(job.Start(manifest) && !job.Start(manifest),"Package job admitted overlapping work");Wait(job);auto outcome=job.Take();
        Need(!job.Active() && outcome.result==Verify::Result::Success && job.Progress()==outcome.report.totalBytes,"Owned asynchronous verification failed");
        Need(job.Start(fixture.root/"missing.json"),"Job could not restart after retirement");Wait(job);outcome=job.Take();Need(outcome.result==Verify::Result::Invalid,"Job failure is not explicit");
        // Released handles permit writes and renames after every completed path.
        fixture.Reset();std::filesystem::rename(fixture.root/"Assets",fixture.root/"Renamed");std::filesystem::rename(fixture.root/"Renamed",fixture.root/"Assets");
        Need(Verify::Verify(manifest,report,error)==Verify::Result::Success,"Handle retirement or recovery failed");
        std::cout<<"PASS: held source SHA-256/namespace/Unicode/legacy/bounds/cancel/writer/hardlink/ADS/report preservation/job ownership/retirement\nRetained fixture: "<<fixture.root.string()<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
