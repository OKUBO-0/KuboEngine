#include "engine/base/render/ViewExtent.h"
#include "engine/base/render/ViewResizeSchedule.h"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace { void Require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); } }
int main() {
    using namespace Engine::Base;
    try {
        ViewExtent extent;
        Require(ValidGameOutputExtent({}) && ValidGameOutputExtent({3840,2160}) && ValidGameOutputExtent({1,4096}),"Valid Game output rejected");
        Require(!ValidGameOutputExtent({0,720}) && !ValidGameOutputExtent({4097,1}) && !ValidGameOutputExtent({4096,4096}),"Invalid/excessive Game output accepted");
        Require(GamePresentationRect({1280,720},{1280,720})==ViewRect{0,0,1280,720},"Default presentation changed");
        Require(GamePresentationRect({1280,720},{512,768})==ViewRect{400,0,480,720},"Portrait presentation was stretched");
        Require(GamePresentationRect({1280,720},{1024,256})==ViewRect{0,200,1280,320},"Wide presentation was stretched");
        Require(GamePresentationRect({0,0},{512,768})==ViewRect{},"Invalid presentation generated a viewport");
        Require(PanelViewExtent(430.9f,250.8f,extent) && extent==ViewExtent{430,250},"Pixel extent rounding failed");
        Require(PanelViewExtent(400,800,extent) && extent.Aspect()==0.5f,"Portrait aspect failed");
        Require(PanelViewExtent(8192,4096,extent) && extent==ViewExtent{2048,1024},"Uniform resource bound failed");
        Require(PanelViewExtent((std::numeric_limits<float>::max)(),(std::numeric_limits<float>::max)(),extent) && extent==ViewExtent{2048,2048},"Large finite input overflowed");
        Require(PanelViewExtent(1,1000000,extent) && extent==ViewExtent{1,2048},"Thin panel pixel bound failed");
        const auto before=extent;
        for (float invalid : {0.0f,-1.0f,0.5f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
            Require(!PanelViewExtent(invalid,100,extent) && extent==before,"Invalid width mutated extent");
            Require(!PanelViewExtent(100,invalid,extent) && extent==before,"Invalid height mutated extent");
        }
        ViewResizeSchedule schedule;
        const ViewExtent current{1280,720};
        schedule.Observe(400,300,current);
        Require(!schedule.Advance(0.1f,current),"Stable resize was not debounced");
        schedule.Observe(400,300,current);
        Require(!schedule.Advance(0.05f,current),"Initial change interval counted as quiet time");
        Require(schedule.Advance(0.1f,current)==ViewExtent{400,300},"Stable size did not settle");
        schedule.Applied(); Require(!schedule.Advance(10,current),"Applied resize repeated");
        for (int frame=0; frame<5; ++frame) {
            schedule.Observe(float(500+frame),300,current);
            const auto next=schedule.Advance(0.1f,current);
            Require(frame==4 ? next==ViewExtent{504,300} : !next,"Continuous drag refresh timing failed");
        }
        schedule.Applied(); schedule.Observe(600,300,current);
        Require(!schedule.Advance(std::numeric_limits<float>::quiet_NaN(),current) &&
            !schedule.Advance(-1,current) && !schedule.Advance(0,current),"Invalid clock advanced schedule");
        schedule.Observe(0,300,current); Require(!schedule.Advance(1,current),"Minimized panel retained pending resize");
        schedule.Observe(600,300,current); schedule.Cancel(); Require(!schedule.Advance(1,current),"Hidden panel retained pending resize");
        schedule.Observe(600,300,current); schedule.Observe(1280,720,current);
        Require(!schedule.Advance(1,current),"Return to current extent did not cancel resize");
        schedule.Observe(600,300,current); Require(!schedule.Advance(0.1f,current),"New request inherited old elapsed time");
        Require(!schedule.Advance(1,ViewExtent{600,300}),"Externally applied size was requested again");
        // Uniformly clamped observations share a request, rather than resetting
        // quiet time for every raw pixel change above the allocation bound.
        schedule.Observe(4096,2048,current); Require(!schedule.Advance(0.1f,current),"Large request was not debounced");
        schedule.Observe(8192,4096,current); Require(schedule.Advance(0.15f,current)==ViewExtent{2048,1024},"Equivalent bounded sizes restarted debounce");
        schedule.Applied();
        for (int frame=0; frame<3; ++frame) {
            schedule.Observe(float(700+frame),300,current);
            const auto next=schedule.Advance(0.2f,current);
            Require(frame==2 ? next==ViewExtent{702,300} : !next,"Slow dragging was mistaken for a settled size");
        }
        std::cout << "PASS: panel extent rounding, aspect, uniform allocation bounds and invalid/minimized isolation\n";
        std::cout << "PASS: stable/continuous resize coalescing, latest request, invalid clock, applied/no-op and hidden/minimized cancellation\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
