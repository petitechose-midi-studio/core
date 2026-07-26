#pragma once

#include <cstdint>

#include "state/StatusBarState.hpp"
#include "state/macro/MacroHistory.hpp"
#include "state/macro/MacroUiState.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

/**
 * Shared Recorded Shape authoring boundary used by Macro and Project inputs.
 *
 * Input handlers provide already-scaled signed-Q15 deltas. This workflow owns
 * Project-phase sampling, exact replace prefill, transaction validation, and
 * the one history-backed publication at modifier release.
 */
class ProjectRecordedShapeCaptureWorkflow {
public:
    static constexpr int16_t DEPTH_100_PERCENT_Q15 = 16384;
    static_assert(
        core::state::modulation::ProjectRecordedShapeCaptureState::
                PACKED_POINT_CAPACITY ==
            core::state::macro::RECORDED_SHAPE_HISTORY_POINT_CAPACITY
    );

    using MarkProjectMutatedFn = void (*)(void* context);

    struct StateRefs {
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState& macroUi;
        core::state::StatusBarState& statusBar;
        core::state::macro::MacroHistoryService& history;
    };

    struct Operations {
        void* context = nullptr;
        void* auditionContext = nullptr;
        MarkProjectMutatedFn markProjectMutated = nullptr;
        core::state::modulation::PublishProjectRecordedShapeAuditionFn
            publishAudition = nullptr;
        core::state::modulation::ClearProjectRecordedShapeAuditionFn
            clearAudition = nullptr;
    };

    explicit ProjectRecordedShapeCaptureWorkflow(StateRefs state);
    ProjectRecordedShapeCaptureWorkflow(StateRefs state,
                                        Operations operations);
    static ProjectRecordedShapeCaptureWorkflow fromCoreState(
        core::state::CoreState& state,
        core::state::modulation::PublishProjectRecordedShapeAuditionFn
            publishAudition = nullptr,
        core::state::modulation::ClearProjectRecordedShapeAuditionFn
            clearAudition = nullptr,
        void* auditionContext = nullptr
    );

    /** Arms a detached Project source. No live audition is published. */
    [[nodiscard]] bool armCreateUnassigned(
        uint32_t nowMs,
        uint16_t durationTicks,
        const char* name = "Recorded Shape",
        uint8_t accent = 0U,
        bool enabled = true
    ) const;

    /**
     * Arms a new source and one edge. Missing contiguous Track/Page topology
     * and a sparse physical Macro position are planned but created only at
     * release. The source is always new, even when an equivalent source exists.
     */
    [[nodiscard]] bool armCreateAssigned(
        uint32_t nowMs,
        uint16_t durationTicks,
        const core::state::macro::MacroAutomationSlotAddress& address,
        int16_t amountQ15 = DEPTH_100_PERCENT_Q15,
        const char* name = "Recorded Shape",
        uint8_t accent = 0U,
        bool enabled = true
    ) const;

    /** Arms an exact overdub of one existing Recorded Shape source. */
    [[nodiscard]] bool armReplaceExisting(
        uint32_t nowMs,
        core::state::modulation::ModulatorId sourceId
    ) const;

    /** Integrates one caller-scaled signed-Q15 encoder delta. */
    [[nodiscard]] bool touchDeltaQ15(int32_t deltaQ15, uint32_t nowMs) const;
    /** Concise spelling used by typed input adapters. */
    [[nodiscard]] bool touchDelta(int32_t deltaQ15, uint32_t nowMs) const {
        return touchDeltaQ15(deltaQ15, nowMs);
    }
    /** Establishes the cumulative RAW encoder origin without writing a point. */
    [[nodiscard]] bool configureRawEncoderOrigin(int32_t position) const;
    /**
     * Converts a cumulative RAW position without per-tick rounding drift.
     * The first call establishes the origin if the handler did not configure
     * it explicitly at RAW-mode entry.
     */
    [[nodiscard]] bool touchRawEncoder(int32_t position,
                                       uint32_t nowMs,
                                       uint16_t ticksPerFullScale = 600U) const;
    /** Advances the circular write head without allocating. */
    [[nodiscard]] bool sample(uint32_t nowMs) const;

    /** Builds the PSRAM curve and publishes one exact history action. */
    [[nodiscard]] core::state::modulation::ProjectModulationResult release(
        uint32_t nowMs
    ) const;
    /** Cancels without publishing musical or history state. */
    [[nodiscard]] bool cancel() const;

    [[nodiscard]] bool active() const;
    [[nodiscard]] core::state::modulation::ProjectRecordedShapeCaptureStatus
        status() const;
    [[nodiscard]] uint32_t revision() const;
    [[nodiscard]] core::state::modulation::ProjectModulationStatus
        lastProjectStatus() const;
    [[nodiscard]] const core::state::modulation::ProjectRecordedShapeTake*
        previewTake() const;
    [[nodiscard]] bool currentSourceValueQ15(int16_t& value) const;
    [[nodiscard]] bool auditionDescriptor(
        core::state::modulation::ProjectRecordedShapeAuditionDescriptor& out
    ) const;

private:
    struct ElapsedTime {
        uint32_t ticks = 0U;
        bool valid = false;
    };

    [[nodiscard]] bool armCreate_(
        uint32_t nowMs,
        uint16_t durationTicks,
        core::state::modulation::ProjectRecordedShapeCaptureMode mode,
        const core::state::macro::MacroAutomationSlotAddress* address,
        int16_t amountQ15,
        const char* name,
        uint8_t accent,
        bool enabled
    ) const;
    [[nodiscard]] bool beginTake_(uint32_t nowMs,
                                  uint16_t durationTicks) const;
    [[nodiscard]] ElapsedTime elapsed_(uint32_t nowMs) const;
    [[nodiscard]] bool validSession_(bool verifyCurvePoints) const;
    void publishAudition_() const;
    [[nodiscard]] core::state::modulation::ProjectModulationResult fail_(
        core::state::modulation::ProjectRecordedShapeCaptureStatus status,
        core::state::modulation::ProjectModulationStatus projectStatus
    ) const;

    core::state::macro::MacroPagesState* pages_ = nullptr;
    core::state::macro::MacroUiState* macro_ui_ = nullptr;
    core::state::StatusBarState* status_bar_ = nullptr;
    core::state::macro::MacroHistoryService* history_ = nullptr;
    Operations operations_{};
};

}  // namespace core::handler
