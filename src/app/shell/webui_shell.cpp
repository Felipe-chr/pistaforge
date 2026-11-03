#include "app/shell/webui_shell.hpp"

#include <iostream>
#include <string>
#include <system_error>
#include <utility>

#include <webui.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

#include "app/bridge/dispatcher.hpp"

namespace pf {
namespace {

/// Caminho curto 8.3, usado apenas como último recurso.
std::filesystem::path short_path(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    const DWORD needed = GetShortPathNameW(wide.c_str(), nullptr, 0);
    if (needed == 0) return path;

    std::wstring buffer(needed, L'\0');
    const DWORD written = GetShortPathNameW(wide.c_str(), buffer.data(), needed);
    if (written == 0 || written >= needed) return path;

    buffer.resize(written);
    return std::filesystem::path{buffer};
#else
    return path;
#endif
}

/// Pasta de dados locais do usuário (sem acentos), quando disponível.
std::filesystem::path local_app_data_dir() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) return std::filesystem::path{buffer};
#endif
    return {};
}

/// O servidor do WebUI usa caminhos estreitos no code page ANSI do sistema e o
/// CivetWeb falha ao abrir arquivos por caminhos com acentos ou nomes 8.3 com
/// `~`. Por isso os assets são servidos a partir de uma pasta ASCII estável.
std::filesystem::path ascii_serving_root(const std::filesystem::path& source) {
    std::error_code error;

    std::filesystem::path base = local_app_data_dir();
    if (base.empty()) base = std::filesystem::temp_directory_path(error);
    if (error || base.empty()) return short_path(source);

    const std::filesystem::path target = base / "PistaForge" / "web";

    bool same = false;
    if (std::filesystem::exists(target, error) && !error) {
        same = std::filesystem::equivalent(source, target, error) && !error;
    }
    if (same) return target;

    error.clear();
    std::filesystem::remove_all(target, error);
    error.clear();
    std::filesystem::create_directories(target.parent_path(), error);
    error.clear();
    std::filesystem::copy(source, target,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          error);
    if (error) {
        std::cerr << "[webui] falha ao copiar assets para " << target << ": " << error.message()
                  << '\n';
        return short_path(source);
    }
    return target;
}

} // namespace

struct WebUiShell::Impl {
    webui::window window;
    std::filesystem::path web_root;
    std::filesystem::path served_root;
};

WebUiShell::WebUiShell(std::filesystem::path executable_dir)
    : impl_(std::make_unique<Impl>()) {
    // O modo cliente-único bloqueia subrecursos (app.js, styles.css) quando o
    // navegador não devolve o cookie `webui_auth`. Como o app é local e de
    // janela única, permitir múltiplos clientes é seguro e evita o 403.
    webui::set_config(multi_client, true);

    // Não bloquear a inicialização esperando o WebSocket: a UI mostra o estado
    // real da ponte e o app continua vivo mesmo se o navegador demorar.
    webui::set_config(show_wait_connection, false);

    impl_->web_root = executable_dir / "web";
    if (!std::filesystem::exists(impl_->web_root / "index.html")) {
#ifdef PF_SOURCE_WEB_DIR
        impl_->web_root = std::filesystem::path{PF_SOURCE_WEB_DIR};
#endif
    }

    impl_->served_root = ascii_serving_root(impl_->web_root);

    impl_->window.set_size(1440, 900);
    if (!impl_->window.set_root_folder(impl_->served_root.string())) {
        std::cerr << "[webui] não foi possível definir a pasta web: " << impl_->served_root
                  << '\n';
    } else {
        std::cout << "[webui] assets: " << impl_->served_root << '\n';
    }
}

WebUiShell::~WebUiShell() = default;

void WebUiShell::bind(Dispatcher& dispatcher) {
    impl_->window.bind("app_call", [&dispatcher](webui::window::event* event) {
        const std::string request = event->get_string();
        const std::string response = dispatcher.handle(request);
        event->return_string(response);
    });

    impl_->window.bind("", [](webui::window::event* event) {
        if (event->get_type() == webui::CONNECTED) {
            std::cout << "[webui] cliente conectado\n";
        } else if (event->get_type() == webui::DISCONNECTED) {
            std::cout << "[webui] cliente desconectado\n";
        }
    });
}

bool WebUiShell::set_port(std::size_t port) {
    return impl_->window.set_port(port);
}

std::size_t WebUiShell::port() const { return impl_->window.get_port(); }

bool WebUiShell::show() {
    const bool shown = impl_->window.show("index.html");
    if (!shown) {
        std::cerr << "[webui] falha ao abrir a janela (pasta: " << impl_->web_root << ")\n";
    } else {
        std::cout << "[webui] http://localhost:" << impl_->window.get_port() << "/\n";
    }
    return shown;
}

void WebUiShell::wait() { webui::wait(); }

const std::filesystem::path& WebUiShell::web_root() const { return impl_->web_root; }

} // namespace pf
