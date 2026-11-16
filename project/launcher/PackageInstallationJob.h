#pragma once
#include "EnginePackageInstallation.h"
#include <future>
namespace KuboHub {
struct PackageInstallationOutcome {
    EnginePackageInstallation::Result result=EnginePackageInstallation::Result::Invalid;
    InstalledPackageReport report;std::string error;
};
class PackageInstallationJob final {
    std::atomic_bool cancel_{false};std::atomic<uint64_t> progress_{0};
    std::future<PackageInstallationOutcome> future_;
    bool Start(const std::filesystem::path& manifest,const std::filesystem::path& store,const std::string& token,bool recover){
        if(Active())return false;
        auto source=manifest,destination=store;auto identity=token;cancel_.store(false);progress_.store(0);
        future_=std::async(std::launch::async,[this,source=std::move(source),destination=std::move(destination),identity=std::move(identity),recover]{
            PackageInstallationOutcome outcome;
            outcome.result=recover?EnginePackageInstallation::Recover(destination,identity,outcome.report,outcome.error,&cancel_,&progress_):
                EnginePackageInstallation::Install(source,destination,outcome.report,outcome.error,&cancel_,&progress_);
            return outcome;
        });return true;
    }
public:
    PackageInstallationJob()=default;PackageInstallationJob(const PackageInstallationJob&)=delete;
    PackageInstallationJob& operator=(const PackageInstallationJob&)=delete;
    ~PackageInstallationJob(){Cancel();if(future_.valid())future_.wait();}
    bool Active() const noexcept{return future_.valid();}
    bool Ready() const {return Active() && future_.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;}
    uint64_t Progress() const noexcept{return progress_.load(std::memory_order_relaxed);}
    void Cancel() noexcept{cancel_.store(true,std::memory_order_relaxed);}
    bool StartInstall(const std::filesystem::path& manifest,const std::filesystem::path& store){return Start(manifest,store,{},false);}
    bool StartRecovery(const std::filesystem::path& store,const std::string& token){return Start({},store,token,true);}
    PackageInstallationOutcome Take(){if(!Ready())throw std::logic_error("Installation job is not ready");return future_.get();}
};
}
