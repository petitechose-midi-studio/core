#include "handler/sequencer/SequencerChordProjectionFeedback.hpp"

#include <cstdio>

#include <config/PlatformCompat.hpp>

namespace core::handler {

FLASHMEM bool showChordProjectionFeedback(
    core::state::sequencer::SequencerHistoryFeedbackState& feedback,
    const core::state::sequencer::SequencerChordContextProjectionStats& stats,
    uint32_t nowMs
) {
    if (!stats.hasAdaptations()) return false;

    char summary[
        core::state::sequencer::SequencerHistoryFeedbackState::LINE_SIZE
    ]{};
    char detail[
        core::state::sequencer::SequencerHistoryFeedbackState::LINE_SIZE
    ]{};
    if (stats.failures != 0U) {
        std::snprintf(
            summary,
            sizeof(summary),
            "%u formula%s unchanged",
            static_cast<unsigned>(stats.failures),
            stats.failures == 1U ? "" : "s"
        );
        std::snprintf(
            detail,
            sizeof(detail),
            "Projection failed - Undo"
        );
        feedback.show(
            "Chord projection warning",
            summary,
            detail,
            nowMs
        );
        return true;
    }

    std::snprintf(
        summary,
        sizeof(summary),
        "%u formula%s adjusted",
        static_cast<unsigned>(stats.adapted),
        stats.adapted == 1U ? "" : "s"
    );
    if (stats.directionLimited != 0U) {
        std::snprintf(
            detail,
            sizeof(detail),
            "Direction relaxed - Undo"
        );
    } else if (stats.droppedVoices != 0U) {
        std::snprintf(
            detail,
            sizeof(detail),
            "%u voice%s omitted - Undo",
            static_cast<unsigned>(stats.droppedVoices),
            stats.droppedVoices == 1U ? "" : "s"
        );
    } else if (stats.rangeLimited != 0U) {
        std::snprintf(
            detail,
            sizeof(detail),
            "Range limited - Undo"
        );
    } else {
        std::snprintf(
            detail,
            sizeof(detail),
            "Nearest fit - Undo available"
        );
    }

    feedback.show(
        "Chord formulas adapted",
        summary,
        detail,
        nowMs
    );
    return true;
}

}  // namespace core::handler
