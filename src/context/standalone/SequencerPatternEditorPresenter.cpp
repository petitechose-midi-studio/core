#include "context/standalone/SequencerPatternEditorPresenter.hpp"

#include <algorithm>
#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerPatternEditorOps.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone {
namespace {

namespace seq = core::state::sequencer;
namespace timeline = core::ui::sequencer;
namespace theme = ::standalone::theme;
namespace icons = ::standalone::icons;

using Field = seq::SequencerPatternEditorField;
using Layer = seq::SequencerPatternEditorLayer;
using Mode = seq::SequencerPatternEditorNavigationMode;
using RandomizeProperty = seq::SequencerPatternRandomizeProperty;

constexpr std::array<const char*, 7> FIELD_ICONS = {
    icons::LENGTH,
    icons::DIVISION,
    icons::SWING,
    icons::NOTE_PROP_NUDGE,
    icons::TRANSPORT_PLAY,
    icons::STATUS_RESUME,
    icons::STATUS_PAUSED,
};

constexpr std::array<uint32_t, 7> FIELD_COLORS = {
    theme::color::STEP_LENGTH,
    theme::color::STEP_DIVISION,
    theme::color::STEP_SWING,
    theme::color::STEP_PATTERN_NUDGE,
    theme::color::STEP_NUDGE,
    theme::color::STEP_LENGTH,
    theme::color::STEP_LENGTH,
};

constexpr std::array<uint32_t, 4> CC_LAYER_COLORS = {
    theme::color::MACRO_5,
    theme::color::MACRO_6,
    theme::color::MACRO_7,
    theme::color::MACRO_8,
};

const char* fieldName(Field field) {
    switch (field) {
        case Field::LENGTH: return "Length";
        case Field::DIVISION: return "Division";
        case Field::SWING: return "Swing";
        case Field::NUDGE: return "Pattern Nudge";
        case Field::PLAY_START: return "Play Start";
        case Field::LOOP_START: return "Loop Start";
        case Field::LOOP_END: return "Loop End";
        case Field::COUNT:
        default: return "Pattern";
    }
}

const char* fieldUnit(Field field) {
    switch (field) {
        case Field::LENGTH: return "steps";
        case Field::DIVISION: return "";
        case Field::SWING:
        case Field::NUDGE: return "%";
        case Field::PLAY_START:
        case Field::LOOP_START:
        case Field::LOOP_END: return "step";
        case Field::COUNT:
        default: return "";
    }
}

void formatFieldValue(
    std::array<char, 12>& buffer,
    const seq::SequencerState& sequencer,
    Field field
) {
    const int value = seq::patternEditorFieldValue(sequencer, field);
    switch (field) {
        case Field::DIVISION:
            std::snprintf(
                buffer.data(),
                buffer.size(),
                "1/%u",
                static_cast<unsigned>(
                    4U * sequencer.pattern.stepsPerBeat.get()
                )
            );
            return;
        case Field::SWING:
        case Field::NUDGE:
            std::snprintf(buffer.data(), buffer.size(), "%+d", value);
            return;
        case Field::PLAY_START:
        case Field::LOOP_START:
            std::snprintf(buffer.data(), buffer.size(), "%d", value + 1);
            return;
        case Field::LENGTH:
        case Field::LOOP_END:
        case Field::COUNT:
        default:
            std::snprintf(buffer.data(), buffer.size(), "%d", value);
            return;
    }
}

const char* randomizePropertyName(RandomizeProperty property) {
    switch (property) {
        case RandomizeProperty::NOTE: return "Note";
        case RandomizeProperty::VELOCITY: return "Velocity";
        case RandomizeProperty::GATE: return "Gate";
        case RandomizeProperty::NUDGE: return "Nudge";
        case RandomizeProperty::PROBABILITY: return "Chance";
    }
    return "Note";
}

uint32_t randomizePropertyColor(RandomizeProperty property) {
    switch (property) {
        case RandomizeProperty::NOTE: return theme::color::STEP_PITCH;
        case RandomizeProperty::VELOCITY: return theme::color::STEP_VELOCITY;
        case RandomizeProperty::GATE: return theme::color::STEP_GATE;
        case RandomizeProperty::NUDGE: return theme::color::STEP_NUDGE;
        case RandomizeProperty::PROBABILITY: return theme::color::STEP_CHANCE;
    }
    return theme::color::CONTENT_ACTIVE;
}

oc::note::sequencer::StepBitMask128 randomizeChangedMask(
    const seq::SequencerPatternRandomizeSession& session
) {
    oc::note::sequencer::StepBitMask128 changed{};
    if (!session.active) return changed;
    const uint8_t length = std::min<uint8_t>(
        session.preview.length,
        seq::SequencerPatternState::MAX_STEPS
    );
    for (uint16_t step = 0U; step < length; ++step) {
        if (seq::projectPatternRandomizeStep(
                session.base, session.draft, static_cast<uint8_t>(step)
            ).changed) {
            changed.setBit(static_cast<uint8_t>(step));
        }
    }
    return changed;
}

}  // namespace

FLASHMEM SequencerPatternEditorPresenter::SequencerPatternEditorPresenter(
    StateRefs state,
    core::ui::SequencerPatternEditorOverlay& overlay,
    core::ui::ContextActionStrip& actionStrip
)
    : state_(state)
    , overlay_(overlay)
    , action_strip_(actionStrip)
    , geometry_(core::app::makeExtmemUnique<
          timeline::SequencerPatternTimelineGeometry>())
    , render_scheduler_(
          core::ui::renderSchedulerDebugLabel("PatternEditor"),
          &SequencerPatternEditorPresenter::drainRender,
          this
      ) {}

FLASHMEM SequencerPatternEditorPresenter::~SequencerPatternEditorPresenter() {}

FLASHMEM bool SequencerPatternEditorPresenter::bind() {
    if (!geometry_ || !render_scheduler_.valid()) return false;
    static_watcher_.bind<&SequencerPatternEditorPresenter::requestStaticRender>(
        *this, 0, "PatternEditor.static"
    );
    bool bound = static_watcher_.watchAll(
        state_.sequencer.patternEditor.active,
        state_.sequencer.pattern.length,
        state_.sequencer.pattern.stepDataRevision,
        state_.sequencer.pattern.patternTimingRevision,
        state_.sequencer.pattern.ccLaneRevision
    );
    playhead_watcher_.bind<&SequencerPatternEditorPresenter::requestPlayheadRender>(
        *this, 1, "PatternEditor.playhead"
    );
    bound = playhead_watcher_.watchAll(
        state_.sequencer.playheadStep,
        state_.sequencer.playheadStepPhaseQ8
    ) && bound;
    return bound;
}

FLASHMEM void SequencerPatternEditorPresenter::requestStaticRender() {
    render_scheduler_.request(RENDER_STATIC | RENDER_PLAYHEAD);
}

void SequencerPatternEditorPresenter::requestPlayheadRender() {
    render_scheduler_.request(RENDER_PLAYHEAD);
}

void SequencerPatternEditorPresenter::drainRender(
    void* context,
    uint32_t flags
) {
    auto* self = static_cast<SequencerPatternEditorPresenter*>(context);
    if (self) self->renderPending(flags);
}

void SequencerPatternEditorPresenter::renderPending(uint32_t flags) {
    if ((flags & RENDER_STATIC) != 0U) renderStatic();
    if ((flags & RENDER_PLAYHEAD) != 0U &&
        (flags & RENDER_STATIC) == 0U) {
        renderPlayhead();
    }
}

FLASHMEM bool SequencerPatternEditorPresenter::ensureGeometry() {
    if (!geometry_) return false;
    const auto& editor = state_.sequencer.patternEditor;
    const auto& pattern = state_.sequencer.pattern;
    const auto* ccLanes = pattern.ccLanes.get();
    const timeline::SequencerPatternTimelineViewport viewport{
        .width = 304U,
        .height = 132U,
        .windowStartStep = editor.windowStart,
        .windowStepCount = 8U,
        .layerMask = timeline::SEQUENCER_PATTERN_TIMELINE_ALL_LAYERS,
        .ccLaneMask = 0x0FU,
    };
    const bool preview = state_.randomize.active;
    const auto previousKey = geometry_->key;
    const bool hadGeometry = geometry_revision_ != 0U;
    const bool rebuilt = preview
        ? timeline::rebuildSequencerPatternTimelineGeometry(
              state_.randomize.preview, ccLanes, viewport, *geometry_
          )
        : timeline::rebuildSequencerPatternTimelineGeometry(
              pattern, ccLanes, viewport, *geometry_
          );
    if (!rebuilt) {
        return false;
    }
    if (!hadGeometry || !(geometry_->key == previousKey)) {
        ++geometry_revision_;
        if (geometry_revision_ == 0U) ++geometry_revision_;
    }
    return true;
}

timeline::SequencerPatternTimelinePlayhead
SequencerPatternEditorPresenter::projectPlayhead() const {
    timeline::SequencerPatternTimelinePlayhead result{};
    if (!geometry_ || geometry_revision_ == 0U ||
        !state_.sequencer.patternEditor.active.get()) {
        return result;
    }
    const uint8_t phaseQ8 = state_.sequencer.playheadStepPhaseQ8.get();
    const uint16_t fraction = static_cast<uint16_t>(phaseQ8) * 257U;
    (void)timeline::projectSequencerPatternTimelinePlayheadLocalStep(
        *geometry_, state_.sequencer.playheadStep.get(), fraction, result
    );
    return result;
}

FLASHMEM void SequencerPatternEditorPresenter::renderStatic() {
    const auto& editor = state_.sequencer.patternEditor;
    if (!editor.active.get() || editor.ownerTrack != state_.tracks.activeTrackIndex() ||
        !ensureGeometry()) {
        overlay_.render({.visible = false});
        action_strip_.render({.visible = false});
        return;
    }

    const uint8_t length = state_.randomize.active
        ? state_.randomize.preview.length
        : state_.sequencer.pattern.length.get();
    const uint8_t windowEnd = static_cast<uint8_t>(std::min<uint16_t>(
        static_cast<uint16_t>(editor.windowStart) + 8U,
        length
    ));
    std::snprintf(
        title_.data(), title_.size(), "T%u · Pattern",
        static_cast<unsigned>(editor.ownerTrack + 1U)
    );
    std::snprintf(
        meta_.data(), meta_.size(), "%u steps \xC2\xB7 1/%u",
        static_cast<unsigned>(length),
        static_cast<unsigned>(
            4U * state_.sequencer.pattern.stepsPerBeat.get()
        )
    );

    const auto* bank = state_.sequencer.pattern.ccLanes.get();
    uint32_t layerColor = theme::color::CONTENT_ACTIVE;
    if (state_.randomize.active) {
        std::snprintf(
            layer_.data(), layer_.size(), "Preview \xC2\xB7 %s",
            randomizePropertyName(state_.randomize.draft.property)
        );
        layerColor = randomizePropertyColor(state_.randomize.draft.property);
    } else switch (editor.focusedLayer) {
        case Layer::NOTES:
            std::snprintf(layer_.data(), layer_.size(), "NOTES");
            layerColor = theme::color::STEP_PITCH;
            break;
        case Layer::REGION:
            std::snprintf(layer_.data(), layer_.size(), "REGION");
            layerColor = theme::color::STEP_LENGTH;
            break;
        case Layer::CC1:
        case Layer::CC2:
        case Layer::CC3:
        case Layer::CC4: {
            const uint8_t lane = static_cast<uint8_t>(editor.focusedLayer) -
                static_cast<uint8_t>(Layer::CC1);
            if (bank && bank->lanes[lane].occupied) {
                std::snprintf(
                    layer_.data(), layer_.size(), "CC %u \xC2\xB7 CC%u",
                    static_cast<unsigned>(lane + 1U),
                    static_cast<unsigned>(bank->lanes[lane].destination.controller)
                );
                layerColor = CC_LAYER_COLORS[lane];
            } else {
                std::snprintf(layer_.data(), layer_.size(), "+ CC LANE");
                layerColor = theme::color::SECONDARY;
            }
            break;
        }
        case Layer::COUNT:
        default:
            std::snprintf(layer_.data(), layer_.size(), "PATTERN");
            break;
    }

    if (state_.randomize.active) {
        // The selected field row already names the edited parameter and
        // shows its value. The helper line is reserved for the musical
        // outcome, avoiding three repetitions of the same information.
        std::snprintf(
            hint_.data(),
            hint_.size(),
            "%u/%u steps changed",
            static_cast<unsigned>(state_.randomize.summary.changedCount),
            static_cast<unsigned>(state_.randomize.summary.eligibleCount)
        );
    } else if (editor.navigationMode == Mode::WINDOWS) {
        std::snprintf(
            hint_.data(), hint_.size(), "WINDOW · STEPS %u–%u",
            static_cast<unsigned>(editor.windowStart + 1U),
            static_cast<unsigned>(windowEnd)
        );
    } else if (editor.navigationMode == Mode::LAYERS) {
        std::snprintf(hint_.data(), hint_.size(), "LAYER · %s", layer_.data());
    } else {
        const auto field = editor.focusedField;
        const int value = seq::patternEditorFieldValue(state_.sequencer, field);
        const int displayed = field == Field::PLAY_START || field == Field::LOOP_START
            ? value + 1
            : value;
        if (field == Field::DIVISION) {
            std::snprintf(
                hint_.data(), hint_.size(), "DIVISION · 1/%u",
                static_cast<unsigned>(
                    4U * state_.sequencer.pattern.stepsPerBeat.get()
                )
            );
        } else {
            std::snprintf(
                hint_.data(), hint_.size(), "%s · %d%s%s",
                fieldName(field), displayed,
                fieldUnit(field)[0] == '%' ? "" : " ",
                fieldUnit(field)
            );
        }
    }

    core::ui::SequencerPatternEditorOverlayProps props{};
    props.visible = true;
    props.title = title_.data();
    props.meta = meta_.data();
    props.layer = layer_.data();
    props.transientHint = hint_.data();
    props.layerColor = layerColor;
    props.geometry = geometry_.get();
    props.geometryRevision = geometry_revision_;
    props.playhead = projectPlayhead();
    props.focusedLayer = editor.focusedLayer;
    props.navigationMode = editor.navigationMode;
    props.randomizePreview = state_.randomize.active;
    props.randomizeProperty = state_.randomize.draft.property;
    props.randomizeChangedSteps = randomizeChangedMask(state_.randomize);
    if (state_.randomize.active) {
        props.fieldCount = 4U;
        std::snprintf(
            field_values_[0].data(), field_values_[0].size(), "%s",
            randomizePropertyName(state_.randomize.draft.property)
        );
        std::snprintf(
            field_values_[1].data(), field_values_[1].size(), "%u%%",
            static_cast<unsigned>(state_.randomize.draft.amount)
        );
        std::snprintf(
            field_values_[2].data(), field_values_[2].size(), "±%u",
            static_cast<unsigned>(state_.randomize.draft.range)
        );
        std::snprintf(
            field_values_[3].data(), field_values_[3].size(), "%s",
            state_.randomize.draft.activeOnly ? "ACTIVE" : "ALL"
        );
        constexpr std::array<const char*, 4> randomIcons = {
            icons::NOTE_PROP_RANDOM,
            icons::DIVISION,
            icons::KNOB,
            icons::NOTE,
        };
        constexpr std::array<uint32_t, 4> randomColors = {
            theme::color::STEP_CHANCE,
            theme::color::STEP_VELOCITY,
            theme::color::STEP_LENGTH,
            theme::color::TEXT_PRIMARY,
        };
        for (std::size_t index = 0U; index < 4U; ++index) {
            props.fields[index] = {
                .icon = randomIcons[index],
                .value = field_values_[index].data(),
                .color = randomColors[index],
                .selected = static_cast<std::size_t>(
                    state_.randomize.focusedField
                ) == index,
            };
        }
    } else {
        props.fieldCount = seq::patternEditorVisibleFieldCount(
            state_.sequencer
        );
        for (uint8_t index = 0U; index < props.fieldCount; ++index) {
            const auto field = seq::patternEditorVisibleFieldAt(
                state_.sequencer,
                index
            );
            formatFieldValue(field_values_[index], state_.sequencer, field);
            props.fields[index] = {
                .icon = FIELD_ICONS[static_cast<std::size_t>(field)],
                .value = field_values_[index].data(),
                .color = FIELD_COLORS[static_cast<std::size_t>(field)],
                .selected = editor.navigationMode == Mode::FIELDS &&
                    editor.focusedField == field,
            };
        }
    }
    overlay_.render(props);
    if (state_.randomize.active) {
        core::ui::ContextActionStripProps actions{.visible = true};
        actions.slots[0] = core::ui::makeStandaloneIconStripSlot(
            icons::NOTE_PROP_RANDOM,
            core::ui::ContextActionStripVisualState::ACTIVE
        );
        actions.slots[2] = core::ui::makeStandaloneIconStripSlot(
            icons::ACTION_APPLY,
            state_.randomize.summary.changedCount > 0U
                ? core::ui::ContextActionStripVisualState::ACTIVE
                : core::ui::ContextActionStripVisualState::DISABLED,
            core::ui::ContextActionStripTone::CONSTRUCTIVE
        );
        action_strip_.render(actions);
    } else {
        core::ui::ContextActionStripProps actions{.visible = true};
        actions.slots[0] = core::ui::makeStandaloneIconStripSlot(
            icons::NOTE_PROP_RANDOM,
            core::ui::ContextActionStripVisualState::AVAILABLE
        );
        actions.slots[2] = core::ui::makeStandaloneIconStripSlot(
            icons::ACTION_PLACE_TARGET,
            state_.sequencer.pattern.length.get() < seq::SequencerState::MAX_STEPS
                ? core::ui::ContextActionStripVisualState::AVAILABLE
                : core::ui::ContextActionStripVisualState::DISABLED,
            core::ui::ContextActionStripTone::CONSTRUCTIVE
        );
        action_strip_.render(actions);
    }
}

void SequencerPatternEditorPresenter::renderPlayhead() {
    if (!state_.sequencer.patternEditor.active.get()) return;
    overlay_.renderPlayhead(projectPlayhead());
}

}  // namespace core::context::standalone
