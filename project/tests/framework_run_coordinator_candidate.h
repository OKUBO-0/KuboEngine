#pragma once
#include "framework_run_failure_candidate.h"
#include <cstdlib>
#include <exception>
#include <type_traits>

// Private policy prototype. Never propagate cleanup refusal into destruction of
// a caller-owned complete host. Fatal must stop the owned process without unwind.
template<class Operation,class Cleanup,class Fatal>
void RunWithRetainedShutdownCandidate(Operation&& operate,Cleanup&& finalize,Fatal&& fatal){
    static_assert(std::is_nothrow_invocable_v<Fatal&,const std::exception_ptr&,const std::exception_ptr&,unsigned>);
    std::exception_ptr operation,cleanup;
    try{operate();}catch(...){operation=std::current_exception();}
    bool completed=false;
    for(unsigned attempt=1;attempt<=3;++attempt){
        try{finalize();completed=true;break;}
        catch(...){if(!cleanup)cleanup=std::current_exception();}
    }
    if(!completed){fatal(operation,cleanup,3);std::_Exit(75);}
    if(operation && cleanup)throw FrameworkRunFailure(operation,cleanup);
    if(cleanup)std::rethrow_exception(cleanup);
    if(operation)std::rethrow_exception(operation);
}
