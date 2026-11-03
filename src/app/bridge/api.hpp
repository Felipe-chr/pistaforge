#pragma once

namespace pf {

class Dispatcher;
struct AppState;

/// Registra os métodos da API do aplicativo.
void register_api(Dispatcher& dispatcher, AppState& state);

} // namespace pf
