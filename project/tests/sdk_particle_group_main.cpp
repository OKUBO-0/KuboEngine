#include "particle_group_fixture.h"
#include <iostream>
#include <exception>
#include <crtdbg.h>
#include <cstdlib>
int main(){
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
    try{RunParticleGroupChecks();std::cout<<"PASS: SDK-only Particle group ownership/multi-view fixture\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
