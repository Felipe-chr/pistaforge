#pragma once

#include <exception>
#include <string>
#include <utility>

namespace pf {

/// Erro de API com código estável consumido pelo frontend.
class ApiError : public std::exception {
public:
    ApiError(std::string code, std::string message)
        : code_(std::move(code)), message_(std::move(message)) {}

    [[nodiscard]] const std::string& code() const noexcept { return code_; }
    [[nodiscard]] const char* what() const noexcept override { return message_.c_str(); }

private:
    std::string code_;
    std::string message_;
};

} // namespace pf
