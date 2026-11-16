// Test the real Hub commit and control-refresh functions without visible UI.
#define UNICODE
#define _UNICODE
#include "../launcher/main.cpp"
#include <cstdlib>
#include <new>
#include <iostream>
namespace {
bool tracking=false,injected=false;size_t allocations=0,failAt=0;
WNDPROC originalCombo=nullptr;int refuseAdd=0;
LRESULT CALLBACK RefusingCombo(HWND window,UINT message,WPARAM w,LPARAM l){if(message==CB_ADDSTRING && refuseAdd){--refuseAdd;return CB_ERRSPACE;}return CallWindowProcW(originalCombo,window,message,w,l);}
void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);Need(in.good(),"Catalog fixture read failed");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<bytes;out.close();Need(!out.fail(),"Catalog fixture write failed");}
bool SameCatalog(const KuboHub::EngineCatalog& a,const KuboHub::EngineCatalog& b){return a.active==b.active && a.editors==b.editors;}
bool SameSnapshot(const KuboHub::CatalogSnapshot& a,const KuboHub::CatalogSnapshot& b){return a.bytes==b.bytes && a.exists==b.exists && a.path==b.path;}
struct OwnedWindow {HWND value=nullptr;~OwnedWindow(){if(value)DestroyWindow(value);g_engineList=nullptr;}};
}
void* operator new(size_t n){if(tracking && ++allocations==failAt){injected=true;throw std::bad_alloc();}if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}void operator delete[](void* p,size_t) noexcept{std::free(p);}
int main(){try{
 namespace fs=std::filesystem;using namespace KuboHub;
 const auto root=fs::temp_directory_path()/(L"kubo-catalog-publication-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(!fs::exists(root),"Fresh catalog fixture required");fs::create_directories(root);
 const auto first=root/(L"日本語-"+std::wstring(80,L'a'))/L"KuboEditor.exe",second=root/std::wstring(90,L'b')/L"KuboEditor.exe";
 const EngineCatalog old{{first},first},next{{first,second},second};std::string error;
 OwnedWindow parent;parent.value=CreateWindowExW(0,L"STATIC",L"Catalog publication test",WS_OVERLAPPED,0,0,300,150,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);Need(parent.value!=nullptr,"Owned Hub test parent failed");
 g_engineList=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|CBS_DROPDOWNLIST,0,0,280,100,parent.value,ControlId(kEngineList),GetModuleHandleW(nullptr),nullptr);Need(g_engineList!=nullptr,"Owned Hub combo failed");
 Need(CreateWindowExW(0,L"BUTTON",L"Remove",WS_CHILD,0,100,80,25,parent.value,ControlId(kRemoveEngine),GetModuleHandleW(nullptr),nullptr)!=nullptr,"Owned Hub remove button failed");
 size_t total=0;
 for(int route=0;route<2;++route){const auto file=root/(L"load-"+std::to_wstring(route)+L".json");EngineCatalog unused;CatalogSnapshot seed;
  if(!EngineCatalogFile::Load(file,unused,seed,error) || !EngineCatalogFile::SaveChecked(file,next,seed,error))throw std::runtime_error(error);
  if(route==1)fs::remove(file);const auto bytes=route==0?Read(file):std::string{};size_t positions=0;
  for(size_t position=0;;++position){auto loaded=old;CatalogSnapshot snapshot{"retained snapshot",false,root/L"previous.json"};const auto before=snapshot;bool result=false,threw=false;
   tracking=true;allocations=0;failAt=position;injected=false;try{result=EngineCatalogFile::Load(file,loaded,snapshot,error);}catch(...){threw=true;}tracking=false;failAt=0;
   Need(position==0 || injected,"Catalog load fault position was not consumed");
   Need(!threw,"Single allocation fault escaped catalog load");if(position==0){positions=allocations;total+=positions;Need(result && positions>0,"Catalog load control failed");}
   if(!result)Need(SameCatalog(loaded,old) && SameSnapshot(snapshot,before),"Failed load partially published catalog/snapshot");
   else Need(SameCatalog(loaded,route==0?next:EngineCatalog{}) && snapshot.exists==(route==0) && snapshot.bytes==bytes && snapshot.path==fs::absolute(file).lexically_normal(),"Successful load left mismatched outputs");
   Need(fs::exists(file)==(route==0) && (route!=0 || Read(file)==bytes),"Load changed source catalog");if(position==positions)break;
  }
  std::cout<<"CATALOG_LOAD_ROUTE="<<route<<" ORDINARY_POSITIONS="<<positions<<'\n';
 }
 for(int route=0;route<4;++route){const bool production=route>=2,existing=(route%2)==0;const auto file=root/(L"route-"+std::to_wstring(route)+L".json");EngineCatalog unused;CatalogSnapshot seed;
  if(!EngineCatalogFile::Load(file,unused,seed,error) || !EngineCatalogFile::SaveChecked(file,old,seed,error))throw std::runtime_error(error);
  const auto original=Read(file);size_t positions=0;
  for(size_t position=0;;++position){
   // Only this probe-owned exact file is reset; no recursive/user-data cleanup.
   if(existing)Write(file,original);else fs::remove(file);
   g_engineFile=file;g_engineError.clear();g_engineCatalog=old;
   if(!EngineCatalogFile::Load(file,unused,g_engineSnapshot,error))throw std::runtime_error(error);
   const auto before=g_engineSnapshot;PopulateEngines();Need(SendMessageW(g_engineList,CB_GETCOUNT,0,0)==2 && SendMessageW(g_engineList,CB_GETCURSEL,0,0)==1,"Initial owned combo state failed");
   bool result=false,threw=false;tracking=true;allocations=0;failAt=position;injected=false;
   try{result=production?CommitEngineSelection(next,error):EngineCatalogFile::SaveChecked(file,next,g_engineSnapshot,error);}catch(...){threw=true;}
   tracking=false;failAt=0;
   Need(position==0 || injected,"Catalog save/commit fault position was not consumed");
   Need(!threw,"Single allocation fault escaped catalog save/commit");
   if(position==0){positions=allocations;total+=positions;Need(result && positions>0,"Successful catalog control failed");}
   const auto present=fs::exists(file);const auto disk=present?Read(file):std::string{};
   if(!result){
    Need(present==existing && (!existing || disk==original) && SameSnapshot(g_engineSnapshot,before) && SameCatalog(g_engineCatalog,old),"Reported failure changed catalog disk/snapshot/active memory");
    Need(SendMessageW(g_engineList,CB_GETCOUNT,0,0)==2 && SendMessageW(g_engineList,CB_GETCURSEL,0,0)==1,"Failed commit changed UI selection");
   }else{
    Need(present && g_engineSnapshot.exists && g_engineSnapshot.bytes==disk && SameCatalog(g_engineCatalog,production?next:old),"Successful save left disk/snapshot/memory inconsistent");
    EngineCatalog readBack;CatalogSnapshot observed;if(!EngineCatalogFile::Load(file,readBack,observed,error))throw std::runtime_error(error);Need(SameCatalog(readBack,next),"Successful catalog did not round trip");
    const auto expectedRows=production?3:2,expectedSelection=production?2:1;
    Need(SendMessageW(g_engineList,CB_GETCOUNT,0,0)==expectedRows && SendMessageW(g_engineList,CB_GETCURSEL,0,0)==expectedSelection,"Successful commit UI disagrees with catalog");
   }
   if(position==positions)break;
  }
  std::cout<<"CATALOG_PUBLICATION_ROUTE="<<route<<" ORDINARY_POSITIONS="<<positions<<'\n';
 }
 // Catalog aliases, swaps and native control refresh must not allocate after
 // successful publication; this does not inject Windows' own heap failures.
 auto a=old,b=next;auto sa=g_engineSnapshot,sb=sa;sb.bytes=std::string(1000,'x');
 const auto selected=SelectedEngineIndex(g_engineCatalog);
 tracking=true;allocations=0;failAt=1;a.Swap(b);sa.Swap(sb);PopulateEngines(selected);tracking=false;failAt=0;Need(allocations==0 && SameCatalog(a,next) && SendMessageW(g_engineList,CB_GETCURSEL,0,0)==static_cast<LRESULT>(selected),"Publication swap/control refresh allocated or lost selection");
 auto normalized=next;normalized.active=second.parent_path()/L"unused/../KuboEditor.exe";g_engineFile=root/L"normalized.json";EngineCatalog fresh;if(!EngineCatalogFile::Load(g_engineFile,fresh,g_engineSnapshot,error) || !CommitEngineSelection(normalized,error))throw std::runtime_error(error);
 Need(SendMessageW(g_engineList,CB_GETCURSEL,0,0)==2,"Prepared UI index lost lexical path equivalence");
 if(!CommitEngineSelection(g_engineCatalog,error))throw std::runtime_error(error);Need(g_engineCatalog.active==normalized.active,"Aliased catalog commit changed active path");
 // Simulate the native control refusing a row, not a real OS heap exhaustion.
 originalCombo=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_engineList,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(RefusingCombo)));Need(originalCombo!=nullptr,"Owned combo subclass failed");refuseAdd=1;
 if(!CommitEngineSelection(next,error))throw std::runtime_error(error);
 Need(SameCatalog(g_engineCatalog,next) && g_engineSnapshot.bytes==Read(g_engineFile) && !IsWindowEnabled(g_engineList) && !IsWindowEnabled(GetDlgItem(parent.value,kRemoveEngine)) && SendMessageW(g_engineList,CB_GETCOUNT,0,0)==1,"Native control refusal corrupted commit or kept stale selectable rows");
 Need(SetWindowLongPtrW(g_engineList,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(originalCombo))!=0,"Owned combo restore failed");originalCombo=nullptr;PopulateEngines();Need(IsWindowEnabled(g_engineList) && SendMessageW(g_engineList,CB_GETCURSEL,0,0)==2,"Control refresh did not recover authoritative selection");
 std::cout<<"PASS: production Hub/catalog existing+absent loads/saves, "<<total<<" ordinary allocation fault positions, failure disk/snapshot/memory/UI isolation, no-allocation publication/control refresh, Unicode/normalized/aliased selection and simulated native control refusal/recovery (not OS heap/physical/signature/install proof) retained="<<root<<'\n';return 0;
}catch(const std::exception& exception){tracking=false;std::cerr<<exception.what()<<'\n';return 1;}}
