#include "engine/script/ScriptSceneCommands.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
#include <cstring>
#include <winioctl.h>
namespace {bool tracking=false;size_t allocations=0,failAt=0;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Fixture write");}}
namespace {
void Junction(const std::filesystem::path& link,const std::filesystem::path& target){
 Require(link.is_absolute() && target.is_absolute() && link.parent_path().parent_path()==target.parent_path(),"Junction must stay in owned fixture");
 Require(CreateDirectoryW(link.c_str(),nullptr)!=0,"Create junction directory");
 const auto substitute=L"\\??\\"+target.native(),display=target.native();
 struct MountPoint {DWORD tag;WORD length,reserved,subOffset,subLength,printOffset,printLength;wchar_t paths[1];};
 const size_t pathBytes=(substitute.size()+1+display.size()+1)*sizeof(wchar_t);
 std::vector<unsigned char> bytes(offsetof(MountPoint,paths)+pathBytes,0);auto* data=reinterpret_cast<MountPoint*>(bytes.data());
 data->tag=IO_REPARSE_TAG_MOUNT_POINT;data->length=WORD(8+pathBytes);data->subLength=WORD(substitute.size()*sizeof(wchar_t));data->printOffset=WORD((substitute.size()+1)*sizeof(wchar_t));data->printLength=WORD(display.size()*sizeof(wchar_t));
 std::memcpy(data->paths,substitute.c_str(),(substitute.size()+1)*sizeof(wchar_t));std::memcpy(reinterpret_cast<unsigned char*>(data->paths)+data->printOffset,display.c_str(),(display.size()+1)*sizeof(wchar_t));
 HANDLE handle=CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);Require(handle!=INVALID_HANDLE_VALUE,"Open junction directory");DWORD returned=0;
 const bool created=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,data,DWORD(bytes.size()),nullptr,0,&returned,nullptr)!=0;CloseHandle(handle);Require(created,"Set junction reparse point");
 Require((GetFileAttributesW(link.c_str())&FILE_ATTRIBUTE_REPARSE_POINT)!=0,"Junction attribute missing");
}
}
void* operator new(size_t n){if(tracking && ++allocations==failAt)throw std::bad_alloc();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}void operator delete[](void* p,size_t) noexcept{std::free(p);}
int main(){using namespace Engine;using namespace Scripting;using R=ScriptSceneCommands::Result;namespace fs=std::filesystem;
 const auto previous=fs::current_path();const auto root=fs::temp_directory_path()/(L"kubo-script-model-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 try{
  fs::create_directories(root/L"Assets/日本語");fs::create_directories(root/"Unrelated/Assets");
  const auto model=root/L"Assets/日本語/モデル.obj";Write(model,"registered model fixture, not GPU/import proof");Write(root/"Assets/wrong.lua","function on_update(e,dt) end");
  Assets::AssetRegistry registry;std::string error;Require(registry.Scan(root/"Assets",error),error.c_str());const auto guid=registry.FindByPath(model)->guid;const auto wrong=registry.FindByPath(root/"Assets/wrong.lua")->guid;
  ScriptAssetView assets(registry,root);Scene::ModelComponent admitted;Require(assets.Model("Assets/日本語/モデル.obj","",admitted)==ScriptAssetView::Result::Success && admitted.assetGuid==guid,"Path did not pin modern identity");
  Require(assets.Model("Assets/stale.obj",guid,admitted)==ScriptAssetView::Result::Success && admitted.assetPath=="Assets/日本語/モデル.obj","GUID did not override old relative hint");
  bool rejected=false;try{ScriptAssetView bad(registry,{});}catch(const std::invalid_argument&){rejected=true;}Require(rejected,"Implicit cwd owner allowed");
  Scene::EntityWorld world;world.CreateEntity("Owner");auto self=*world.Find(1);ScriptSceneView view;ScriptSceneCommands commands;Scene::EntityId born=777;
  {ScriptSceneView::WriteScope scope(view,world);commands.Begin(&view,self);Require(commands.SpawnModel("Model","Assets/日本語/モデル.obj","",0,born)==R::Unavailable && born==777,"No asset owner admitted");}
  fs::current_path(root/"Unrelated");
  {ScriptSceneView::WriteScope scope(view,world,&assets);commands.Begin(&view,self);Require(commands.Apply(1,ScriptSceneCommands::Operation::Translate,{2,0,0})==R::Success && commands.SpawnModel("生成","Assets/日本語/モデル.obj","",1,born)==R::Success && born==2 && commands.CreatedCount()==1 && commands.OperationCount()==2,"Model generation admission");
   Require(!world.Find(2) && commands.ReadWorld()->Find(2)->model.assetGuid==guid && commands.Prepare(),"Model leaked prepublication");allocations=0;failAt=1;tracking=true;const bool published=commands.PublishPrepared();tracking=false;failAt=0;Require(published && allocations==0 && world.Find(2)->hasModel && world.Find(2)->parent==1,"Model publication allocated/lost component");}
  const auto snapshot=world.Capture().dump();const auto high=world.AllocationHighWatermark();
  for(const auto& hint:std::vector<std::string>{"../outside.obj","Assets/../outside.obj","C:/outside.obj","/outside.obj","Assets/x:y.obj",std::string("Assets/a\0b.obj",14),std::string(1,char(255)),"Assets/wrong.lua","Assets/missing.obj"}){
   self=*world.Find(1);ScriptSceneView::WriteScope scope(view,world,&assets);commands.Begin(&view,self);Require(commands.Apply(1,ScriptSceneCommands::Operation::Translate,{5,0,0})==R::Success,"Failure prefix");born=777;
   Require(commands.SpawnModel("Rejected",hint,"",0,born)!=R::Success && born==777 && !commands.Prepare() && !commands.PublishPrepared() && world.Capture().dump()==snapshot && world.AllocationHighWatermark()==high,"Invalid asset admission leaked prefix/identity");
  }
  Require(assets.Model("",wrong,admitted)==ScriptAssetView::Result::Invalid && assets.Model("",std::string(32,'f'),admitted)==ScriptAssetView::Result::Missing,"GUID type/missing refusal");
  const auto admittedPath=admitted.assetPath,admittedGuid=admitted.assetGuid;
  Require(assets.Model("../escape.obj","",admitted)==ScriptAssetView::Result::Invalid && admitted.assetPath==admittedPath && admitted.assetGuid==admittedGuid,"Rejected admission modified output component");
  {ScriptSceneView::WriteScope scope(view,world,&assets);self=*world.Find(1);commands.Begin(&view,self);born=777;
   Require(commands.SpawnModel("Missing parent","Assets/日本語/モデル.obj","",999,born)==R::Missing && born==777 && commands.OperationCount()==0,"Missing Model parent consumed ID");
   for(int i=0;i<64;++i)Require(commands.SpawnModel("Bounded","Assets/日本語/モデル.obj","",0,born)==R::Success,"Model creation cap early");
   Require(commands.CreatedCount()==64 && commands.SpawnModel("Extra","Assets/日本語/モデル.obj","",0,born)==R::Limit,"Model creation cap bypass");}
  fs::create_directories(root/"Outside");Write(root/"Outside/out.obj","outside registry");Assets::AssetRegistry other;Require(other.Scan(root/"Outside",error),error.c_str());ScriptAssetView outside(other,root/"Assets");
  Require(outside.Model("",other.FindByPath(root/"Outside/out.obj")->guid,admitted)==ScriptAssetView::Result::Invalid,"GUID outside project owner admitted");
  {ScriptSceneView::WriteScope outer(view,world,&assets);self=*world.Find(1);commands.Begin(&view,self);{ScriptSceneView::WriteScope inner(view,world);born=777;Require(commands.SpawnModel("Nested","Assets/日本語/モデル.obj","",0,born)==R::Unavailable,"Expired token used asset owner");}Require(commands.SpawnModel("Restored","Assets/日本語/モデル.obj","",0,born)==R::Success,"Nested asset owner not restored");}
  size_t positions=0;
  for(size_t position=0;;++position){auto live=world;auto record=*live.Find(1);ScriptSceneView local;ScriptSceneView::WriteScope scope(local,live,&assets);ScriptSceneCommands batch;batch.Begin(&local,record);Require(batch.Apply(1,ScriptSceneCommands::Operation::Translate,{7,0,0})==R::Success,"Fault prefix");born=777;
   allocations=0;failAt=position;tracking=true;const auto result=batch.SpawnModel("Allocated model","Assets/日本語/モデル.obj","",0,born);const bool prepared=result==R::Success && batch.Prepare();tracking=false;failAt=0;
   if(position==0){positions=allocations;Require(prepared && positions>0,"Allocation baseline");}else {
    const bool retry=batch.Prepare(),published=batch.PublishPrepared();
    if(prepared || retry || published || (result!=R::Success && born!=777)){
     std::cerr<<"Fault position="<<position<<" result="<<int(result)<<" born="<<born<<" prepared="<<prepared<<" retry="<<retry<<" published="<<published<<'\n';
     Require(false,"Faulted admission/promotion published");
    }
   }
   Require(live.Capture().dump()==snapshot && live.AllocationHighWatermark()==high,"Model allocation fault changed live World");if(position==positions)break;
  }
  const auto moved=root/L"Assets/日本語/移動.obj";fs::rename(model,moved);fs::rename(Assets::AssetRegistry::MetadataPath(model),Assets::AssetRegistry::MetadataPath(moved));Write(model,"replacement");
  Require(assets.Model("Assets/日本語/モデル.obj",guid,admitted)!=ScriptAssetView::Result::Success,"Stale registry substituted replacement");Require(registry.Scan(root/"Assets",error),error.c_str());Require(assets.Model("Assets/日本語/モデル.obj",guid,admitted)==ScriptAssetView::Result::Success && admitted.assetPath=="Assets/日本語/移動.obj","Rescan GUID move");
  Write(Assets::AssetRegistry::MetadataPath(moved),"{\"version\":1,\"guid\":\"ffffffffffffffffffffffffffffffff\"}");Require(assets.Model("",guid,admitted)!=ScriptAssetView::Result::Success,"Changed identity admitted");
  fs::create_directories(root/"Legacy");Write(root/"Legacy/legacy.obj","no metadata");Assets::AssetRegistry legacy;Require(legacy.Scan(root/"Legacy",error,Assets::AssetRegistry::MetadataPolicy::ReadOnly),error.c_str());ScriptAssetView legacyAssets(legacy,root);
  Require(legacyAssets.Model("Legacy/legacy.obj","",admitted)==ScriptAssetView::Result::Success && admitted.assetGuid.empty() && !fs::exists(Assets::AssetRegistry::MetadataPath(root/"Legacy/legacy.obj")),"ReadOnly legacy path created/pinned missing metadata");
  const auto linkParent=root/"Assets/LinkedParent",linkSource=linkParent/"linked.obj",target=root/"LinkedTarget";fs::create_directories(linkParent);Write(linkSource,"junction fixture");Assets::AssetRegistry linked;Require(linked.Scan(root/"Assets",error),error.c_str());const auto linkedGuid=linked.FindByPath(linkSource)->guid;ScriptAssetView linkedAssets(linked,root);
  Require(linkParent.is_absolute() && target.is_absolute() && linkParent.parent_path().parent_path()==root && target.parent_path()==root,"Move targets escaped own fixture");fs::rename(linkParent,target);Junction(linkParent,target);
  const auto linkedPathResult=linkedAssets.Model("Assets/LinkedParent/linked.obj","",admitted),linkedGuidResult=linkedAssets.Model("",linkedGuid,admitted);
  Require(fs::is_regular_file(linkSource) && linkedPathResult!=ScriptAssetView::Result::Success && linkedGuidResult==ScriptAssetView::Result::Invalid,"Actual linked ancestor accepted valid file/identity");
  std::cout<<"LINKED_ANCESTOR_PATH_RESULT="<<int(linkedPathResult)<<" GUID_RESULT="<<int(linkedGuidResult)<<'\n';
  fs::current_path(previous);std::cout<<"PASS: scoped native Model generation, read-only typed asset owner/Unicode/cwd, pinned GUID move/stale/type/identity/path refusal, nested/expired scope, atomic no-allocation publication and "<<positions<<" ordinary allocation failures (no Lua/host/GPU integration)\nRetained fixture: "<<root.string()<<'\n';return 0;
 }catch(const std::exception& e){tracking=false;fs::current_path(previous);std::cerr<<"FAIL: "<<e.what()<<"; fixture="<<root.string()<<'\n';return 1;}}
