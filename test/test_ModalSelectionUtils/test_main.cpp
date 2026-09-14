#include <cassert>
#include <iostream>
#include <vector>

#include "../../src/handler/common/ModalSelectionUtils.hpp"
#include "../../src/handler/common/ValueSelectorInput.hpp"
#include "../../src/state/DeviceSettingsState.hpp"

namespace {

enum class DummyOverlay {
    NONE = 0,
    SELECTOR = 1,
    DIALOG = 2,
    ROOT = 3,
};

class DummyOverlayManager {
public:
    DummyOverlay current() const {
        if (stack_.empty()) {
            return DummyOverlay::NONE;
        }
        return stack_.back();
    }

    void show(DummyOverlay overlay, bool stacked = true) {
        if (!stacked || stack_.empty()) {
            stack_.clear();
        }
        stack_.push_back(overlay);
    }

    void hide() {
        if (!stack_.empty()) {
            stack_.pop_back();
        }
    }

private:
    std::vector<DummyOverlay> stack_{};
};

void test_hide_if_current_only_closes_matching_overlay() {
    DummyOverlayManager overlays;
    overlays.show(DummyOverlay::ROOT, false);
    overlays.show(DummyOverlay::SELECTOR, true);

    const bool ignored = core::handler::modal::hideIfCurrent(overlays, DummyOverlay::DIALOG);
    assert(!ignored);
    assert(overlays.current() == DummyOverlay::SELECTOR);

    const bool hidden = core::handler::modal::hideIfCurrent(overlays, DummyOverlay::SELECTOR);
    assert(hidden);
    assert(overlays.current() == DummyOverlay::ROOT);

    std::cout << "[PASS] test_hide_if_current_only_closes_matching_overlay\n";
}

void test_value_selector_publication_and_stack_boundaries() {
    // Independent publication/stack assertions for the shared lifecycle.
    using Input = core::handler::modal::ValueSelectorInput;
    DummyOverlayManager overlays;
    core::state::DeviceSettingsValueSelectorState selector;
    int count = 3;
    int applied = 0;
    int closed = 0;
    bool accept = false;
    bool ownerActive = true;
    auto dispatch = [&](Input input) {
        input.handle(overlays, DummyOverlay::SELECTOR, selector, ownerActive, count,
            [&](uint8_t row, int choice) {
                assert(row == 2U && choice == 1);
                ++applied;
                return accept;
            },
            [&]() {
                assert(overlays.current() == DummyOverlay::ROOT);
                selector.reset();
                ++closed;
            });
    };
    overlays.show(DummyOverlay::ROOT);
    overlays.show(DummyOverlay::SELECTOR);
    selector.visible.set(true);
    selector.editingRow.set(2U);
    selector.selectedIndex.set(1);
    dispatch({Input::Kind::ACCEPT});
    assert(applied == 1 && closed == 0);
    assert(selector.selectedIndex.get() == 1 && selector.visible.get());
    assert(overlays.current() == DummyOverlay::SELECTOR);

    // Dynamic choice range is rechecked before reaching the domain.
    count = 1;
    dispatch({Input::Kind::ACCEPT});
    selector.selectedIndex.set(-1);
    dispatch({Input::Kind::ACCEPT});
    assert(applied == 1 && closed == 0);
    count = 3;
    selector.selectedIndex.set(1);

    ownerActive = false;
    for (auto kind : {Input::Kind::MOVE, Input::Kind::ACCEPT, Input::Kind::CANCEL}) {
        dispatch({kind, 1.0f});
        assert(selector.selectedIndex.get() == 1 && applied == 1 && closed == 0);
        assert(overlays.current() == DummyOverlay::SELECTOR);
    }
    ownerActive = true;

    overlays.show(DummyOverlay::DIALOG);
    for (auto kind : {Input::Kind::MOVE, Input::Kind::ACCEPT, Input::Kind::CANCEL}) {
        dispatch({kind, 1.0f});
        assert(selector.selectedIndex.get() == 1);
        assert(applied == 1 && closed == 0);
        assert(overlays.current() == DummyOverlay::DIALOG);
    }
    overlays.hide();
    accept = true;
    dispatch({Input::Kind::ACCEPT});
    assert(applied == 2 && closed == 1);
    assert(overlays.current() == DummyOverlay::ROOT);
    dispatch({Input::Kind::ACCEPT});
    dispatch({Input::Kind::CANCEL});
    assert(applied == 2 && closed == 1);

    overlays.show(DummyOverlay::SELECTOR);
    // Lost visibility must fail closed even if an input was already routed.
    dispatch({Input::Kind::ACCEPT});
    assert(applied == 2 && closed == 1);
    selector.visible.set(true);
    selector.selectedIndex.set(0);
    dispatch({Input::Kind::MOVE, -1.0f});
    assert(selector.selectedIndex.get() == 2);
    dispatch({Input::Kind::MOVE, 1.0f});
    dispatch({Input::Kind::MOVE, 0.0f});
    assert(selector.selectedIndex.get() == 0);
    count = 0;
    dispatch({Input::Kind::MOVE, 1.0f});
    dispatch({Input::Kind::ACCEPT});
    assert(selector.selectedIndex.get() == 0 && applied == 2);
    dispatch({Input::Kind::CANCEL});
    assert(applied == 2 && closed == 2);
    assert(overlays.current() == DummyOverlay::ROOT);
    std::cout << "[PASS] Value selector publication and stack boundaries\n";
}

}  // namespace

int main() {
    test_value_selector_publication_and_stack_boundaries();
    test_hide_if_current_only_closes_matching_overlay();

    std::cout << "\nAll ModalSelectionUtils tests passed.\n";
    return 0;
}
