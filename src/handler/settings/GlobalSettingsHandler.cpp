#include "GlobalSettingsHandler.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "state/CoreState.hpp"

namespace core::handler {

using oc::ui::lvgl::scope;
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

namespace {

constexpr int MODE_COUNT = 3;
constexpr int FOLLOW_COUNT = 2;
constexpr int FALLBACK_COUNT = 7;
constexpr int LOCK_COUNT = 8;

constexpr core::state::MidiSyncMode MODE_VALUES[MODE_COUNT] = {
    core::state::MidiSyncMode::MASTER,
    core::state::MidiSyncMode::SLAVE,
    core::state::MidiSyncMode::AUTO,
};

constexpr bool FOLLOW_VALUES[FOLLOW_COUNT] = {false, true};
constexpr uint16_t FALLBACK_VALUES[FALLBACK_COUNT] = {150, 250, 500, 750, 1000, 1500, 2000};
constexpr uint8_t LOCK_VALUES[LOCK_COUNT] = {1, 2, 3, 4, 6, 8, 12, 24};

template <typename T, int N>
int findChoiceIndex(const T& value, const T (&choices)[N], int fallback = 0) {
    for (int i = 0; i < N; ++i) {
        if (choices[i] == value) return i;
    }
    return std::clamp(fallback, 0, N - 1);
}

int currentChoiceIndexForRow(const core::state::CoreState& state, uint8_t row) {
    const auto& sync = state.midiSync;

    switch (row) {
        case 0:
            return findChoiceIndex(sync.mode.get(), MODE_VALUES,
                                   findChoiceIndex(core::state::MidiSyncMode::AUTO, MODE_VALUES, 0));
        case 1:
            return findChoiceIndex(sync.followTransport.get(), FOLLOW_VALUES, 1);
        case 2:
            return findChoiceIndex(sync.autoFallbackMs.get(), FALLBACK_VALUES,
                                   findChoiceIndex(static_cast<uint16_t>(500), FALLBACK_VALUES, 0));
        case 3:
            return findChoiceIndex(sync.autoLockClockCount.get(), LOCK_VALUES,
                                   findChoiceIndex(static_cast<uint8_t>(6), LOCK_VALUES, 0));
        default:
            return 0;
    }
}

void applyChoiceForRow(core::state::CoreState& state, uint8_t row, int choiceIndex) {
    auto& sync = state.midiSync;

    switch (row) {
        case 0: {
            const int idx = std::clamp(choiceIndex, 0, MODE_COUNT - 1);
            sync.mode.set(MODE_VALUES[idx]);
            return;
        }
        case 1: {
            const int idx = std::clamp(choiceIndex, 0, FOLLOW_COUNT - 1);
            sync.followTransport.set(FOLLOW_VALUES[idx]);
            return;
        }
        case 2: {
            const int idx = std::clamp(choiceIndex, 0, FALLBACK_COUNT - 1);
            sync.autoFallbackMs.set(FALLBACK_VALUES[idx]);
            return;
        }
        case 3: {
            const int idx = std::clamp(choiceIndex, 0, LOCK_COUNT - 1);
            sync.autoLockClockCount.set(LOCK_VALUES[idx]);
            return;
        }
        default:
            return;
    }
}

void persistRow(core::state::CoreState& state, uint8_t row) {
    auto status = core::persistence::PersistenceWriteStatus::OK;
    switch (row) {
        case 0:
            status = state.settings.saveMidiSyncModeStatus(state.midiSync.mode.get());
            break;
        case 1:
            status = state.settings.saveMidiFollowTransportStatus(
                state.midiSync.followTransport.get()
            );
            break;
        case 2:
            status = state.settings.saveMidiAutoFallbackMsStatus(
                state.midiSync.autoFallbackMs.get()
            );
            break;
        case 3:
            status = state.settings.saveMidiAutoLockClockCountStatus(
                state.midiSync.autoLockClockCount.get()
            );
            break;
        default:
            return;
    }

    if (status != core::persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[GlobalSettings] Failed to stage settings row {}: {}",
                    row,
                    core::persistence::persistenceWriteStatusLabel(status));
        return;
    }

    const auto commitStatus = state.settings.commitStatus();
    if (commitStatus != core::persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[GlobalSettings] Failed to commit settings row {}: {}",
                    row,
                    core::persistence::persistenceWriteStatusLabel(commitStatus));
    }
}

}  // namespace

FLASHMEM GlobalSettingsHandler::GlobalSettingsHandler(core::state::CoreState& state,
                                                      oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                                      oc::api::EncoderAPI& encoders,
                                                      oc::api::ButtonAPI& buttons,
                                                      lv_obj_t* settingsOverlayScope,
                                                      lv_obj_t* selectorOverlayScope)
    : state_(state)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , settings_overlay_scope_(settingsOverlayScope)
    , selector_overlay_scope_(selectorOverlayScope) {
    setupBindings();
}

FLASHMEM void GlobalSettingsHandler::setupBindings() {
    buttons_.button(ButtonID::LEFT_TOP)
        .longPress(SETTINGS_LONG_PRESS_MS)
        .when([this]() {
            if (state_.globalSettings.visible.get() || state_.globalSettings.selector.visible.get()) {
                return false;
            }
            const auto current = overlays_.current();
            return current == core::ui::OverlayType::NONE || current == core::ui::OverlayType::VIEW_SELECTOR;
        })
        .then([this]() { openSettings(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(scope(settings_overlay_scope_))
        .then([this](float delta) { moveFocus(delta); });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(scope(settings_overlay_scope_))
        .then([this]() { openValueSelector(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(scope(settings_overlay_scope_))
        .then([this]() { closeSettings(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(scope(selector_overlay_scope_))
        .then([this](float delta) { navigateSelector(delta); });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(scope(selector_overlay_scope_))
        .then([this]() { applySelectorAndClose(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(scope(selector_overlay_scope_))
        .then([this]() { closeSelectorCancel(); });
}

FLASHMEM void GlobalSettingsHandler::openSettings() {
    auto& s = state_.globalSettings;
    s.reset();

    ignore_open_release_ = true;
    overlays_.show(core::ui::OverlayType::GLOBAL_SETTINGS, false);
}

FLASHMEM void GlobalSettingsHandler::closeSettings() {
    if (ignore_open_release_) {
        ignore_open_release_ = false;
        return;
    }

    overlays_.hide();
    state_.globalSettings.reset();
}

FLASHMEM void GlobalSettingsHandler::moveFocus(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const int current = static_cast<int>(state_.globalSettings.focusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, ROW_COUNT);

    state_.globalSettings.focusedRow.set(static_cast<uint8_t>(next));
}

FLASHMEM void GlobalSettingsHandler::openValueSelector() {
    auto& s = state_.globalSettings;
    const uint8_t row = s.focusedRow.get();

    auto& selector = s.selector;
    selector.reset();
    selector.editingRow.set(row);

    const int current = currentChoiceIndexForRow(state_, row);
    selector.selectedIndex.set(current);

    overlays_.show(core::ui::OverlayType::GLOBAL_SETTINGS_SELECTOR, true);
}

FLASHMEM void GlobalSettingsHandler::navigateSelector(float delta) {
    const uint8_t row = state_.globalSettings.selector.editingRow.get();
    int count = 0;

    switch (row) {
        case 0: count = MODE_COUNT; break;
        case 1: count = FOLLOW_COUNT; break;
        case 2: count = FALLBACK_COUNT; break;
        case 3: count = LOCK_COUNT; break;
        default: return;
    }

    const int current = state_.globalSettings.selector.selectedIndex.get();
    int next = current;
    if (!modal::advanceWrappedSelection(
            delta,
            state_.globalSettings.selector.visible.get(),
            current,
            count,
            next
        )) {
        return;
    }
    state_.globalSettings.selector.selectedIndex.set(next);
}

FLASHMEM void GlobalSettingsHandler::applySelectorAndClose() {
    auto& selector = state_.globalSettings.selector;
    const uint8_t row = selector.editingRow.get();
    const int choice = selector.selectedIndex.get();

    applyChoiceForRow(state_, row, choice);
    persistRow(state_, row);

    modal::hideOverlayAndReset(overlays_, [&selector]() { selector.reset(); });
}

FLASHMEM void GlobalSettingsHandler::closeSelectorCancel() {
    modal::hideOverlayAndReset(overlays_, [this]() { state_.globalSettings.selector.reset(); });
}

}  // namespace core::handler
