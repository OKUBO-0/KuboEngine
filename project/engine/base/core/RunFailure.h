#pragma once
#include <exception>
#include <utility>

namespace Engine::Base {
// Propagated only after successful shutdown. Both original errors stay available.
class RunFailure final : public std::exception {
public:
    RunFailure(std::exception_ptr operation, std::exception_ptr cleanup) noexcept
        : operation_(std::move(operation)), cleanup_(std::move(cleanup)) {}
    const char* what() const noexcept override { return "Engine operation and shutdown both failed"; }
    const std::exception_ptr& Operation() const noexcept { return operation_; }
    const std::exception_ptr& Cleanup() const noexcept { return cleanup_; }
private:
    std::exception_ptr operation_, cleanup_;
};
}
