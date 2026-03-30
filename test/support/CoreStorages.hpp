#pragma once

#include "MemoryStorage.hpp"

namespace test_support {

struct CoreStorages {
    MemoryStorage settings;
    MemoryStorage macroWorkspace;
    MemoryStorage macroLibrary;
    MemoryStorage sequencerWorkspace;
    MemoryStorage sequencerPatternLibrary;
    MemoryStorage sequencerSetLibrary;

    CoreStorages() {
        initAll();
    }

    void initAll() {
        settings.init();
        macroWorkspace.init();
        macroLibrary.init();
        sequencerWorkspace.init();
        sequencerPatternLibrary.init();
        sequencerSetLibrary.init();
    }
};

}  // namespace test_support
