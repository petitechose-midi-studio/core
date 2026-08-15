#include "context/standalone/DrumLaneEditorPresenter.hpp"


#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "midi/MidiUtils.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/DrumLaneVisuals.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone {
namespace {

namespace seq = core::state::sequencer;
namespace icons = ::standalone::icons;
namespace theme = ::standalone::theme;

using Field = seq::DrumLaneEditorField;
using Slot = core::ui::SequencerStepEditVisualSlot;

FLASHMEM const char* fieldLabel(Field field) {
    switch (field) {
        case Field::ROLE: return "Role";
        case Field::NAME: return "Name";
        case Field::ICON: return "Icon";
        case Field::COLOR: return "Color";
        case Field::NOTE: return "MIDI note";
        case Field::POSITION: return "Position";
        case Field::COUNT:
        default: return "Lane";
    }
}

FLASHMEM const char* fieldIcon(Field field) {
    switch (field) {
        case Field::ROLE: return icons::ROUTING;
        case Field::NAME: return icons::TEXT_NAME;
        case Field::ICON: return icons::CYCLE_STATE;
        case Field::COLOR: return icons::COLOR_SWATCH;
        case Field::NOTE: return icons::NOTE_PROP_PITCH;
        case Field::POSITION: return icons::ACTION_PLACE_TARGET;
        case Field::COUNT:
        default: return icons::SETTINGS_GEAR;
    }
}

FLASHMEM uint32_t fieldColor(
    Field field,
    const seq::DrumLaneEditorState& editor
) {
    switch (field) {
        case Field::ICON:
        case Field::COLOR:
        case Field::NOTE:
            return theme::color::trackColor(
                seq::drumLaneDisplayColorIndex(editor.draft)
            );
        case Field::POSITION:
            return theme::color::STEP_NUDGE;
        case Field::NAME:
        case Field::ROLE:
        case Field::COUNT:
        default:
            return theme::color::TEXT_SECONDARY;
    }
}

FLASHMEM const char* colorLabel(uint8_t colorIndex) {
    static constexpr std::array<const char*, 8> LABELS = {
        "Red", "Orange", "Yellow", "Green",
        "Cyan", "Blue", "Purple", "Pink",
    };
    return LABELS[colorIndex % LABELS.size()];
}

FLASHMEM Slot visualSlot(Field field) {
    switch (field) {
        case Field::NAME: return Slot::STATE;
        case Field::NOTE: return Slot::CHANCE;
        case Field::ICON: return Slot::PITCH;
        case Field::COLOR: return Slot::VELOCITY;
        case Field::POSITION: return Slot::ACTION_1;
        case Field::ROLE: return Slot::ACTION_2;
        case Field::COUNT:
        default: return Slot::AUTO;
    }
}

FLASHMEM core::ui::SequencerStepEditPropertyChip propertyChip(
    Field field,
    const char* value,
    const seq::DrumLaneEditorState& editor
) {
    return {
        .key = fieldLabel(field),
        .value = value,
        .icon = field == Field::ICON
            ? core::ui::sequencer::drumLaneIconGlyph(
                  seq::drumLaneDisplayIcon(editor.draft)
              )
            : fieldIcon(field),
        .color = fieldColor(field, editor),
        .valueColor = field == Field::NOTE
            ? theme::color::trackColor(
                  seq::drumLaneDisplayColorIndex(editor.draft)
              )
            : 0U,
        .active = true,
    };
}

FLASHMEM core::ui::SequencerStepEditActionChip actionChip(
    Field field,
    const char* value,
    const seq::DrumLaneEditorState& editor
) {
    return {
        .key = fieldLabel(field),
        .value = value,
        .icon = fieldIcon(field),
        .color = fieldColor(field, editor),
    };
}

}  // namespace

FLASHMEM DrumLaneEditorPresenter::DrumLaneEditorPresenter(
    core::state::sequencer::SequencerState& sequencer,
    core::ui::SequencerStepEditOverlay& overlay,
    core::ui::interaction::TextKeyboardView& keyboard,
    core::ui::ContextActionStrip& actionStrip
)
    : sequencer_(sequencer)
    , overlay_(overlay)
    , keyboard_(keyboard)
    , action_strip_(actionStrip)
    , render_scheduler_(
          core::ui::renderSchedulerDebugLabel("DrumLaneEditor"),
          &DrumLaneEditorPresenter::drainRender,
          this
      ) {}

FLASHMEM bool DrumLaneEditorPresenter::bind() {
    if (!render_scheduler_.valid()) return false;
    requestRender();
    return true;
}

FLASHMEM void DrumLaneEditorPresenter::update() {
    const auto& drumUi = sequencer_.drumSequencer;
    const uint32_t revision = drumUi.revision.get();
    const bool visible = drumUi.laneEditor.active &&
        drumUi.selector == seq::DrumSequencerSelector::LANE_EDITOR;
    if (observed_revision_ == revision && observed_visible_ == visible) return;
    observed_revision_ = revision;
    observed_visible_ = visible;
    requestRender();
}

FLASHMEM void DrumLaneEditorPresenter::requestRender() {
    render_scheduler_.request(RENDER);
}

FLASHMEM void DrumLaneEditorPresenter::drainRender(
    void* context,
    uint32_t flags
) {
    auto* self = static_cast<DrumLaneEditorPresenter*>(context);
    if (self && (flags & RENDER) != 0U) self->render();
}

FLASHMEM void DrumLaneEditorPresenter::render() {
    const auto& drumUi = sequencer_.drumSequencer;
    const auto& editor = drumUi.laneEditor;
    const bool visible = editor.active &&
        drumUi.selector == seq::DrumSequencerSelector::LANE_EDITOR;
    observed_revision_ = drumUi.revision.get();
    observed_visible_ = visible;
    if (!visible || drumUi.drumTrack == nullptr) {
        keyboard_.setVisible(false);
        overlay_.setContentVisible(true);
        if (!sequencer_.stepEdit.visible.get()) {
            overlay_.render({.visible = false});
            action_strip_.render({.visible = false});
        }
        return;
    }

    if (editor.mode == seq::DrumLaneEditorMode::CREATE) {
        std::snprintf(badge_.data(), badge_.size(), "+");
    } else {
        std::snprintf(
            badge_.data(), badge_.size(), "L%u",
            static_cast<unsigned>(editor.sourceLane + 1U)
        );
    }
    std::snprintf(
        title_.data(), title_.size(), "%s",
        seq::drumLaneDisplayName(editor.draft)
    );
    std::array<char, 8> noteName{};
    core::midi::formatNoteName(
        noteName.data(), noteName.size(), editor.draft.midiNote
    );
    const bool moving = editor.mode == seq::DrumLaneEditorMode::EDIT &&
        editor.targetLane != editor.sourceLane;
    auto value = [this](Field field) -> char* {
        return values_[static_cast<size_t>(field)].data();
    };
    std::snprintf(value(Field::ROLE), 16, "%s", seq::drumLaneRoleLabel(editor.draft.role));
    std::snprintf(value(Field::NAME), 16, "%s", seq::drumLaneDisplayName(editor.draft));
    std::snprintf(
        value(Field::ICON),
        16,
        "%s",
        seq::drumLaneIconLabel(seq::drumLaneDisplayIcon(editor.draft))
    );
    std::snprintf(
        value(Field::COLOR),
        16,
        "%s",
        colorLabel(seq::drumLaneDisplayColorIndex(editor.draft))
    );
    std::snprintf(
        value(Field::NOTE), 16, "%s \xC2\xB7 %u",
        noteName.data(), static_cast<unsigned>(editor.draft.midiNote)
    );
    if (editor.mode == seq::DrumLaneEditorMode::CREATE) {
        std::snprintf(
            value(Field::POSITION), 16, "Insert L%u",
            static_cast<unsigned>(editor.targetLane + 1U)
        );
    } else if (moving) {
        std::snprintf(
            value(Field::POSITION), 16, "L%u > L%u",
            static_cast<unsigned>(editor.sourceLane + 1U),
            static_cast<unsigned>(editor.targetLane + 1U)
        );
    } else {
        std::snprintf(
            value(Field::POSITION), 16, "Lane %u",
            static_cast<unsigned>(editor.targetLane + 1U)
        );
    }

    core::ui::SequencerStepEditOverlayProps props{
        .visible = true,
        .stepBadge = badge_.data(),
        .title = title_.data(),
        .meta = "",
        .focusLabel = fieldLabel(editor.field),
        .primaryRowLayout =
            core::ui::SequencerStepEditPrimaryRowLayout::PRIMARY_WIDE,
        .enabled = true,
        .actionsVisible = true,
        .dataRevision = drumUi.revision.get(),
        .selectedIndex = static_cast<int>(editor.field),
        .selectedVisualSlot = visualSlot(editor.field),
        .stepBadgeColor = theme::color::trackColor(
            seq::drumLaneDisplayColorIndex(editor.draft)
        ),
    };
    props.state = propertyChip(Field::NAME, value(Field::NAME), editor);
    props.properties[0] = propertyChip(Field::ICON, value(Field::ICON), editor);
    props.properties[1] = propertyChip(Field::COLOR, value(Field::COLOR), editor);
    props.properties[4] = propertyChip(Field::NOTE, value(Field::NOTE), editor);
    props.actions[1] = actionChip(Field::POSITION, value(Field::POSITION), editor);
    props.actions[2] = actionChip(Field::ROLE, value(Field::ROLE), editor);
    overlay_.render(props);

    if (editor.textEditing) {
        overlay_.setContentVisible(false);
        keyboard_.render({
            .visible = true,
            .title = "LANE NAME",
            .meta = badge_.data(),
            .name = editor.draft.name.data(),
            .selectedKey = editor.textKeyIndex,
            .shiftActive = editor.textShiftActive,
        });
        action_strip_.render(
            core::ui::interaction::TextKeyboardView::
                bottomActionStripProps(true, false)
        );
        return;
    }
    keyboard_.setVisible(false);
    overlay_.setContentVisible(true);

    core::ui::ContextActionStripProps actions{.visible = true};
    if (editor.mode == seq::DrumLaneEditorMode::EDIT) {
        actions.slots[0] = core::ui::makeStandaloneIconStripSlot(
            icons::ACTION_REMOVE,
            core::ui::ContextActionStripVisualState::AVAILABLE,
            core::ui::ContextActionStripTone::DESTRUCTIVE
        );
    }
    actions.slots[1].visualState =
        core::ui::ContextActionStripVisualState::HIDDEN;
    actions.slots[2] = core::ui::makeStandaloneIconStripSlot(
        icons::ACTION_VALIDATE,
        core::ui::ContextActionStripVisualState::AVAILABLE,
        core::ui::ContextActionStripTone::POSITIVE
    );
    action_strip_.render(actions);
}

}  // namespace core::context::standalone
