#pragma once

#include "MemoryStorage.hpp"

namespace test_support {

struct CoreStorages {
    MemoryStorage settings;

    CoreStorages() { initAll(); }

    void initAll() { settings.init(); }
};

}  // namespace test_support
