#pragma once

namespace core::validation::ux {

struct StructureUxTraceState {
    bool ignoreNextBottomLeftRelease = false;
    bool ignoreNextBottomRightRelease = false;
    bool ignoreNextNavRelease = false;

    void clear() {
        ignoreNextBottomLeftRelease = false;
        ignoreNextBottomRightRelease = false;
        ignoreNextNavRelease = false;
    }
};

}  // namespace core::validation::ux
