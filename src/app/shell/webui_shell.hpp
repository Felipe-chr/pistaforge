#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>

namespace pf {

class Dispatcher;

/// Adaptador fino sobre o FastWebUI. Mantém a API do WebUI fora do resto do app.
class WebUiShell {
public:
    explicit WebUiShell(std::filesystem::path executable_dir);
    ~WebUiShell();

    WebUiShell(const WebUiShell&) = delete;
    WebUiShell& operator=(const WebUiShell&) = delete;
    WebUiShell(WebUiShell&&) = delete;
    WebUiShell& operator=(WebUiShell&&) = delete;

    void bind(Dispatcher& dispatcher);
    [[nodiscard]] bool set_port(std::size_t port);
    [[nodiscard]] std::size_t port() const;
    [[nodiscard]] bool show();
    void wait();

    [[nodiscard]] const std::filesystem::path& web_root() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pf
