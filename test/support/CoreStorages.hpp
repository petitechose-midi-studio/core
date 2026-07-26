#pragma once

#include "persistence/SequencerPersistence.hpp"

#include "MemoryStorage.hpp"

namespace test_support {

struct CoreStorages {
    MemoryStorage settings;
    MemoryStorage macroLibrary;
    MemoryStorage sequencerPatternLibrary;
    MemoryStorage sequencerSetLibrary;

    CoreStorages()
        : sequencerPatternLibrary(
              core::persistence::SequencerPersistence::
                  PATTERN_LIBRARY_STORAGE_CAPACITY)
        , sequencerSetLibrary(
              core::persistence::SequencerPersistence::
                  SET_LIBRARY_STORAGE_CAPACITY) {
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
