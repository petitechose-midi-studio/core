#include "handler/sequencer/SequencerPatternEditorHandler.hpp"

#include <algorithm>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "state/sequencer/SequencerPatternEditorOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::handler {
namespace {

namespace input_utils = core::handler::sequencer::input_utils;
using Field = core::state::sequencer::SequencerPatternEditorField;
using Mode = core::state::sequencer::SequencerPatternEditorNavigationMode;

FLASHMEM int16_t normalizedToRange(
    float normalized,
    core::state::sequencer::SequencerPatternEditorValueRange range
) {
    if (!range.editable()) return 0;
    const int index = input_utils::normalizedToIndex(
        normalized,
        static_cast<int>(range.count())
    );
    return static_cast<int16_t>(range.minimum + index);
}

FLASHMEM float rangeValueToNormalized(
    int16_t value,
    core::state::sequencer::SequencerPatternEditorValueRange range
) {
    if (!range.editable()) return 0.0f;
    return input_utils::indexToNormalized(
        std::clamp<int>(value, range.minimum, range.maximum) - range.minimum,
        static_cast<int>(range.count())
    );
}

FLASHMEM bool requiresFullHistory(Field field) {
    return field == Field::LENGTH;
}

FLASHMEM int32_t normalizedToRandomizeRange(
    float normalized,
    core::state::sequencer::SequencerPatternRandomizeValueRange range
) {
    const uint32_t count = range.count();
    if (count == 0U) return range.minimum;
    const int index = input_utils::normalizedToIndex(
        normalized,
        static_cast<int>(count)
    );
    return range.minimum + index;
}

FLASHMEM float randomizeValueToNormalized(
    int32_t value,
    core::state::sequencer::SequencerPatternRandomizeValueRange range
) {
    const uint32_t count = range.count();
    if (count == 0U) return 0.0f;
    return input_utils::indexToNormalized(
        static_cast<int>(std::clamp(value, range.minimum, range.maximum) -
                         range.minimum),
        static_cast<int>(count)
    );
}

}  // namespace

FLASHMEM SequencerPatternEditorHandler::SequencerPatternEditorHandler(
    StateRefs state,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID sequencerViewScope,
    oc::type::ScopeID overlayScope
)
    : sequencer_(state.sequencer)
    , tracks_(state.tracks)
    , randomize_(state.randomize)
    , history_(state.history)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , sequencer_view_scope_(sequencerViewScope)
    , overlay_scope_(overlayScope) {
    setupBindings();
}

FLASHMEM void SequencerPatternEditorHandler::setupBindings() {
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(overlay_scope_)
        .when([this]() {
            return sequencer_.patternEditor.active.get() && ownsActiveTrack();
        })
        .then([this](float delta) { navigate(delta); });

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(overlay_scope_)
        .when([this]() {
            return sequencer_.patternEditor.active.get() && ownsActiveTrack() &&
                   (randomize_.active ||
                    sequencer_.patternEditor.navigationMode == Mode::FIELDS);
        })
        .then([this](float normalized) { setFocusedValue(normalized); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .press()
        .scope(overlay_scope_)
        .when([this]() {
            return sequencer_.patternEditor.active.get() && ownsActiveTrack() &&
                   !randomize_.active &&
                   sequencer_.patternEditor.navigationMode == Mode::FIELDS;
        })
        .then([this]() { beginWindowSelection(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(overlay_scope_)
        .when([this]() {
            return sequencer_.patternEditor.active.get() &&
                   sequencer_.patternEditor.navigationMode == Mode::WINDOWS;
        })
        .then([this]() { endWindowSelection(); });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .press()
        .scope(overlay_scope_)
        .when([this]() {
            return sequencer_.patternEditor.active.get() && ownsActiveTrack() &&
                   !randomize_.active &&
                   sequencer_.patternEditor.navigationMode == Mode::FIELDS;
        })
        .then([this]() { beginLayerSelection(); });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .release()
        .scope(overlay_scope_)
        .when([this]() {
            return sequencer_.patternEditor.active.get() &&
                   sequencer_.patternEditor.navigationMode == Mode::LAYERS;
        })
        .then([this]() { endLayerSelection(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(overlay_scope_)
        .when([this]() { return sequencer_.patternEditor.active.get(); })
        .then([this]() {
            if (randomize_.active) {
                cancelRandomize();
            } else {
                close();
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(overlay_scope_)
        .when([this]() {
            return sequencer_.patternEditor.active.get() && ownsActiveTrack() &&
                   !randomize_.active &&
                   sequencer_.patternEditor.navigationMode == Mode::FIELDS;
        })
        .then([this]() { openRandomize(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(overlay_scope_)
        .when([this]() {
            return sequencer_.patternEditor.active.get() && ownsActiveTrack() &&
                   randomize_.active;
        })
        .then([this]() { rerollRandomize(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(overlay_scope_)
        .when([this]() {
            return sequencer_.patternEditor.active.get() && ownsActiveTrack() &&
                   ((randomize_.active &&
                     randomize_.summary.changedCount > 0U) ||
                    (!randomize_.active &&
                     sequencer_.patternEditor.navigationMode == Mode::FIELDS &&
                     sequencer_.pattern.length.get() <
                         core::state::sequencer::SequencerState::MAX_STEPS));
        })
        .then([this]() {
            if (randomize_.active) {
                applyRandomize();
            } else {
                addPage();
            }
        });
}

FLASHMEM bool SequencerPatternEditorHandler::openFromCurrentPage() {
    history_.commitCoalescedPatternEdit();
    discardPendingEdit();
    randomize_.cancel();
    if (!core::state::sequencer::openPatternEditor(
            sequencer_,
            tracks_.activeTrackIndex()
        )) {
        return false;
    }
    overlays_.show(core::ui::OverlayType::SEQ_PATTERN_EDIT);
    configureOptForFocusedField();
    return true;
}

FLASHMEM void SequencerPatternEditorHandler::close() {
    if (!sequencer_.patternEditor.active.get()) {
        discardPendingEdit();
        randomize_.cancel();
        return;
    }
    commitPendingEdit();
    randomize_.cancel();
    core::state::sequencer::closePatternEditor(sequencer_);
    if (overlays_.isCurrent(core::ui::OverlayType::SEQ_PATTERN_EDIT)) {
        overlays_.hide();
    }
}

FLASHMEM void SequencerPatternEditorHandler::update(uint32_t nowMs) {
    (void)nowMs;
    if (!sequencer_.patternEditor.active.get()) {
        discardPendingEdit();
        randomize_.cancel();
        if (overlays_.isCurrent(core::ui::OverlayType::SEQ_PATTERN_EDIT)) {
            overlays_.hide();
        }
        return;
    }
    if (!overlays_.isCurrent(core::ui::OverlayType::SEQ_PATTERN_EDIT)) {
        commitPendingEdit();
        randomize_.cancel();
        core::state::sequencer::closePatternEditor(sequencer_);
        return;
    }
    if (ownsActiveTrack()) return;
    // External Track navigation is authoritative.  Capture the old owner from
    // the bank (the Track switch transaction has already published it), close
    // the exact edit boundary, and never retarget the retained surface.
    commitPendingEdit();
    randomize_.cancel();
    core::state::sequencer::closePatternEditor(sequencer_);
    overlays_.hide();
}

FLASHMEM bool SequencerPatternEditorHandler::ownsActiveTrack() const {
    return sequencer_.patternEditor.ownerTrack == tracks_.activeTrackIndex();
}

FLASHMEM void SequencerPatternEditorHandler::navigate(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const int direction = nav::turnStep(delta);
    if (randomize_.active) {
        if (randomize_.moveField(direction)) {
            sequencer_.patternEditor.bump();
            configureOptForFocusedField();
        }
        return;
    }
    switch (sequencer_.patternEditor.navigationMode) {
        case Mode::WINDOWS:
            (void)core::state::sequencer::movePatternEditorWindow(
                sequencer_,
                direction
            );
            return;
        case Mode::LAYERS:
            (void)core::state::sequencer::movePatternEditorLayer(
                sequencer_,
                direction
            );
            return;
        case Mode::FIELDS:
        default:
            commitPendingEdit();
            if (core::state::sequencer::movePatternEditorField(
                    sequencer_,
                    direction
                )) {
                configureOptForFocusedField();
            }
            return;
    }
}

FLASHMEM void SequencerPatternEditorHandler::setFocusedValue(float normalized) {
    if (randomize_.active) {
        const auto range = core::state::sequencer::patternRandomizeValueRange(
            randomize_
        );
        if (randomize_.setFocusedValue(
                normalizedToRandomizeRange(normalized, range)
            )) {
            sequencer_.patternEditor.bump();
            configureOptForFocusedField();
        }
        return;
    }
    const auto field = sequencer_.patternEditor.focusedField;
    const auto range = core::state::sequencer::patternEditorValueRange(
        sequencer_,
        field
    );
    if (!range.editable()) return;
    const int16_t next = normalizedToRange(normalized, range);
    if (next == core::state::sequencer::patternEditorFieldValue(
                    sequencer_,
                    field
                )) {
        return;
    }
    if (!beginPendingEdit()) return;
    if (core::state::sequencer::setPatternEditorFieldValue(
            sequencer_,
            field,
            next
        )) {
        // Length and marker edits change another field's legal range.  Keep the
        // physical encoder exact without recomputing any timeline geometry.
        configureOptForFocusedField();
    }
}

FLASHMEM void SequencerPatternEditorHandler::beginWindowSelection() {
    commitPendingEdit();
    (void)core::state::sequencer::setPatternEditorNavigationMode(
        sequencer_,
        Mode::WINDOWS
    );
}

FLASHMEM void SequencerPatternEditorHandler::endWindowSelection() {
    (void)core::state::sequencer::setPatternEditorNavigationMode(
        sequencer_,
        Mode::FIELDS
    );
    configureOptForFocusedField();
}

FLASHMEM void SequencerPatternEditorHandler::beginLayerSelection() {
    commitPendingEdit();
    (void)core::state::sequencer::setPatternEditorNavigationMode(
        sequencer_,
        Mode::LAYERS
    );
}

FLASHMEM void SequencerPatternEditorHandler::endLayerSelection() {
    (void)core::state::sequencer::setPatternEditorNavigationMode(
        sequencer_,
        Mode::FIELDS
    );
    configureOptForFocusedField();
}

FLASHMEM void SequencerPatternEditorHandler::configureOptForFocusedField() {
    if (randomize_.active) {
        const auto range = core::state::sequencer::patternRandomizeValueRange(
            randomize_
        );
        const uint32_t count = range.count();
        encoders_.setDiscreteTicksPerStep(
            Config::EncoderID::OPT,
            input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP
        );
        encoders_.setNormalizedTurns(
            Config::EncoderID::OPT,
            input_utils::DEFAULT_NORMALIZED_TURNS
        );
        encoders_.setDiscreteSteps(
            Config::EncoderID::OPT,
            static_cast<uint8_t>(std::min<uint32_t>(count, 255U))
        );
        encoders_.setPosition(
            Config::EncoderID::OPT,
            randomizeValueToNormalized(
                core::state::sequencer::patternRandomizeFocusedValue(randomize_),
                range
            )
        );
        return;
    }
    const auto field = sequencer_.patternEditor.focusedField;
    const auto range = core::state::sequencer::patternEditorValueRange(
        sequencer_,
        field
    );
    const uint16_t count = range.count();
    encoders_.setDiscreteTicksPerStep(
        Config::EncoderID::OPT,
        input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP
    );
    encoders_.setNormalizedTurns(
        Config::EncoderID::OPT,
        input_utils::DEFAULT_NORMALIZED_TURNS
    );
    encoders_.setDiscreteSteps(
        Config::EncoderID::OPT,
        static_cast<uint8_t>(std::min<uint16_t>(count, 255U))
    );
    encoders_.setPosition(
        Config::EncoderID::OPT,
        rangeValueToNormalized(
            core::state::sequencer::patternEditorFieldValue(sequencer_, field),
            range
        )
    );
}

FLASHMEM void SequencerPatternEditorHandler::openRandomize() {
    if (randomize_.active || !ownsActiveTrack()) return;
    commitPendingEdit();
    history_.commitCoalescedPatternEdit();
    randomize_.begin(sequencer_.pattern, sequencer_.focusedStep.get());
    sequencer_.patternEditor.bump();
    configureOptForFocusedField();
}

FLASHMEM void SequencerPatternEditorHandler::cancelRandomize() {
    if (!randomize_.active) return;
    randomize_.cancel();
    sequencer_.patternEditor.bump();
    configureOptForFocusedField();
}

FLASHMEM void SequencerPatternEditorHandler::rerollRandomize() {
    if (!randomize_.reroll()) return;
    sequencer_.patternEditor.bump();
}

FLASHMEM void SequencerPatternEditorHandler::applyRandomize() {
    if (!randomize_.active || randomize_.summary.changedCount == 0U ||
        !ownsActiveTrack()) {
        return;
    }

    auto change = core::app::makeExtmemUnique<
        core::state::sequencer::SequencerHistoryPatternChange>();
    if (!change) return;

    const uint8_t owner = sequencer_.patternEditor.ownerTrack;
    change->trackIndex = owner;
    change->storage =
        core::state::sequencer::SequencerHistoryPatternStorage::FlatOnly;
    change->descriptor = {
        .kind = core::state::sequencer::SequencerHistoryActionKind::PatternRandomize,
        .trackIndex = owner,
    };
    change->before.flat = randomize_.base;
    change->before.focusedStep = randomize_.focusedStep;
    change->after.flat = randomize_.preview;
    change->after.focusedStep = randomize_.focusedStep;

    if (!history_.canRecordPattern(*change)) return;

    core::state::sequencer::applySnapshotToEditorPreservingGraph(
        sequencer_,
        randomize_.preview
    );
    core::state::sequencer::applySnapshotPreservingGraph(
        tracks_.track(owner),
        randomize_.preview
    );
    sequencer_.invalidateVariationTelemetry();
    history_.recordPreparedPattern(std::move(change));

    randomize_.cancel();
    sequencer_.patternEditor.bump();
    configureOptForFocusedField();
}

FLASHMEM void SequencerPatternEditorHandler::addPage() {
    commitPendingEdit();
    const uint8_t current = sequencer_.pattern.length.get();
    const uint8_t next = static_cast<uint8_t>(std::min<unsigned>(
        core::state::sequencer::SequencerState::MAX_STEPS,
        ((static_cast<unsigned>(current) +
          core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1U) /
             core::state::sequencer::SequencerState::STEPS_PER_PAGE +
         1U) * core::state::sequencer::SequencerState::STEPS_PER_PAGE
    ));
    sequencer_.patternEditor.focusedField = Field::LENGTH;
    if (!beginPendingEdit()) return;
    if (core::state::sequencer::setPatternEditorFieldValue(
            sequencer_, Field::LENGTH, next
        )) {
        const uint8_t newPage = static_cast<uint8_t>(
            next / core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1U
        );
        sequencer_.patternEditor.windowStart = static_cast<uint8_t>(
            newPage * core::state::sequencer::SequencerState::STEPS_PER_PAGE
        );
        sequencer_.page.set(newPage);
        sequencer_.focusedStep.set(sequencer_.patternEditor.windowStart);
        sequencer_.patternEditor.bump();
        page_add_pending_ = true;
        commitPendingEdit();
        configureOptForFocusedField();
    } else {
        discardPendingEdit();
    }
}

FLASHMEM bool SequencerPatternEditorHandler::beginPendingEdit() {
    const auto field = sequencer_.patternEditor.focusedField;
    if (edit_pending_) {
        return edit_field_ == field;
    }
    if (!ownsActiveTrack()) return false;

    history_.commitCoalescedPatternEdit();
    edit_storage_ = requiresFullHistory(field)
        ? core::state::sequencer::SequencerHistoryPatternStorage::FullGraph
        : core::state::sequencer::SequencerHistoryPatternStorage::FlatOnly;
    bool captured = true;
    if (edit_storage_ ==
        core::state::sequencer::SequencerHistoryPatternStorage::FullGraph) {
        captured = core::state::sequencer::captureHistorySnapshot(
            sequencer_,
            edit_before_
        );
    } else {
        core::state::sequencer::captureFlatHistorySnapshot(
            sequencer_,
            edit_before_
        );
    }
    if (!captured) {
        discardPendingEdit();
        return false;
    }
    edit_field_ = field;
    edit_pending_ = true;
    return true;
}

FLASHMEM void SequencerPatternEditorHandler::commitPendingEdit() {
    if (!edit_pending_) return;

    const uint8_t owner = sequencer_.patternEditor.ownerTrack;
    // History payloads live in PSRAM. Capture directly into the final change
    // object so this cold commit path never places a full Pattern snapshot on
    // the RAM1 stack.
    auto change = core::app::makeExtmemUnique<
        core::state::sequencer::SequencerHistoryPatternChange
    >();
    if (!change) {
        discardPendingEdit();
        return;
    }

    bool captured = true;
    if (edit_storage_ ==
        core::state::sequencer::SequencerHistoryPatternStorage::FullGraph) {
        captured = core::state::sequencer::captureHistorySnapshot(
            tracks_,
            sequencer_,
            owner,
            change->after
        );
    } else {
        core::state::sequencer::captureFlatHistorySnapshot(
            tracks_,
            sequencer_,
            owner,
            change->after
        );
    }

    if (!captured || core::state::sequencer::sameMusicalHistorySnapshot(
            edit_before_,
            change->after
        )) {
        discardPendingEdit();
        return;
    }

    change->trackIndex = owner;
    change->storage = edit_storage_;
    change->descriptor = {
        .kind = page_add_pending_
            ? core::state::sequencer::SequencerHistoryActionKind::PageStructure
            : core::state::sequencer::SequencerHistoryActionKind::PatternSettings,
        .trackIndex = owner,
    };
    if (page_add_pending_) {
        change->descriptor.hasValue = true;
        change->descriptor.beforeValue = static_cast<int32_t>(
            (edit_before_.flat.length +
             core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1U) /
            core::state::sequencer::SequencerState::STEPS_PER_PAGE
        );
        change->descriptor.afterValue = static_cast<int32_t>(
            (change->after.flat.length +
             core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1U) /
            core::state::sequencer::SequencerState::STEPS_PER_PAGE
        );
    }
    change->before = std::move(edit_before_);
    edit_pending_ = false;
    page_add_pending_ = false;
    (void)history_.recordPattern(std::move(change));
    edit_before_.reset();
}

FLASHMEM void SequencerPatternEditorHandler::discardPendingEdit() {
    edit_pending_ = false;
    page_add_pending_ = false;
    edit_storage_ =
        core::state::sequencer::SequencerHistoryPatternStorage::FlatOnly;
    edit_field_ = Field::LENGTH;
    edit_before_.reset();
}

}  // namespace core::handler
