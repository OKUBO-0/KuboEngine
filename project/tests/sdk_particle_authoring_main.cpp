#include "player_particle_fixture.h"
#include <iostream>
#include <exception>
#include <string>
#include <stdexcept>
#include <crtdbg.h>
#include <cstdlib>
int main(int argc,char** argv){
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
    try {
        const bool deployed=argc==2&&std::string(argv[1])=="--particle-export";
        if(argc!=1&&!deployed)throw std::runtime_error("Unsupported SDK Particle authoring mode");
        std::cout<<"RUNNER_MODE="<<(deployed?"--particle-export":"--particles")<<'\n';RunPlayerParticleChecks(deployed);
        std::cout<<"PASS: SDK-only authored Particle RuntimeGame startup fixture\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
