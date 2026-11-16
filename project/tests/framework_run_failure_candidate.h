#pragma once
#include <exception>
#include <utility>

// Private candidate protocol; NOT an adopted Runtime API or ownership policy.
class FrameworkRunFailure final : public std::exception {
public:
    FrameworkRunFailure(std::exception_ptr operation,std::exception_ptr cleanup) noexcept
        :operation_(std::move(operation)),cleanup_(std::move(cleanup)){}
    const char* what() const noexcept override {return "Framework operation and shutdown both failed";}
    const std::exception_ptr& Operation() const noexcept {return operation_;}
    const std::exception_ptr& Cleanup() const noexcept {return cleanup_;}
private:
    std::exception_ptr operation_,cleanup_;
};
