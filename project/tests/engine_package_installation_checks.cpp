#include "../launcher/EnginePackageInstallation.h"
#include <fstream>
#include <iostream>
namespace {
using Install=KuboHub::EnginePackageInstallation;
using Report=KuboHub::InstalledPackageReport;
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file<<bytes;Need(bool(file),"Fixture write failed");}
std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
std::string Manifest(){
    auto value=crude_json::value::parse(R"({"format":"kubo-portable-engine-2","engineVersion":"1.2.3","platform":"Windows-x64","launcher":"KuboLauncher.exe","editor":"KuboEditor.exe","player":"KuboPlayer.exe","configuration":"Release","files":[]})");
    for(const auto* role:{"KuboLauncher.exe","KuboEditor.exe","KuboPlayer.exe"}){
        crude_json::value entry(crude_json::object{});entry["path"]=role;entry["sha256"]="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";value["files"].push_back(entry);
    }return value.dump();
}
void Package(const std::filesystem::path& root){Need(std::filesystem::create_directory(root),"Fresh package fixture required");for(const auto* role:{"KuboLauncher.exe","KuboEditor.exe","KuboPlayer.exe"})Write(root/role,"abc");Write(root/"engine-package.json",Manifest());}
std::string Journal(const std::string& token,const std::string& hash){crude_json::value value(crude_json::object{});value["format"]="kubo-engine-install-1";value["token"]=token;value["manifestSha256"]=hash;return value.dump();}
void Old(Report& output){output.directory=L"old";output.package.declaredVersion="old";output.package.manifestSha256="old";output.alreadyPresent=true;output.journalRetained=true;}
void Preserved(const Report& output){Need(output.directory==L"old" && output.package.declaredVersion=="old" && output.package.manifestSha256=="old" && output.alreadyPresent && output.journalRetained,"Failure partially published result");}
}
int main(){try{
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/(L"kubo-install-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Need(fs::create_directory(root),"Fresh installation fixture required");Package(root/"Source");const auto manifest=root/"Source/engine-package.json",store=root/"Store";
    const auto original=Read(manifest);Report report;std::string error;
    auto status=Install::Install(manifest,store,report,error);Need(status==Install::Result::Installed,error.c_str());
    Need(report.package.declaredVersion=="1.2.3" && report.package.payloadFiles==3 && !report.alreadyPresent && !report.journalRetained && Read(report.directory/"engine-package.json")==original,"Installed package result is incomplete");
    const auto installed=report.directory;Need(fs::is_directory(installed) && installed.filename().wstring().starts_with(L"package-"),"Version publication is missing");
    Need(Read(installed/"KuboEditor.exe")=="abc" && Read(manifest)==original,"Install changed source bytes");
    status=Install::Install(manifest,store,report,error);Need(status==Install::Result::AlreadyPresent && report.alreadyPresent && report.directory==installed,"Repeated install did not preserve existing version");
    Write(installed/"user.txt","preserve");Old(report);Need(Install::Install(manifest,store,report,error)==Install::Result::Invalid,"Occupied modified version was overwritten");Preserved(report);Need(Read(installed/"user.txt")=="preserve","Occupied user file changed");fs::remove(installed/"user.txt");
    auto newer=crude_json::value::parse(original);newer["engineVersion"]="2.0.0";Write(manifest,newer.dump());
    status=Install::Install(manifest,store,report,error);Need(status==Install::Result::Installed,error.c_str());Need(report.directory!=installed && Read(installed/"engine-package.json")==original,"Installing another version overwrote old version");
    const auto newerInstalled=report.directory;Write(manifest,original);
    fs::create_directory(root/"Unmanaged");Write(root/"Unmanaged/user.txt","preserve");Old(report);
    Need(Install::Install(manifest,root/"Unmanaged",report,error)==Install::Result::Invalid,"Nonempty unmanaged directory adopted");Preserved(report);Need(!fs::exists(root/"Unmanaged/.install.lock") && Read(root/"Unmanaged/user.txt")=="preserve","Refusal modified unmanaged directory");
    Need(Install::Install(manifest,root/"Source/Nested",report,error)==Install::Result::Invalid,"Source/store overlap accepted");Need(!fs::exists(root/"Source/Nested"),"Overlap refusal created source directory");
    std::atomic_bool cancel{true};Need(Install::Install(manifest,root/"Cancelled",report,error,&cancel)==Install::Result::Cancelled,"Pre-cancel accepted");Preserved(report);Need(!fs::exists(root/"Cancelled"),"Pre-cancel changed destination");
    Need(Install::Install(manifest,root/"Space",report,error,nullptr,nullptr,~uint64_t{0})==Install::Result::Invalid,"Capacity refusal was bypassed");Preserved(report);Need(!fs::exists(root/"Space/package-"),"Capacity refusal published version");
    Need(Install::Install(manifest,root/"Lowered",report,error,nullptr,nullptr,1)==Install::Result::Invalid && !fs::exists(root/"Lowered"),"4 GiB reserve was weakened");
    fs::create_directory(root/"Outside");const auto linked=root/"LinkedStore";
    if(CreateSymbolicLinkW(linked.c_str(),(root/"Outside").c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|0x2)){
        Need(Install::Install(manifest,linked,report,error)==Install::Result::Invalid && fs::is_empty(root/"Outside"),"Destination reparse followed or changed outside root");Preserved(report);
        std::cout<<"PASS: destination store reparse refused without outside writes\n";
    }else std::cout<<"SKIP: destination symlink privilege unavailable\n";
    const auto markerAlias=root/"marker-alias";Need(CreateHardLinkW(markerAlias.c_str(),(store/"store-format.txt").c_str(),nullptr),"Cannot create marker hardlink");
    Need(Install::Install(manifest,store,report,error)==Install::Result::Invalid,"Hardlinked store marker accepted");Preserved(report);Need(DeleteFileW(markerAlias.c_str()),"Cannot retire own marker alias");
    HANDLE busy=CreateFileW((store/".install.lock").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);Need(busy!=INVALID_HANDLE_VALUE,"Cannot hold store lock");Need(Install::Install(manifest,store,report,error)==Install::Result::Invalid,"Overlapping install admitted");CloseHandle(busy);Preserved(report);
    const auto hash=installed.filename().string().substr(8);const std::string token(32,'a');const auto journal=store/("pending-"+token+".json"),candidate=store/(".pending-"+token);
    // Durable journal with publication already done: retire only its own record.
    Write(journal,Journal(token,hash));status=Install::Recover(store,token,report,error);Need(status==Install::Result::AlreadyPresent && report.directory==installed && !fs::exists(journal),"Published journal recovery failed");
    // Journal before candidate, then incomplete candidate, then complete candidate.
    fs::rename(installed,root/"HeldOldVersion");Write(journal,Journal(token,hash));Old(report);
    Need(Install::Recover(store,token,report,error)==Install::Result::Invalid && fs::exists(journal),"Missing candidate recovery invented bytes");Preserved(report);
    Need(fs::create_directory(candidate),"Cannot create partial candidate");Write(candidate/"engine-package.json",original);
    Need(Install::Recover(store,token,report,error)==Install::Result::Invalid && fs::exists(journal) && fs::exists(candidate),"Partial candidate was published or deleted");Preserved(report);
    for(const auto* role:{"KuboLauncher.exe","KuboEditor.exe","KuboPlayer.exe"})Write(candidate/role,"abc");
    status=Install::Recover(store,token,report,error);Need(status==Install::Result::Installed,error.c_str());Need(report.directory==installed && !fs::exists(candidate) && !fs::exists(journal) && Read(root/"HeldOldVersion/KuboEditor.exe")=="abc","Complete candidate recovery altered old version or retained live journal");
    Write(journal,Journal(token,hash));fs::create_directory(candidate);Old(report);
    Need(Install::Recover(store,token,report,error)==Install::Result::Invalid && fs::exists(candidate) && fs::exists(journal),"Ambiguous recovery deleted user data");Preserved(report);
    Need(Install::Recover(store,"../escape",report,error)==Install::Result::Invalid,"Recovery token escaped store");
    Write(journal,"{\"format\":\"kubo-engine-install-1\",\"token\":\"bad\",\"manifestSha256\":\"bad\"}");Need(Install::Recover(store,token,report,error)==Install::Result::Invalid,"Malformed journal accepted");
    Need(Read(manifest)==original && Read(newerInstalled/"KuboEditor.exe")=="abc" && !fs::exists(root/"engine_catalog.json"),"Installation/recovery changed source, another version or catalog");
    std::cout<<"PASS: held-source copy/reverification/non-overwrite versions/idempotence/old-output retention/unmanaged overlap space busy cancel refusals/durable missing-partial-complete-published-ambiguous journal recovery (no fake EXEs launched; no publisher/catalog/hostile-race/power-loss proof) retained="<<root<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
