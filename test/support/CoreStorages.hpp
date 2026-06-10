#pragma once

#include "MemoryStorage.hpp"

namespace test_support {

struct CoreStorages {
    MemoryStorage settings;
    MemoryStorage macroLibrary;
    MemoryStorage sequencerPatternLibrary;
    MemoryStorage sequencerSetLibrary;

    CoreStorages() {
        initAll();
    }

    void initAll() {
        settings.init();
        macroLibrary.init();
        sequencerPatternLibrary.init();
        sequencerSetLibrary.init();
    }
};

}  // namespace test_support
