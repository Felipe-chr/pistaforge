#pragma once

namespace pf {

class Dispatcher;
struct AppState;

/// Registra os métodos de documento e cena (`doc.*`, `scene.*`).
void register_doc_api(Dispatcher& dispatcher, AppState& state);

} // namespace pf
