#pragma once

#include <string>
#include <utility>

namespace usm {

class Result final {
public:
    [[nodiscard]] static Result success() { return Result(true, {}); }
    [[nodiscard]] static Result failure(std::string message) {
        return Result(false, std::move(message));
    }

    [[nodiscard]] explicit operator bool() const noexcept { return succeeded_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

private:
    Result(bool succeeded, std::string message)
        : succeeded_(succeeded), message_(std::move(message)) {}

    bool succeeded_{};
    std::string message_;
};

} // namespace usm

