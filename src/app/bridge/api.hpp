#pragma once

namespace pf {

class Dispatcher;

/// Registra os métodos da API do aplicativo.
void register_api(Dispatcher& dispatcher);

} // namespace pf
