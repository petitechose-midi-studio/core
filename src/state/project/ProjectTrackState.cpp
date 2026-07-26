#include "state/project/ProjectTrackState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::project {

FLASHMEM ProjectTrackState::ProjectTrackState()
    : authored{defaultProjectTrackSnapshot()}
    , revision{0U} {}

FLASHMEM void ProjectTrackState::reset() {
    authored = defaultProjectTrackSnapshot();
    revision.set(0U);
}

}  // namespace core::state::project
