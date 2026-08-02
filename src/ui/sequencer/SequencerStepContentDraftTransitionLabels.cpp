#include "ui/sequencer/SequencerStepContentDraftTransitionLabels.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace core::ui::sequencer {

namespace {

using Transition =
    core::state::sequencer::SequencerStepContentDraftBlockedTransition;

const char STANDALONE_NONE_LABEL[] PROGMEM = "APPLY OR DISCARD DRAFT";
const char STANDALONE_TRACK_LABEL[] PROGMEM = "APPLY BEFORE CHANGING TRACK";
const char STANDALONE_VIEW_LABEL[] PROGMEM = "APPLY BEFORE CHANGING VIEW";
const char STANDALONE_PROJECT_LOAD_LABEL[] PROGMEM = "APPLY BEFORE LOADING";
const char STANDALONE_RESET_LABEL[] PROGMEM = "APPLY BEFORE RESET";
const char STANDALONE_STRUCTURE_EDIT_LABEL[] PROGMEM =
    "APPLY BEFORE STRUCTURE EDIT";
const char STANDALONE_HISTORY_LABEL[] PROGMEM = "APPLY BEFORE UNDO/REDO";

const char PROPERTY_NONE_LABEL[] PROGMEM = "Apply or discard";
const char PROPERTY_TRACK_LABEL[] PROGMEM = "Apply before track";
const char PROPERTY_VIEW_LABEL[] PROGMEM = "Apply before view";
const char PROPERTY_PROJECT_LOAD_LABEL[] PROGMEM = "Apply before load";
const char PROPERTY_RESET_LABEL[] PROGMEM = "Apply before reset";
const char PROPERTY_STRUCTURE_EDIT_LABEL[] PROGMEM = "Apply before structure edit";
const char PROPERTY_HISTORY_LABEL[] PROGMEM = "Apply before undo/redo";

constexpr std::array<const char*, 7U> STANDALONE_LABELS PROGMEM{
    STANDALONE_NONE_LABEL,
    STANDALONE_TRACK_LABEL,
    STANDALONE_VIEW_LABEL,
    STANDALONE_PROJECT_LOAD_LABEL,
    STANDALONE_RESET_LABEL,
    STANDALONE_STRUCTURE_EDIT_LABEL,
    STANDALONE_HISTORY_LABEL,
};

constexpr std::array<const char*, 7U> PROPERTY_LABELS PROGMEM{
    PROPERTY_NONE_LABEL,
    PROPERTY_TRACK_LABEL,
    PROPERTY_VIEW_LABEL,
    PROPERTY_PROJECT_LOAD_LABEL,
    PROPERTY_RESET_LABEL,
    PROPERTY_STRUCTURE_EDIT_LABEL,
    PROPERTY_HISTORY_LABEL,
};

static_assert(static_cast<uint8_t>(Transition::NONE) == 0U);
static_assert(static_cast<uint8_t>(Transition::TRACK) == 1U);
static_assert(static_cast<uint8_t>(Transition::VIEW) == 2U);
static_assert(static_cast<uint8_t>(Transition::PROJECT_LOAD) == 3U);
static_assert(static_cast<uint8_t>(Transition::RESET) == 4U);
static_assert(static_cast<uint8_t>(Transition::STRUCTURE_EDIT) == 5U);
static_assert(static_cast<uint8_t>(Transition::HISTORY) == 6U);

template <std::size_t Size>
FLASHMEM const char* transitionLabel(
    const std::array<const char*, Size>& labels,
    Transition transition
) {
    const auto index = static_cast<std::size_t>(transition);
    return labels[index < labels.size() ? index : 0U];
}

}  // namespace

FLASHMEM const char* standaloneStepContentDraftTransitionLabel(
    Transition transition
) {
    return transitionLabel(STANDALONE_LABELS, transition);
}

FLASHMEM const char* propertyOverlayStepContentDraftTransitionLabel(
    Transition transition
) {
    return transitionLabel(PROPERTY_LABELS, transition);
}

}  // namespace core::ui::sequencer
