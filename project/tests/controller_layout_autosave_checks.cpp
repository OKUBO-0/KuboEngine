#include "engine/editor/ControllerLayoutAutosave.h"
#include <iostream>
#include <stdexcept>
#include <limits>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    using Engine::Editor::ControllerLayoutAutosave;ControllerLayoutAutosave policy;
    Need(policy.Enabled() && !policy.MayFlush() && !policy.Observe(true,0,true,false) && !policy.Observe(false,100,true,false),"Unbound/stale layout allowed automatic overwrite");
    policy.Reset(true);Need(!policy.Pending() && !policy.Observe(false,1,true,false),"Passive initial frame created a write");
    Need(!policy.Observe(true,2,true,false) && !policy.Observe(false,2.74,true,false) && policy.Observe(false,2.75,true,false),"Quiet debounce boundary failed");
    Need(!policy.Observe(true,3,true,false) && !policy.Observe(true,3.6,true,false) && !policy.Observe(false,4.2,true,false),"Movement did not restart quiet timer");
    Need(!policy.Observe(false,4.4,true,true) && !policy.Observe(false,4.4,false,false) && policy.Pending(),"Gesture/dirty/playback/operation guard lost pending work");
    policy.SetEnabled(false);Need(!policy.MayFlush() && !policy.Observe(false,5,true,false) && policy.Pending(),"Disabled autosave wrote or discarded pending change");
    policy.SetEnabled(true);Need(policy.MayFlush() && policy.Observe(false,5,true,false),"Enabled clean pending layout did not resume");
    policy.Failed();Need(policy.Blocked() && !policy.MayFlush() && !policy.Observe(true,6,true,false) && !policy.Observe(false,100,true,false),"Failed save retried automatically");
    policy.Saved();Need(!policy.Blocked() && !policy.Pending() && !policy.Observe(false,101,true,false),"Explicit save did not acknowledge work");
    Need(!policy.Observe(true,102,false,false) && policy.MayFlush(),"Close flush did not retain debounced change");
    Need(!policy.Observe(false,100,true,false) && !policy.Observe(false,100.7,true,false) && policy.Observe(false,100.75,true,false),"Reversed clock prematurely published");
    policy.Reset(true);Need(!policy.Observe(true,std::numeric_limits<double>::quiet_NaN(),true,false) && !policy.Pending() && !policy.Observe(true,-1,true,false),"Invalid clock mutated state");
    policy.SetEnabled(false);policy.Reset(true);Need(!policy.Enabled() && !policy.Pending(),"New owner reset forgot explicit disable");
    policy.SetEnabled(true);policy.Reset(false);Need(!policy.Observe(true,1,true,false) && !policy.Observe(false,1000,true,false),"Stale graph binding auto overwritten");
    policy.Reset(true);Need(!policy.Observe(true,1,true,false) && policy.Observe(false,2,true,false),"Explicit reload did not unblock pending layout");
    std::cout<<"PASS: layout debounce/quiet restart/interaction+dirty+operation gates/disable+reenable/failed retry suppression/explicit Save+Reload reset/close flush readiness/stale binding/new owner/invalid+reversed time; policy only\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
