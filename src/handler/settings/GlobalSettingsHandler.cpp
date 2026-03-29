#include "GlobalSettingsHandler.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>

#include <config/PlatformCompat.hpp>
#include <config/InputIDs.hpp>
#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"

namespace core::handler {
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

int currentChoiceIndexForRow(const core::state::MidiSyncState& sync, uint8_t row) {
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

void applyChoiceForRow(core::state::MidiSyncState& sync, uint8_t row, int choiceIndex) {
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

void persistRow(core::state::CoreSettings& settings,
                const core::state::MidiSyncState& sync,
                uint8_t row) {
    auto status = core::persistence::PersistenceWriteStatus::OK;
    switch (row) {
        case 0:
            status = settings.saveMidiSyncModeStatus(sync.mode.get());
            break;
        case 1:
            status = settings.saveMidiFollowTransportStatus(sync.followTransport.get());
            break;
        case 2:
            status = settings.saveMidiAutoFallbackMsStatus(sync.autoFallbackMs.get());
            break;
        case 3:
            status = settings.saveMidiAutoLockClockCountStatus(sync.autoLockClockCount.get());
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

    const auto commitStatus = settings.commitStatus();
    if (commitStatus != core::persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[GlobalSettings] Failed to commit settings row {}: {}",
                    row,
                    core::persistence::persistenceWriteStatusLabel(commitStatus));
    }
}

}  // namespace

FLASHMEM GlobalSettingsHandler::GlobalSettingsHandler(StateRefs state,
                                                      oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                                      oc::api::EncoderAPI& encoders,
                                                      oc::api::ButtonAPI& buttons,
                                                      oc::type::ScopeID settingsOverlayScope,
                                                      oc::type::ScopeID selectorOverlayScope)
    : global_settings_(state.globalSettings)
    , midi_sync_(state.midiSync)
    , settings_(state.settings)
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
            if (global_settings_.visible.get() || global_settings_.selector.visible.get()) {
                return false;
            }
            const auto current = overlays_.current();
            return current == core::ui::OverlayType::NONE || current == core::ui::OverlayType::VIEW_SELECTOR;
        })
        .then([this]() { openSettings(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(settings_overlay_scope_)
        .then([this](float delta) { moveFocus(delta); });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(settings_overlay_scope_)
        .then([this]() { openValueSelector(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(settings_overlay_scope_)
        .then([this]() { closeSettings(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(selector_overlay_scope_)
        .then([this](float delta) { navigateSelector(delta); });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(selector_overlay_scope_)
        .then([this]() { applySelectorAndClose(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(selector_overlay_scope_)
        .then([this]() { closeSelectorCancel(); });
}

FLASHMEM void GlobalSettingsHandler::openSettings() {
    global_settings_.reset();

    ignore_open_release_ = true;
    overlays_.show(core::ui::OverlayType::GLOBAL_SETTINGS, false);
}

FLASHMEM void GlobalSettingsHandler::closeSettings() {
    if (ignore_open_release_) {
        ignore_open_release_ = false;
        return;
    }

    overlays_.hide();
    global_settings_.reset();
}

FLASHMEM void GlobalSettingsHandler::moveFocus(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const int current = static_cast<int>(global_settings_.focusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, ROW_COUNT);

    global_settings_.focusedRow.set(static_cast<uint8_t>(next));
}

FLASHMEM void GlobalSettingsHandler::openValueSelector() {
    auto& s = global_settings_;
    const uint8_t row = s.focusedRow.get();
    const int current = currentChoiceIndexForRow(midi_sync_, row);
    modal::openSelectorOverlay(
        overlays_,
        core::ui::OverlayType::GLOBAL_SETTINGS_SELECTOR,
        s.selector,
        current,
        [row](auto& selector) { selector.editingRow.set(row); }
    );
}

FLASHMEM void GlobalSettingsHandler::navigateSelector(float delta) {
    const uint8_t row = global_settings_.selector.editingRow.get();
    int count = 0;

    switch (row) {
        case 0: count = MODE_COUNT; break;
        case 1: count = FOLLOW_COUNT; break;
        case 2: count = FALLBACK_COUNT; break;
        case 3: count = LOCK_COUNT; break;
        default: return;
    }

    int next = global_settings_.selector.selectedIndex.get();
    if (!modal::advanceWrappedSelection(delta, global_settings_.selector, count, next)) {
        return;
    }
    global_settings_.selector.selectedIndex.set(next);
}

FLASHMEM void GlobalSettingsHandler::applySelectorAndClose() {
    auto& selector = global_settings_.selector;
    const uint8_t row = selector.editingRow.get();
    const int choice = selector.selectedIndex.get();

    applyChoiceForRow(midi_sync_, row, choice);
    persistRow(settings_, midi_sync_, row);

    modal::hideOverlayAndResetSelector(overlays_, selector);
}

FLASHMEM void GlobalSettingsHandler::closeSelectorCancel() {
    modal::hideOverlayAndResetSelector(overlays_, global_settings_.selector);
}

}  // namespace core::handler
