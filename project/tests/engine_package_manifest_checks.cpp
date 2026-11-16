#include "engine/services/EnginePackageManifest.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <new>
namespace {bool tracking=false,injected=false;size_t allocations=0,failAt=0;
void Need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
void Write(const std::filesystem::path& p,const std::string& bytes){std::ofstream out(p,std::ios::binary);out<<bytes;out.close();Need(!out.fail(),"Package fixture write failed");}
bool Equal(const Engine::Services::EnginePackageManifest& a,const Engine::Services::EnginePackageManifest& b){if(a.format!=b.format || a.engineVersion!=b.engineVersion || a.platform!=b.platform || a.bytes!=b.bytes || a.files.size()!=b.files.size())return false;for(size_t i=0;i<a.files.size();++i)if(a.files[i].path!=b.files[i].path || a.files[i].sha256!=b.files[i].sha256)return false;return true;}
}
void* operator new(size_t n){if(tracking && ++allocations==failAt){injected=true;throw std::bad_alloc();}if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}void operator delete[](void* p,size_t) noexcept{std::free(p);}
int main(){try{
 using M=Engine::Services::EnginePackageManifest;using V=crude_json::value;
 auto data=V::parse(R"({"format":"kubo-portable-engine-2","engineVersion":"1.2.3","platform":"Windows-x64","launcher":"KuboLauncher.exe","editor":"KuboEditor.exe","player":"KuboPlayer.exe","configuration":"Release","files":[]})");
 for(const auto* name:{"KuboLauncher.exe","KuboEditor.exe","KuboPlayer.exe"})data["files"].push_back(V(crude_json::object{{"path",name},{"sha256",std::string(64,'A')}}));
 std::string error;M output;const auto valid=data.dump();if(!M::Decode(valid,output,error))throw std::runtime_error(error);
 Need(output.Versioned() && output.engineVersion=="1.2.3" && output.platform=="Windows-x64" && output.bytes==valid && output.files[0].sha256==std::string(64,'a'),"Versioned declaration failed");
 const auto retained=output;auto Reject=[&](const std::string& text){Need(!M::Decode(text,output,error) && !error.empty() && Equal(output,retained),"Malformed package changed previous output or was accepted");};
 auto legacy=data;legacy["format"]="kubo-portable-engine-1";legacy.erase("engineVersion");legacy.erase("platform");M old;
 Need(M::Decode(legacy.dump(),old,error) && !old.Versioned() && old.engineVersion.empty() && old.platform.empty(),"Legacy package fabricated version/platform");
 for(const auto* version:{"","1.",".1","1..0","-1","+1","1.2beta","1.2+build"," 1.2","1.2 ","2147483648","١.٢"}){auto bad=data;bad["engineVersion"]=version;Reject(bad.dump());Need(!M::ValidVersion(version),"Invalid release version accepted");}
 Need(M::ValidVersion("0.01.2147483647") && M::ValidVersion("0."+std::string(126,'0')) && !M::ValidVersion("0."+std::string(127,'0')),"Version boundary contract failed");
 for(const auto* field:{"format","platform","editor","configuration"}){auto bad=data;bad[field]="unsupported";Reject(bad.dump());}
 {auto bad=data;bad["engineVersion"]=1.0;Reject(bad.dump());bad=data;bad["unknown"]=true;Reject(bad.dump());bad=data;bad["files"][size_t(0)]["unknown"]=false;Reject(bad.dump());}
 for(const auto* path:{"../escape.exe","/absolute.exe","C:/outside.exe","a\\b","a//b","a/./b","a/../b","a/","a./b","a /b","NUL.txt","com1.bin","LPT9","COM¹.txt","a:stream","engine-package.json","KUBOEDITOR.EXE"}){auto bad=data;bad["files"].push_back(V(crude_json::object{{"path",path},{"sha256",std::string(64,'0')}}));Reject(bad.dump());}
 {auto bad=data;bad["files"][size_t(0)]["sha256"]="bad";Reject(bad.dump());bad=data;bad["files"].get<crude_json::array>().erase(bad["files"].get<crude_json::array>().begin());Reject(bad.dump());}
 Reject("{\"format\":\"x\",\"\\u0066ormat\":\"y\"}");Reject("{\"format\":\"\\ud800\"}");Reject("{\"format\":\"\\udc00\"}");Reject(valid+"x");Reject(std::string(M::MaxBytes+1,' '));Reject(std::string(1,static_cast<char>(0xff)));Reject("[[[[[[[[[[0]]]]]]]]]]");
 {auto unicode=data;unicode["files"].push_back(V(crude_json::object{{"path","Resources/日本語/😀.bin"},{"sha256",std::string(64,'1')}}));M decoded;Need(M::Decode("\xEF\xBB\xBF"+unicode.dump(),decoded,error) && decoded.files.back().path=="Resources/日本語/😀.bin","Unicode/BOM package failed");}
 {auto bounded=data;for(size_t i=3;i<M::MaxFiles;++i)bounded["files"].push_back(V(crude_json::object{{"path","Assets/file-"+std::to_string(i)},{"sha256",std::string(64,'0')}}));M decoded;Need(M::Decode(bounded.dump(),decoded,error) && decoded.files.size()==M::MaxFiles,"Exact package file cap refused");bounded["files"].push_back(V(crude_json::object{{"path","Assets/one-extra"},{"sha256",std::string(64,'0')}}));Reject(bounded.dump());}
 size_t positions=0;for(size_t position=0;;++position){output=old;bool result=false,threw=false;tracking=true;allocations=0;failAt=position;injected=false;try{result=M::Decode(valid,output,error);}catch(...){threw=true;}tracking=false;failAt=0;Need(!threw,"Single metadata allocation failure escaped");if(position==0){positions=allocations;Need(result && positions>0 && output.Versioned() && output.bytes==valid && !Equal(output,old),"Metadata allocation control failed");}else Need(injected && !result && Equal(output,old),"Metadata allocation failure partially published distinct old output or injection not consumed");if(position==positions)break;}
 const auto root=std::filesystem::temp_directory_path()/(L"kubo-package-manifest-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(!std::filesystem::exists(root),"Fresh metadata fixture required");std::filesystem::create_directories(root);const auto file=root/L"engine-package.json";output=retained;
 Need(M::Read(file,output,error)==M::ReadResult::Missing && Equal(output,retained),"Missing metadata changed output");Write(file,valid);output=old;Need(M::Read(file,output,error)==M::ReadResult::Success && Equal(output,retained),"Native stable metadata read failed to replace distinct old state");
 HANDLE writer=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Need(writer!=INVALID_HANDLE_VALUE,"Metadata writer fixture failed");const auto locked=M::Read(file,output,error);CloseHandle(writer);Need(locked==M::ReadResult::Invalid && Equal(output,retained),"Writable metadata admitted");
 const auto link=root/L"linked.json";Need(CreateHardLinkW(link.c_str(),file.c_str(),nullptr)!=FALSE,"Metadata hardlink fixture failed");Need(M::Read(file,output,error)==M::ReadResult::Invalid && Equal(output,retained),"Hardlinked metadata admitted");Need(DeleteFileW(link.c_str())!=FALSE,"Owned metadata link retirement failed");
 Write(file,"broken JSON");Need(M::Read(file,output,error)==M::ReadResult::Invalid && Equal(output,retained),"Invalid native read changed prior output");Need(M::Read(L"relative.json",output,error)==M::ReadResult::Invalid,"Cwd-dependent metadata path accepted");
 std::cout<<"PASS: versioned/legacy package declarations, strict UTF8/JSON/duplicate/depth/file/path/hash/version bounds, "<<positions<<" consumed ordinary allocation failures and read-only native missing/locked/hardlink/output isolation (NOT payload/signature/ABI/install verification) retained="<<root<<'\n';return 0;
}catch(const std::exception& e){tracking=false;std::cerr<<e.what()<<'\n';return 1;}}
