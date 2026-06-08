#pragma once

namespace core::persistence {
class ProductFileService;
}

namespace core::state {
struct CoreState;
}

namespace core::validation::project {

bool runProjectStoreSmoke(core::persistence::ProductFileService& productFiles,
                          core::state::CoreState& state);

}  // namespace core::validation::project
