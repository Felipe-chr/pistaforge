#include "app/shell/file_dialog.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

namespace pf {
namespace {

std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), length);
    return result;
}

/// Filtro no formato do Win32: rótulo\0padrão\0...\0\0.
std::wstring build_filter() {
    std::wstring filter;
    const auto add = [&filter](std::wstring_view label, std::wstring_view pattern) {
        filter.append(label);
        filter.push_back(L'\0');
        filter.append(pattern);
        filter.push_back(L'\0');
    };
    add(L"Pistas PistaForge (*.pftrack)", L"*.pftrack");
    add(L"Todos os arquivos (*.*)", L"*.*");
    filter.push_back(L'\0');
    return filter;
}

} // namespace

std::optional<std::filesystem::path> show_file_dialog(const FileDialogRequest& request) {
    const std::wstring title = widen(
        request.title.empty() ? (request.save ? "Salvar pista" : "Abrir pista")
                              : request.title);
    const std::wstring filter = build_filter();
    const std::wstring extension = L"pftrack";
    const std::wstring initial_dir = request.initial_path.wstring();
    const std::wstring default_name = widen(request.default_name);

    std::vector<wchar_t> buffer(32768, L'\0');
    if (!default_name.empty()) {
        const std::size_t count = std::min(default_name.size(), buffer.size() - 1);
        std::copy_n(default_name.data(), count, buffer.data());
    }

    OPENFILENAMEW dialog{};
    dialog.lStructSize = static_cast<DWORD>(sizeof(dialog));
    dialog.hwndOwner = GetForegroundWindow();
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrInitialDir = initial_dir.empty() ? nullptr : initial_dir.c_str();
    dialog.lpstrTitle = title.c_str();
    dialog.lpstrDefExt = extension.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (request.save) {
        dialog.Flags |= OFN_OVERWRITEPROMPT;
    } else {
        dialog.Flags |= OFN_FILEMUSTEXIST;
    }

    const BOOL accepted =
        request.save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!accepted) return std::nullopt;
    return std::filesystem::path{buffer.data()};
}

} // namespace pf
