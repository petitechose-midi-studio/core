#include "state/modulation/ProjectControlState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::modulation {

FLASHMEM ProjectControlState::ProjectControlState() = default;

FLASHMEM void ProjectControlState::clear() {
    authored.clear();
    plan = {};
    runtime = {};
    sourceScratch.fill(0.0f);
    audition = {};
    focus = {};
    authoredRevision = 1;
    compiledRevision = 0;
    runtimeContextHash = 0;
    reserved = 0;
}

}  // namespace core::state::modulation
