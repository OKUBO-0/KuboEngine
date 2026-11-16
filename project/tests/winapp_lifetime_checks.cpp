#include "../engine/base/core/WinApp.h"
#include <iostream>
#include <stdexcept>
LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM){return 0;}
void Require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(){try{
 const HRESULT owned=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Require(SUCCEEDED(owned),"Own STA initialization failed");
 for(int mode=0;mode<3;++mode){
  {Engine::Base::WinApp app;
   if(mode!=0){bool failed=false;try{app.Initialize();}catch(const std::exception&){failed=true;}Require(failed && !app.GetHwnd(),"MTA should fail before window creation on caller STA");}
   if(mode!=2){app.Finalize();app.Finalize();}
  } // Destructor-only cleanup is mode2; modes0/1 additionally test repeated Finalize.
  const auto retained=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Require(retained==S_FALSE,"WinApp consumed caller STA ownership");CoUninitialize();
 }
 CoUninitialize();const auto released=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Require(released==S_OK,"WinApp leaked COM initialization ownership");CoUninitialize();
 std::cout<<"PASS: WinApp never-initialized/failed-MTA/repeated Finalize/destructor preserve caller STA, no window created and no leaked COM ownership\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
