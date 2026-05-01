#pragma once

namespace core::validation::ux {

struct StructureUxTraceState {
    bool ignoreNextBottomLeftRelease = false;
    bool ignoreNextBottomRightRelease = false;

    void clear() {
        ignoreNextBottomLeftRelease = false;
        ignoreNextBottomRightRelease = false;
    }
};

}  // namespace core::validation::ux
