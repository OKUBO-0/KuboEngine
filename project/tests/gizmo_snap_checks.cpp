#include "engine/editor/GizmoSnapSettings.h"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace { void Require(bool ok,const char* why) { if (!ok) throw std::runtime_error(why); } }
int main() {
    using namespace Engine::Editor;
    try {
        GizmoSnapSettings settings;
        std::array<float,3> result{4,5,6}; std::string error;
        Require(settings.Resolve(0,result,error) && result==std::array<float,3>{0,0,0},"Disabled snap not cleared");
        settings.enabled=true;
        for (int op=0;op<3;++op) {
            const float expected=op==0 ? 1.0f : op==1 ? 15.0f : 0.1f;
            Require(settings.Resolve(op,result,error) && result==std::array<float,3>{expected,expected,expected},"Snap unit/default mapping failed");
            Require(ValidGizmoSnapStep(op,0.0001f),"Minimum valid snap rejected");
            Require(ValidGizmoSnapStep(op,op==0 ? 1000000.0f : op==1 ? 360.0f : 100.0f),"Maximum valid snap rejected");
            for (float invalid : {0.0f,-1.0f,0.00001f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
                Require(!ValidGizmoSnapStep(op,invalid),"Invalid snap accepted");
        }
        settings.degrees=361; result={7,8,9}; const auto before=result;
        Require(!settings.Resolve(1,result,error) && result==before && !error.empty(),"Invalid angle mutated output");
        Require(settings.Resolve(0,result,error) && error.empty(),"Dormant invalid setting blocked other operation");
        settings.scaleRatio=101; Require(!settings.Resolve(2,result,error),"Excess scale step accepted");
        settings.distance=1000001; Require(!settings.Resolve(0,result,error),"Excess distance step accepted");
        settings.enabled=false; Require(settings.Resolve(2,result,error) && result==std::array<float,3>{},"Disabled snap rejected dormant values");
        result=before; Require(!settings.Resolve(3,result,error) && result==before && !ValidGizmoSnapStep(-1,1),"Unknown operation accepted/mutated output");
        std::cout << "PASS: gizmo snap units/defaults, enabled/disabled selection, finite bounds and invalid/dormant isolation\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
