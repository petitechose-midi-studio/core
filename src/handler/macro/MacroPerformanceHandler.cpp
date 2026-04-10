#include "handler/macro/MacroPerformanceHandler.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>

#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>

#include "config/InputIDs.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"

namespace core::handler {

namespace input_utils = core::handler::sequencer::input_utils;

namespace {

inline oc::type::IsActiveFn performanceAvailable(
    core::state::macro::MacroUiState& macroUi,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays
) {
    return [&macroUi, &overlays]() {
        return !overlays.hasVisible() && !macroUi.structureSelection.active.get();
    };
}

inline oc::type::IsActiveFn quickControlsSelecting(
    core::state::macro::MacroUiState& macroUi,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays
) {
    return [&macroUi, &overlays]() {
        return macroUi.quickControlsSelecting.get() && !overlays.hasVisible();
    };
}

inline oc::type::IsActiveFn clutchActive(
    core::state::macro::MacroUiState& macroUi,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays
) {
    return [&macroUi, &overlays]() {
        return macroUi.clutchActive.get() &&
               !macroUi.quickControlsSelecting.get() &&
               !macroUi.structureSelection.active.get() &&
               !overlays.hasVisible();
    };
}

inline oc::type::IsActiveFn clutchInactive(
    core::state::macro::MacroUiState& macroUi,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays
) {
    return [&macroUi, &overlays]() {
        return !macroUi.clutchActive.get() &&
               !macroUi.quickControlsSelecting.get() &&
               !macroUi.structureSelection.active.get() &&
               !overlays.hasVisible();
    };
}

float normalizedForProperty(const core::handler::MacroDomainServices& services,
                            uint8_t index,
                            core::state::macro::MacroPerformanceProperty property) {
    if (property == core::state::macro::MacroPerformanceProperty::CC) {
        return input_utils::indexToNormalized(services.activeConfig(index).cc, 128);
    }

    if (property == core::state::macro::MacroPerformanceProperty::CHANNEL) {
        return input_utils::indexToNormalized(services.activeConfig(index).channel, 16);
    }

    return services.runtimeValue(index);
}

inline oc::type::IsActiveFn selectionActive(
    core::state::macro::MacroUiState& macroUi,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays
) {
    return [&macroUi, &overlays]() {
        return macroUi.structureSelection.active.get() && !overlays.hasVisible();
    };
}

uint8_t countEnabledMacroTracks(uint16_t enabledMask) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < core::state::macro::TRACK_COUNT; ++i) {
        if ((enabledMask & static_cast<uint16_t>(1U << i)) != 0) {
            ++count;
        }
    }
    return count;
}

uint8_t countEnabledMacroPages(uint16_t enabledMask) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < core::state::macro::PAGE_COUNT; ++i) {
        if ((enabledMask & static_cast<uint16_t>(1U << i)) != 0) {
            ++count;
        }
    }
    return count;
}

uint8_t nextEnabledIndex(uint16_t enabledMask, uint8_t current, uint8_t count, int direction) {
    if (count == 0) return current;

    for (uint8_t offset = 1; offset < count; ++offset) {
        const int candidate =
            (static_cast<int>(current) + (direction * static_cast<int>(offset)) + count) % count;
        const uint16_t bit = static_cast<uint16_t>(1U << static_cast<uint8_t>(candidate));
        if ((enabledMask & bit) != 0) {
            return static_cast<uint8_t>(candidate);
        }
    }

    return current;
}

int nextMacroAddIndexAfterHighest(uint16_t enabledMask, uint8_t count) {
    for (int index = static_cast<int>(count) - 1; index >= 0; --index) {
        if ((enabledMask & static_cast<uint16_t>(1U << static_cast<uint8_t>(index))) == 0) {
            continue;
        }
        const int next = index + 1;
        return (next < count) ? next : -1;
    }
    return (count > 0) ? 0 : -1;
}

struct StructureNavTarget {
    uint8_t index = 0;
    bool addSlot = false;
    bool valid = false;
};

uint8_t firstEnabledIndex(uint16_t enabledMask, uint8_t count) {
    for (uint8_t i = 0; i < count; ++i) {
        if ((enabledMask & static_cast<uint16_t>(1U << i)) != 0) {
            return i;
        }
    }
    return 0;
}

uint8_t lastEnabledIndex(uint16_t enabledMask, uint8_t count) {
    for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
        const auto index = static_cast<uint8_t>(i);
        if ((enabledMask & static_cast<uint16_t>(1U << index)) != 0) {
            return index;
        }
    }
    return 0;
}

StructureNavTarget nextStructureTarget(
    uint16_t enabledMask,
    uint8_t current,
    uint8_t count,
    bool currentAddSlot,
    int direction
) {
    const int addIndex = nextMacroAddIndexAfterHighest(enabledMask, count);
    const uint8_t firstEnabled = firstEnabledIndex(enabledMask, count);
    const uint8_t lastEnabled = lastEnabledIndex(enabledMask, count);

    if (currentAddSlot) {
        if (direction < 0) {
            return {.index = lastEnabled, .addSlot = false, .valid = true};
        }
        return {
            .index = (addIndex >= 0) ? static_cast<uint8_t>(addIndex) : lastEnabled,
            .addSlot = (addIndex >= 0),
            .valid = true,
        };
    }

    if (direction > 0) {
        for (uint8_t candidate = static_cast<uint8_t>(current + 1); candidate < count; ++candidate) {
            if ((enabledMask & static_cast<uint16_t>(1U << candidate)) != 0) {
                return {.index = candidate, .addSlot = false, .valid = true};
            }
        }

        if (addIndex >= 0 && current == lastEnabled) {
            return {
                .index = static_cast<uint8_t>(addIndex),
                .addSlot = true,
                .valid = true,
            };
        }

        return {.index = firstEnabled, .addSlot = false, .valid = true};
    }

    for (int candidate = static_cast<int>(current) - 1; candidate >= 0; --candidate) {
        const auto index = static_cast<uint8_t>(candidate);
        if ((enabledMask & static_cast<uint16_t>(1U << index)) != 0) {
            return {.index = index, .addSlot = false, .valid = true};
        }
    }

    return {.index = lastEnabled, .addSlot = false, .valid = true};
}

}  // namespace

FLASHMEM MacroPerformanceHandler::MacroPerformanceHandler(
    StateRefs state,
    MacroDomainServices services,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId)
    : macro_ui_(state.macroUi)
    , pages_(state.pages)
    , navigation_focus_(state.navigationFocus)
    , structure_clipboard_(state.structureClipboard)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId) {
    configureMacroEncoders();
    setupBindings();
}

FLASHMEM void MacroPerformanceHandler::setupBindings() {
    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .press()
        .latch()
        .scope(scope_id_)
        .when([this]() {
            left_center_held_ = true;
            return performanceAvailable(macro_ui_, overlays_)() &&
                   !left_bottom_held_;
        })
        .then([this]() { openQuickControls(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(scope_id_)
        .then([this]() {
            left_center_held_ = false;
            if (macro_ui_.quickControlsSelecting.get()) {
                closeQuickControlsApply();
            }
        });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .press()
        .latch()
        .scope(scope_id_)
        .when([this]() {
            left_bottom_held_ = true;
            return performanceAvailable(macro_ui_, overlays_)() &&
                   !left_center_held_ &&
                   !macro_ui_.quickControlsSelecting.get();
        })
        .then([this]() { activateClutch(); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(selectionActive(macro_ui_, overlays_))
        .then([this](float delta) { navigateSelection(delta); });

    buttons_.button(Config::ButtonID::NAV)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when(clutchInactive(macro_ui_, overlays_))
        .then([this]() {
            nav_long_press_used_ = true;
            enterSelectionMode(core::state::selectionScopeForFocus(
                navigation_focus_.get()
            ));
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when(selectionActive(macro_ui_, overlays_))
        .then([this]() {
            if (nav_long_press_used_) {
                nav_long_press_used_ = false;
                return;
            }
            toggleSelectionAtCursor();
        });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope_id_)
        .then([this]() {
            left_bottom_held_ = false;
            if (macro_ui_.clutchActive.get() &&
                !macro_ui_.quickControlsSelecting.get()) {
                deactivateClutch();
            }
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when(clutchInactive(macro_ui_, overlays_))
        .then([this]() {
            if (nav_long_press_used_) {
                nav_long_press_used_ = false;
                return;
            }
            if (macro_ui_.previewAddSlot.get()) {
                createPreviewedStructure();
                return;
            }
            cycleNavigationFocus();
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(quickControlsSelecting(macro_ui_, overlays_))
        .then([this](float delta) { navigateQuickControls(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(clutchActive(macro_ui_, overlays_))
        .then([this](float delta) { navigateProperty(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(clutchInactive(macro_ui_, overlays_))
        .then([this](float delta) {
            switch (navigation_focus_.get()) {
                case core::state::StructureNavigationFocus::TRACK:
                    moveTrack(delta);
                    return;
                case core::state::StructureNavigationFocus::PAGE:
                default:
                    movePage(delta);
                    return;
            }
        });

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(scope_id_)
        .when(quickControlsSelecting(macro_ui_, overlays_))
        .then([this](float normalized) { setFocusedQuickControlValue(normalized); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when(selectionActive(macro_ui_, overlays_))
        .then([this]() { cancelSelectionMode(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when(clutchInactive(macro_ui_, overlays_))
        .then([this]() {
            if (canRemoveCurrentStructure()) {
                beginHoldAction(core::state::StructureHoldAction::REMOVE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when(selectionActive(macro_ui_, overlays_))
        .then([this]() { deleteSelection(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when(clutchInactive(macro_ui_, overlays_))
        .then([this]() {
            clearHoldAction();
            if (ignore_next_bottom_left_release_) {
                ignore_next_bottom_left_release_ = false;
                return;
            }
            eraseCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when(clutchInactive(macro_ui_, overlays_))
        .then([this]() {
            clearHoldAction();
            ignore_next_bottom_left_release_ = true;
            removeCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when(clutchInactive(macro_ui_, overlays_))
        .then([this]() {
            if (canPasteCurrentStructure()) {
                beginHoldAction(core::state::StructureHoldAction::PASTE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when(selectionActive(macro_ui_, overlays_))
        .then([this]() { duplicateSelection(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when(clutchInactive(macro_ui_, overlays_))
        .then([this]() {
            clearHoldAction();
            if (ignore_next_bottom_right_release_) {
                ignore_next_bottom_right_release_ = false;
                return;
            }
            copyCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when(clutchInactive(macro_ui_, overlays_))
        .then([this]() {
            clearHoldAction();
            ignore_next_bottom_right_release_ = true;
            pasteCurrentStructure();
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when(quickControlsSelecting(macro_ui_, overlays_))
        .then([this]() { closeQuickControlsCancel(); });
}

FLASHMEM void MacroPerformanceHandler::activateClutch() {
    if (overlays_.hasVisible()) return;
    if (macro_ui_.quickControlsSelecting.get()) return;
    macro_ui_.previewAddSlot.set(false);
    macro_ui_.clutchActive.set(true);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::deactivateClutch() {
    if (!macro_ui_.clutchActive.get()) return;
    macro_ui_.clutchActive.set(false);
    macro_ui_.activeProperty.set(core::state::macro::MacroPerformanceProperty::VALUE);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::openQuickControls() {
    if (overlays_.hasVisible()) return;

    macro_ui_.previewAddSlot.set(false);
    macro_ui_.clutchActive.set(false);
    macro_ui_.quickControlsSelecting.set(true);
    macro_ui_.focusedQuickControl.set(core::state::macro::MacroQuickControlItem::GLOBAL_CHANNEL);
    macro_ui_.ccOffset.set(0);
    quick_snapshot_page_ = pages_.activePage;
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        quick_snapshot_configs_[i] = services_.activeConfig(i);
    }
    configureQuickControlEncoder();
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::closeQuickControlsApply() {
    if (!macro_ui_.quickControlsSelecting.get()) return;
    macro_ui_.quickControlsSelecting.set(false);
    resetQuickControlsState();
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::closeQuickControlsCancel() {
    if (!macro_ui_.quickControlsSelecting.get()) return;

    const uint8_t originalPage = quick_snapshot_page_;
    if (pages_.activePage != originalPage) {
        services_.switchToPage(originalPage);
    }
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        services_.setConfig(i, quick_snapshot_configs_[i].channel, quick_snapshot_configs_[i].cc);
    }
    macro_ui_.quickControlsSelecting.set(false);
    resetQuickControlsState();
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::navigateQuickControls(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const int current = core::state::macro::quickControlIndex(macro_ui_.focusedQuickControl.get());
    const int next = nav::nextWrappedIndex(delta, current, 2);
    macro_ui_.focusedQuickControl.set(core::state::macro::quickControlAtIndex(next));
    configureQuickControlEncoder();
}

FLASHMEM void MacroPerformanceHandler::setFocusedQuickControlValue(float normalized) {
    const auto item = macro_ui_.focusedQuickControl.get();
    if (item == core::state::macro::MacroQuickControlItem::CC_OFFSET) {
        const int offset = normalizedToOffset(normalized);
        if (macro_ui_.ccOffset.get() == offset) return;
        macro_ui_.ccOffset.set(static_cast<int8_t>(offset));
        applyCcOffsetFromSnapshot(offset);
        return;
    }

    const uint8_t channel =
        static_cast<uint8_t>(input_utils::normalizedToIndex(std::clamp(normalized, 0.0f, 1.0f), 16));
    if (services_.activeConfig(0).channel == channel) return;
    applyGlobalChannel(channel);
}

FLASHMEM void MacroPerformanceHandler::navigateProperty(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const int current = core::state::macro::performancePropertyIndex(
        macro_ui_.activeProperty.get()
    );
    const int next = nav::nextWrappedIndex(delta, current, 3);
    macro_ui_.activeProperty.set(core::state::macro::performancePropertyAtIndex(next));
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::cycleNavigationFocus() {
    const auto current = navigation_focus_.get();
    const auto next = (current == core::state::StructureNavigationFocus::PAGE)
        ? core::state::StructureNavigationFocus::TRACK
        : core::state::StructureNavigationFocus::PAGE;
    macro_ui_.previewAddSlot.set(false);
    navigation_focus_.set(next);
}

FLASHMEM void MacroPerformanceHandler::movePage(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const uint16_t enabledMask = services_.pageEnabledMask();
    if (countEnabledMacroPages(enabledMask) == 0) return;

    const uint8_t current = pages_.activePage;
    const bool currentAddSlot = macro_ui_.previewAddSlot.get();
    const auto target = nextStructureTarget(
        enabledMask,
        current,
        core::state::macro::PAGE_COUNT,
        currentAddSlot,
        nav::turnStep(delta)
    );
    if (!target.valid) return;
    if (target.addSlot) {
        macro_ui_.previewAddSlot.set(true);
        return;
    }

    macro_ui_.previewAddSlot.set(false);
    if (target.index != current) {
        services_.switchToPage(target.index);
    } else if (!currentAddSlot) {
        return;
    }
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::moveTrack(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const uint16_t enabledMask = services_.trackEnabledMask();
    if (countEnabledMacroTracks(enabledMask) == 0) return;

    const uint8_t current = services_.activeTrack();
    const bool currentAddSlot = macro_ui_.previewAddSlot.get();
    const auto target = nextStructureTarget(
        enabledMask,
        current,
        core::state::macro::TRACK_COUNT,
        currentAddSlot,
        nav::turnStep(delta)
    );
    if (!target.valid) return;
    if (target.addSlot) {
        macro_ui_.previewAddSlot.set(true);
        return;
    }

    macro_ui_.previewAddSlot.set(false);
    if (target.index != current) {
        services_.switchToTrack(target.index);
    } else if (!currentAddSlot) {
        return;
    }
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::eraseCurrentStructure() {
    if (macro_ui_.previewAddSlot.get()) return;

    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            services_.eraseTrack(services_.activeTrack());
            return;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            services_.erasePage(pages_.activePage);
            return;
    }
}

FLASHMEM void MacroPerformanceHandler::removeCurrentStructure() {
    if (macro_ui_.previewAddSlot.get()) return;

    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            services_.deleteActiveTrack();
            return;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            services_.deleteActivePage();
            return;
    }
}

FLASHMEM bool MacroPerformanceHandler::canRemoveCurrentStructure() const {
    if (macro_ui_.previewAddSlot.get()) return false;

    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            return countEnabledMacroTracks(services_.trackEnabledMask()) > 1U;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            return countEnabledMacroPages(services_.pageEnabledMask()) > 1U;
    }
}

FLASHMEM void MacroPerformanceHandler::copyCurrentStructure() {
    if (macro_ui_.previewAddSlot.get()) return;

    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            structure_clipboard_.storeMacroTrack(pages_.tracks[services_.activeTrack()]);
            return;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            structure_clipboard_.storeMacroPage(pages_.activePageData());
            return;
    }
}

FLASHMEM void MacroPerformanceHandler::pasteCurrentStructure() {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        if (!structure_clipboard_.hasMacroTrack()) return;
        const uint8_t addTrackIndex = static_cast<uint8_t>(std::max(
            0,
            nextMacroAddIndexAfterHighest(
                services_.trackEnabledMask(),
                core::state::macro::TRACK_COUNT
            )
        ));
        const uint8_t targetIndex = macro_ui_.previewAddSlot.get() ? addTrackIndex : services_.activeTrack();
        if (targetIndex >= core::state::macro::TRACK_COUNT) return;
        services_.pasteTrack(targetIndex, structure_clipboard_.macroTrack);
        macro_ui_.previewAddSlot.set(false);
        configureMacroEncoders();
        return;
    }

    if (!structure_clipboard_.hasMacroPage()) return;
    const uint8_t addPageIndex = static_cast<uint8_t>(std::max(
        0,
        nextMacroAddIndexAfterHighest(
            services_.pageEnabledMask(),
            core::state::macro::PAGE_COUNT
        )
    ));
    const uint8_t targetIndex = macro_ui_.previewAddSlot.get() ? addPageIndex : pages_.activePage;
    if (targetIndex >= core::state::macro::PAGE_COUNT) return;
    services_.pastePage(targetIndex, structure_clipboard_.macroPage);
    macro_ui_.previewAddSlot.set(false);
    configureMacroEncoders();
}

FLASHMEM bool MacroPerformanceHandler::canPasteCurrentStructure() const {
    if (navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK) {
        return structure_clipboard_.hasMacroTrack();
    }
    return structure_clipboard_.hasMacroPage();
}

FLASHMEM void MacroPerformanceHandler::beginHoldAction(core::state::StructureHoldAction action) {
    macro_ui_.hold.begin(action, core::time_compat::millis());
}

FLASHMEM void MacroPerformanceHandler::clearHoldAction() {
    macro_ui_.hold.clear();
}

FLASHMEM void MacroPerformanceHandler::createPreviewedStructure() {
    switch (navigation_focus_.get()) {
        case core::state::StructureNavigationFocus::TRACK:
            services_.createNextTrack();
            break;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            services_.createNextPage();
            break;
    }

    macro_ui_.previewAddSlot.set(false);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::enterSelectionMode(core::state::StructureSelectionScope scope) {
    auto& selection = macro_ui_.structureSelection;
    if (selection.active.get()) return;
    macro_ui_.previewAddSlot.set(false);

    const uint8_t cursor =
        (scope == core::state::StructureSelectionScope::TRACK)
            ? services_.activeTrack()
            : pages_.activePage;

    selection.active.set(true);
    selection.scope.set(scope);
    selection.cursorIndex.set(cursor);
    selection.selectedMask.set(0);
    navigation_focus_.set(
        scope == core::state::StructureSelectionScope::TRACK
            ? core::state::StructureNavigationFocus::TRACK
            : core::state::StructureNavigationFocus::PAGE
    );
}

FLASHMEM void MacroPerformanceHandler::cancelSelectionMode() {
    macro_ui_.previewAddSlot.set(false);
    const auto scope = macro_ui_.structureSelection.scope.get();
    const uint8_t cursor =
        (scope == core::state::StructureSelectionScope::TRACK)
            ? services_.activeTrack()
            : pages_.activePage;
    macro_ui_.structureSelection.reset(scope, cursor);
}

FLASHMEM void MacroPerformanceHandler::toggleSelectionAtCursor() {
    auto& selection = macro_ui_.structureSelection;
    if (!selection.active.get()) return;

    const uint8_t cursor = selection.cursorIndex.get();
    const bool trackScope = selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const uint16_t enabledMask =
        trackScope ? services_.trackEnabledMask() : services_.pageEnabledMask();
    const uint16_t bit = static_cast<uint16_t>(1U << cursor);
    if ((enabledMask & bit) == 0) return;

    uint16_t selectedMask = selection.selectedMask.get();
    if ((selectedMask & bit) != 0) {
        selectedMask &= static_cast<uint16_t>(~bit);
    } else {
        selectedMask |= bit;
    }
    selection.selectedMask.set(selectedMask);
}

FLASHMEM void MacroPerformanceHandler::navigateSelection(float delta) {
    auto& selection = macro_ui_.structureSelection;
    if (!selection.active.get()) return;
    if (!nav::hasTurnDelta(delta)) return;

    const bool trackScope = selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const uint16_t enabledMask =
        trackScope ? services_.trackEnabledMask() : services_.pageEnabledMask();
    const uint8_t count =
        trackScope ? core::state::macro::TRACK_COUNT : core::state::macro::PAGE_COUNT;
    const uint8_t enabledCount =
        trackScope ? countEnabledMacroTracks(enabledMask) : countEnabledMacroPages(enabledMask);
    if (enabledCount == 0) return;

    const uint8_t current = selection.cursorIndex.get();
    const uint8_t next = nextEnabledIndex(enabledMask, current, count, nav::turnStep(delta));
    selection.cursorIndex.set(next);
}

FLASHMEM void MacroPerformanceHandler::deleteSelection() {
    auto& selection = macro_ui_.structureSelection;
    if (!selection.active.get()) return;

    const bool trackScope = selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const uint16_t selectedMask = selection.selectedMask.get();
    if (selectedMask == 0) return;

    const bool changed = trackScope
        ? services_.deleteSelectedTracks(selectedMask)
        : services_.deleteSelectedPages(selectedMask);
    if (!changed) return;

    cancelSelectionMode();
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::duplicateSelection() {
    auto& selection = macro_ui_.structureSelection;
    if (!selection.active.get()) return;

    const bool trackScope = selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const uint16_t selectedMask = selection.selectedMask.get();
    if (selectedMask == 0) return;

    const bool changed = trackScope
        ? services_.duplicateSelectedTracks(selectedMask)
        : services_.duplicateSelectedPages(selectedMask);
    if (!changed) return;

    cancelSelectionMode();
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::configureMacroEncoders() {
    if (macro_ui_.quickControlsSelecting.get()) {
        configureValueEncoders();
        return;
    }

    const auto property = macro_ui_.clutchActive.get()
        ? macro_ui_.activeProperty.get()
        : core::state::macro::MacroPerformanceProperty::VALUE;

    switch (property) {
        case core::state::macro::MacroPerformanceProperty::CC:
            configureDiscreteEncoders(128);
            break;
        case core::state::macro::MacroPerformanceProperty::CHANNEL:
            configureDiscreteEncoders(16);
            break;
        case core::state::macro::MacroPerformanceProperty::VALUE:
        default:
            configureValueEncoders();
            break;
    }

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        encoders_.setPosition(Config::MACRO_ENCODERS[i], normalizedForProperty(services_, i, property));
    }
}

FLASHMEM void MacroPerformanceHandler::configureQuickControlEncoder() {
    using Item = core::state::macro::MacroQuickControlItem;

    if (macro_ui_.focusedQuickControl.get() == Item::CC_OFFSET) {
        const int itemCount = (currentCcOffsetMax() - currentCcOffsetMin()) + 1;
        configureDiscreteEncoder(Config::EncoderID::OPT, static_cast<uint8_t>(std::max(itemCount, 1)));
        encoders_.setPosition(
            Config::EncoderID::OPT,
            offsetToNormalized(macro_ui_.ccOffset.get())
        );
        return;
    }

    configureDiscreteEncoder(Config::EncoderID::OPT, 16);
    encoders_.setPosition(
        Config::EncoderID::OPT,
        input_utils::indexToNormalized(services_.activeConfig(0).channel, 16)
    );
}

FLASHMEM void MacroPerformanceHandler::resetQuickControlsState() {
    macro_ui_.focusedQuickControl.set(core::state::macro::MacroQuickControlItem::GLOBAL_CHANNEL);
    macro_ui_.ccOffset.set(0);
}

FLASHMEM int MacroPerformanceHandler::currentCcOffsetMin() const {
    int minOffset = -127;
    for (const auto& config : quick_snapshot_configs_) {
        minOffset = std::max(minOffset, -static_cast<int>(config.cc));
    }
    return minOffset;
}

FLASHMEM int MacroPerformanceHandler::currentCcOffsetMax() const {
    int maxOffset = 127;
    for (const auto& config : quick_snapshot_configs_) {
        maxOffset = std::min(maxOffset, 127 - static_cast<int>(config.cc));
    }
    return maxOffset;
}

FLASHMEM float MacroPerformanceHandler::offsetToNormalized(int offset) const {
    const int minOffset = currentCcOffsetMin();
    const int maxOffset = currentCcOffsetMax();
    if (maxOffset <= minOffset) return 0.5f;
    const int clamped = std::clamp(offset, minOffset, maxOffset);
    return static_cast<float>(clamped - minOffset) /
           static_cast<float>(maxOffset - minOffset);
}

FLASHMEM int MacroPerformanceHandler::normalizedToOffset(float normalized) const {
    const int minOffset = currentCcOffsetMin();
    const int maxOffset = currentCcOffsetMax();
    if (maxOffset <= minOffset) return 0;
    const int itemCount = (maxOffset - minOffset) + 1;
    const int index = input_utils::normalizedToIndex(std::clamp(normalized, 0.0f, 1.0f), itemCount);
    return minOffset + index;
}

FLASHMEM void MacroPerformanceHandler::applyCcOffsetFromSnapshot(int offset) const {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        const auto& snapshot = quick_snapshot_configs_[i];
        const int nextCc = std::clamp(static_cast<int>(snapshot.cc) + offset, 0, 127);
        services_.setConfig(i, snapshot.channel, static_cast<uint8_t>(nextCc));
    }
}

FLASHMEM void MacroPerformanceHandler::applyGlobalChannel(uint8_t channel) const {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        services_.setConfig(i, channel, services_.activeConfig(i).cc);
    }
}

FLASHMEM void MacroPerformanceHandler::configureValueEncoders() {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        configureNormalizedEncoder(Config::MACRO_ENCODERS[i]);
        encoders_.setContinuous(Config::MACRO_ENCODERS[i]);
    }
}

FLASHMEM void MacroPerformanceHandler::configureDiscreteEncoders(uint8_t discreteSteps) {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        configureDiscreteEncoder(Config::MACRO_ENCODERS[i], discreteSteps);
    }
}

FLASHMEM void MacroPerformanceHandler::configureNormalizedEncoder(Config::EncoderID id) {
    encoders_.setDiscreteTicksPerStep(id, input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP);
    encoders_.setNormalizedTurns(id, input_utils::DEFAULT_NORMALIZED_TURNS);
}

FLASHMEM void MacroPerformanceHandler::configureDiscreteEncoder(
    Config::EncoderID id,
    uint8_t discreteSteps
) {
    configureNormalizedEncoder(id);
    encoders_.setDiscreteSteps(id, discreteSteps);
}

}  // namespace core::handler
