#include <cassert>
#include <iostream>
#include <vector>

#include "../../src/handler/common/ModalSelectionUtils.hpp"
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

void test_selector_navigation_uses_selector_state_contract() {
    core::state::DeviceSettingsValueSelectorState selector;
    selector.visible.set(true);
    selector.selectedIndex.set(1);

    int next = selector.selectedIndex.get();
    const bool changed = core::handler::modal::advanceWrappedSelection(1.0f, selector, 3, next);

    assert(changed);
    assert(next == 2);

    next = selector.selectedIndex.get();
    const bool hiddenChanged =
        core::handler::modal::advanceWrappedSelection(1.0f, selector, 0, next);
    assert(!hiddenChanged);

    selector.visible.set(false);
    next = selector.selectedIndex.get();
    const bool invisibleChanged =
        core::handler::modal::advanceWrappedSelection(1.0f, selector, 3, next);
    assert(!invisibleChanged);

    std::cout << "[PASS] test_selector_navigation_uses_selector_state_contract\n";
}

void test_open_selector_overlay_resets_then_initializes_state() {
    DummyOverlayManager overlays;
    core::state::DeviceSettingsValueSelectorState selector;
    selector.visible.set(true);
    selector.editingRow.set(3);
    selector.selectedIndex.set(9);

    core::handler::modal::openSelectorOverlay(
        overlays,
        DummyOverlay::SELECTOR,
        selector,
        2,
        [](auto& valueSelector) { valueSelector.editingRow.set(1); }
    );

    assert(overlays.current() == DummyOverlay::SELECTOR);
    assert(selector.selectedIndex.get() == 2);
    assert(selector.editingRow.get() == 1);
    assert(!selector.visible.get());

    std::cout << "[PASS] test_open_selector_overlay_resets_then_initializes_state\n";
}

void test_hide_overlay_and_reset_selector_clears_state() {
    DummyOverlayManager overlays;
    core::state::DeviceSettingsValueSelectorState selector;

    overlays.show(DummyOverlay::SELECTOR, true);
    selector.visible.set(true);
    selector.editingRow.set(2);
    selector.selectedIndex.set(4);

    core::handler::modal::hideOverlayAndResetSelector(overlays, selector);

    assert(overlays.current() == DummyOverlay::NONE);
    assert(!selector.visible.get());
    assert(selector.editingRow.get() == 0);
    assert(selector.selectedIndex.get() == 0);

    std::cout << "[PASS] test_hide_overlay_and_reset_selector_clears_state\n";
}

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

void test_hide_while_current_in_unwinds_overlay_stack() {
    DummyOverlayManager overlays;
    overlays.show(DummyOverlay::ROOT, false);
    overlays.show(DummyOverlay::SELECTOR, true);
    overlays.show(DummyOverlay::DIALOG, true);

    core::handler::modal::hideWhileCurrentIn(
        overlays,
        std::array{DummyOverlay::SELECTOR, DummyOverlay::DIALOG}
    );

    assert(overlays.current() == DummyOverlay::ROOT);

    std::cout << "[PASS] test_hide_while_current_in_unwinds_overlay_stack\n";
}

}  // namespace

int main() {
    test_selector_navigation_uses_selector_state_contract();
    test_open_selector_overlay_resets_then_initializes_state();
    test_hide_overlay_and_reset_selector_clears_state();
    test_hide_if_current_only_closes_matching_overlay();
    test_hide_while_current_in_unwinds_overlay_stack();

    std::cout << "\nAll ModalSelectionUtils tests passed.\n";
    return 0;
}
