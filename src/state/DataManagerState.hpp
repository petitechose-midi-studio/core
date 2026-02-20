#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {

enum class DataManagerDomain : uint8_t {
    MACRO_LIBRARY = 0,
    SEQ_PATTERN_LIBRARY = 1,
    SEQ_SET_LIBRARY = 2,
};

enum class DataManagerAction : uint8_t {
    SAVE = 0,
    LOAD = 1,
    ERASE = 2,
};

enum class DataManagerSetLoadMode : uint8_t {
    REPLACE = 0,
    MERGE = 1,
};

struct DataManagerSetLoadSelectorState {
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<int> selectedIndex{0};

    void reset() {
        visible.set(false);
        selectedIndex.set(0);
    }
};

struct DataManagerState {
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<uint8_t> focusedRow{0};
    oc::state::Signal<DataManagerDomain> domain{DataManagerDomain::MACRO_LIBRARY};
    oc::state::Signal<DataManagerAction> action{DataManagerAction::SAVE};
    oc::state::Signal<uint8_t> slotIndex{0};
    oc::state::Signal<DataManagerSetLoadMode> setLoadMode{DataManagerSetLoadMode::REPLACE};

    DataManagerSetLoadSelectorState setLoadSelector;

    void reset() {
        visible.set(false);
        focusedRow.set(0);
        domain.set(DataManagerDomain::MACRO_LIBRARY);
        action.set(DataManagerAction::SAVE);
        slotIndex.set(0);
        setLoadMode.set(DataManagerSetLoadMode::REPLACE);
        setLoadSelector.reset();
    }

    bool isSetLoadModeRowVisible() const {
        return domain.get() == DataManagerDomain::SEQ_SET_LIBRARY &&
               action.get() == DataManagerAction::LOAD;
    }

    uint8_t rowCount() const {
        return isSetLoadModeRowVisible() ? 4U : 3U;
    }
};

}  // namespace core::state
