#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/modulation/ProjectCurveArena.hpp"

namespace core::state::modulation {

enum class ProjectRecordedShapeTakePhase : uint8_t {
    IDLE = 0,
    RECORDING,
};

/**
 * Allocation-free, mono-source scratch used to record one relative gesture.
 *
 * The grid is authored directly in Project phase. It is circular, keeps the
 * most recent pass at every phase, and stores the integrated encoder delta in
 * signed Q15. Destination bases are deliberately absent from this type: only
 * the source domain itself is saturated, to [-32767, 32767].
 *
 * The complete value grid lives in caller-owned state and remains trivially
 * copyable so its placement (normally EXTMEM) is explicit at the integration
 * boundary.
 */
struct ProjectRecordedShapeTake {
    static constexpr uint16_t SAMPLE_CAPACITY = 2048U;
    static constexpr int16_t SOURCE_MIN = -32767;
    static constexpr int16_t SOURCE_MAX = 32767;

    std::array<int16_t, SAMPLE_CAPACITY> values{};
    uint32_t startProjectPhaseTick = 0U;
    uint32_t latestElapsedTick = 0U;
    uint32_t lastWriteElapsedTick = 0U;
    uint32_t scratchCurveRevision = 0U;
    uint32_t prefillSearchSteps = 0U;
    uint16_t durationTicks = 0U;
    uint16_t sampleCount = 0U;
    int16_t currentValue = 0;
    int16_t lastWriteValue = 0;
    ProjectRecordedShapeTakePhase phase =
        ProjectRecordedShapeTakePhase::IDLE;
    bool touched = false;
    bool changed = false;
    bool reduced = false;
    bool prefilled = false;
    bool writeCursor = false;

    void reset();

    /** Starts a new zero-centred circular take. */
    [[nodiscard]] bool begin(uint16_t duration,
                             uint32_t projectPhaseTick);

    /**
     * Seeds the fixed grid from an existing bipolar curve before the first
     * effective input delta. The source is resampled over the take duration.
     */
    [[nodiscard]] bool prefill(const ProjectCurveSpec& source,
                               const ProjectPackedCurvePoint* points,
                               uint16_t pointCount);

    /** Integrates one relative signed-Q15 encoder delta at Project time. */
    [[nodiscard]] bool touchDelta(int32_t deltaQ15,
                                  uint32_t elapsedTick);

    /** Advances an active gesture while holding its current relative value. */
    [[nodiscard]] bool sample(uint32_t elapsedTick);

    /** Returns the circular Project-phase write head for live presentation. */
    [[nodiscard]] bool writePositionQ16(uint16_t& positionQ16) const;

    /** Samples the active fixed grid at one normalized preview position. */
    [[nodiscard]] bool samplePreviewValue(uint16_t positionQ16,
                                          int16_t& value) const;

    /**
     * Builds a native bipolar Project curve into caller-owned storage.
     * Linear simplification is performed in place while retaining a bounded
     * one-Q15-unit error. An untouched/effectively unchanged take is a no-op.
     */
    [[nodiscard]] bool buildPackedCurve(ProjectCurveSpec& spec,
                                        ProjectPackedCurvePoint* output,
                                        uint16_t capacity,
                                        uint16_t& written) const;

private:
    [[nodiscard]] uint16_t gridTick_(uint16_t sample) const;
    [[nodiscard]] int16_t valueAtElapsed_(uint32_t elapsedTick) const;
    void writeGridValue_(uint64_t absoluteGridOrdinal, int16_t value);
    [[nodiscard]] bool sampleValue_(uint32_t elapsedTick, int16_t value);
};

static_assert(std::is_trivially_copyable_v<ProjectRecordedShapeTake>);
static_assert(sizeof(ProjectRecordedShapeTake) <= 5U * 1024U);

}  // namespace core::state::modulation
