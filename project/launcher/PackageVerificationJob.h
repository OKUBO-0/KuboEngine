#pragma once
#include "EnginePackageVerification.h"
#include <future>

namespace KuboHub {
struct PackageVerificationOutcome {
    EnginePackageVerification::Result result=EnginePackageVerification::Result::Invalid;
    PackageVerificationReport report;std::string error;
};
// One owned worker. It never touches HWNDs, catalogs, projects or package bytes.
class PackageVerificationJob final {
    std::atomic_bool cancel_{false};std::atomic<uint64_t> progress_{0};
    std::future<PackageVerificationOutcome> future_;
public:
    PackageVerificationJob()=default;
    PackageVerificationJob(const PackageVerificationJob&)=delete;
    PackageVerificationJob& operator=(const PackageVerificationJob&)=delete;
    ~PackageVerificationJob(){Cancel();if(future_.valid())future_.wait();}
    bool Active() const noexcept{return future_.valid();}
    bool Ready() const {return Active() && future_.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;}
    uint64_t Progress() const noexcept{return progress_.load(std::memory_order_relaxed);}
    void Cancel() noexcept {cancel_.store(true,std::memory_order_relaxed);}
    bool Start(const std::filesystem::path& manifest){
        if(Active())return false;
        auto path=manifest;cancel_.store(false,std::memory_order_relaxed);progress_.store(0,std::memory_order_relaxed);
        future_=std::async(std::launch::async,[this,path=std::move(path)]{
            PackageVerificationOutcome outcome;
            outcome.result=EnginePackageVerification::Verify(path,outcome.report,outcome.error,{},&cancel_,&progress_);
            return outcome;
        });
        return true;
    }
    PackageVerificationOutcome Take(){if(!Ready())throw std::logic_error("Package job is not ready");return future_.get();}
};
}
