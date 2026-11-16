#include "engine/physics/PhysicsWorld.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    using namespace Engine::Scene;using namespace Engine::Physics;
    try{
        EntityWorld seed;const auto body=seed.CreateEntity("Fixed falling body");
        seed.Find(body)->transform.position.y=10;seed.SetRigidBody(body,{{1,0,0},1,true,true,false});
        auto sixty=seed,quarter=seed,grouped=seed;PhysicsWorld a,b,c;
        for(int frame=0;frame<60;++frame)a.Advance(sixty,1.0f/60);
        for(int frame=0;frame<240;++frame)b.Advance(quarter,1.0f/240);
        for(int frame=0;frame<10;++frame)c.Advance(grouped,0.1f);
        Require(sixty.Capture().dump()==quarter.Capture().dump() && sixty.Capture().dump()==grouped.Capture().dump(),"Fixed physics changed with equal-time frame partitions");
        Require(std::abs(sixty.Find(body)->transform.position.x-1)<1e-5 && sixty.Find(body)->rigidBody.velocity.y< -9.79f,"Fixed scheduler failed to integrate one second");
        EntityWorld waiting=seed;PhysicsWorld clock;const auto before=waiting.Capture().dump();
        clock.Advance(waiting,1.0f/240);const auto fraction=clock.GetAdvanceUsage().remainingSeconds;
        Require(clock.GetAdvanceUsage().steps==0 && fraction>0 && waiting.Capture().dump()==before && clock.GetEvents().empty(),"Fractional frame integrated early");
        for(const float invalid:{0.0f,-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
            clock.Advance(waiting,invalid);Require(waiting.Capture().dump()==before && clock.GetAdvanceUsage().steps==0 && clock.GetAdvanceUsage().remainingSeconds==fraction,"Invalid/paused frame consumed clock or modified World");
        }
        clock.ResetSimulation();Require(clock.GetAdvanceUsage().remainingSeconds==0 && clock.GetEvents().empty(),"Simulation reset retained clock");
        clock.Advance(waiting,10);Require(clock.GetAdvanceUsage().steps==8 && std::abs(clock.GetAdvanceUsage().consumedSeconds-8.0/60)<1e-12 && std::abs(clock.GetAdvanceUsage().droppedSeconds-(10-8.0/60))<1e-8 && clock.GetAdvanceUsage().remainingSeconds<1.0/60,"Hitch exceeded bounded steps or accumulated catch-up debt");
        clock.Advance(waiting,1.0f/240);Require(clock.GetAdvanceUsage().steps==0 && clock.GetAdvanceUsage().droppedSeconds==0,"Hitch left whole-tick backlog");
        clock.Advance(waiting,(std::numeric_limits<float>::max)());Require(clock.GetAdvanceUsage().steps==8 && std::isfinite(clock.GetAdvanceUsage().droppedSeconds),"Extreme finite dt overflowed clock");
        EntityWorld contacts;for(int n=0;n<130;++n){const auto id=contacts.CreateEntity("Trigger");contacts.SetCollider(id,{{0.5f,0.5f,0.5f},true});}
        PhysicsWorld triggers;triggers.Advance(contacts,8.0f/60);
        const auto total=size_t{130}*129/2*8;
        Require(triggers.GetAdvanceUsage().steps==8 && triggers.GetEvents().size()==PhysicsWorld::MaximumFrameEvents && triggers.GetAdvanceUsage().discardedEvents==total-PhysicsWorld::MaximumFrameEvents,"Substep events not aggregated or bounded");
        const auto& first=triggers.GetEvents().front();Require(first.first==1 && first.second==2 && first.trigger,"Substep event order changed");
        triggers.Advance(contacts,1.0f/240);Require(triggers.GetEvents().empty() && triggers.GetAdvanceUsage().discardedEvents==0,"No-tick frame replayed previous collision events");
        EntityWorld invalidHierarchy;const auto parent=invalidHierarchy.CreateEntity("Singular"),child=invalidHierarchy.CreateEntity("Child",parent);invalidHierarchy.Find(parent)->transform.scale.x=0;invalidHierarchy.SetRigidBody(child,{});
        PhysicsWorld diagnostics;diagnostics.Advance(invalidHierarchy,0.1f);Require(!diagnostics.GetDiagnostic().empty(),"Fixed scheduler lost Step diagnostic");
        const auto diagnostic=diagnostics.GetDiagnostic();diagnostics.Advance(invalidHierarchy,1.0f/240);Require(diagnostics.GetAdvanceUsage().steps==0 && diagnostics.GetDiagnostic()==diagnostic,"No-tick frame lost persistent diagnostic/dedup state");
        std::cout<<"PASS: fixed physics frame partitions, fractional/invalid/reset time, bounded hitch/drop recovery, aggregate event cap/order and diagnostic retention\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
