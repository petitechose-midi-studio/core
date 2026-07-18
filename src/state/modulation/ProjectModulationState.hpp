#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/modulation/ModulationDestination.hpp"
#include "state/modulation/ModulationIds.hpp"
#include "state/modulation/ProjectCurveArena.hpp"

namespace core::state::modulation {

inline constexpr uint16_t PROJECT_MODULATOR_CAPACITY = 128;
inline constexpr uint16_t PROJECT_MODULATION_BINDING_CAPACITY = 512;
inline constexpr uint16_t PROJECT_MODULATION_TRIGGER_CAPACITY = 128;
inline constexpr uint16_t PROJECT_MODULATION_DESTINATION_SCALE_CAPACITY =
    PROJECT_MODULATION_BINDING_CAPACITY;
inline constexpr uint16_t PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15 = 32768U;
inline constexpr uint8_t PROJECT_MODULATOR_NAME_CAPACITY = 16;
inline constexpr uint32_t PROJECT_MODULATOR_FREE_PERIOD_MIN_MS = 4;
inline constexpr uint32_t PROJECT_MODULATOR_FREE_PERIOD_MAX_MS = 3600000;

enum class ModulatorKind : uint8_t {
    RECORDED_SHAPE = 0,
    LFO,
    ADSR,
};

enum class ModulatorLfoShape : uint8_t {
    SINE = 0,
    TRIANGLE,
    SAW_UP,
    SAW_DOWN,
    SQUARE,
};

/** Clock domain used by one generator. */
enum class ModulatorTimingMode : uint8_t {
    SYNC = 0,
    FREE,
};

enum class ModulatorRetriggerPolicy : uint8_t {
    FREE_RUNNING = 0,
    TRANSPORT,
    EXPLICIT_TRIGGER,
};

struct ModulatorLfoParameters {
    uint32_t periodTicks = 384;
    uint32_t freePeriodMs = 2000;
    int16_t phaseQ15 = 0;
    ModulatorLfoShape shape = ModulatorLfoShape::SINE;
    ModulatorRetriggerPolicy retrigger =
        ModulatorRetriggerPolicy::FREE_RUNNING;
    ModulatorTimingMode timing = ModulatorTimingMode::SYNC;
    std::array<uint8_t, 3> reserved{};
};

enum class ModulatorAdsrRetriggerMode : uint8_t {
    RETRIGGER = 0,
    LEGATO,
};

enum class ModulatorAdsrCurve : uint8_t {
    LINEAR = 0,
    SMOOTH,
    EXPONENTIAL,
};

inline constexpr uint16_t PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15 = 32768U;
inline constexpr uint16_t PROJECT_MODULATOR_ADSR_DEFAULT_SUSTAIN_Q15 = 22938U;

/**
 * Compact positive-domain ADSR payload. Stage durations use milliseconds in
 * FREE mode and Project control ticks in SYNC mode.
 */
struct ModulatorAdsrParameters {
    uint16_t attack = 16U;
    uint16_t decay = 250U;
    uint16_t release = 500U;
    uint16_t sustainQ15 = PROJECT_MODULATOR_ADSR_DEFAULT_SUSTAIN_Q15;
    ModulatorTimingMode timing = ModulatorTimingMode::FREE;
    ModulatorAdsrRetriggerMode retrigger =
        ModulatorAdsrRetriggerMode::RETRIGGER;
    ModulatorAdsrCurve curve = ModulatorAdsrCurve::EXPONENTIAL;
    uint8_t reserved = 0U;
};

/** Fixed-width active payload; persistence remains one 16-byte record. */
union ModulatorParameters {
    ProjectCurveId recordedCurveId;
    ModulatorLfoParameters lfo;
    ModulatorAdsrParameters adsr;
    std::array<uint8_t, 16> raw{};
};

inline constexpr uint8_t PROJECT_MODULATOR_FLAG_ENABLED = 0x01U;

struct ModulatorSourceState {
    ModulatorId id{};
    std::array<char, PROJECT_MODULATOR_NAME_CAPACITY> name{};
    ModulatorKind kind = ModulatorKind::LFO;
    uint8_t flags = PROJECT_MODULATOR_FLAG_ENABLED;
    uint8_t accent = 0;
    uint8_t schemaVersion = 1;
    ModulatorParameters parameters{};
};

/** Authored application of a source to one destination. */
enum class ModulationApplication : uint8_t {
    NATURAL = 0,
    AROUND_BASE,
    FROM_BASE,
};

/** Natural output declaration carried by the source payload. */
enum class ModulatorNaturalDomain : uint8_t {
    CENTERED = 0,
    POSITIVE,
};

/** Compact scalar transform stored in the hot runtime plan. */
enum class ResolvedModulationMapping : uint8_t {
    IDENTITY = 0,
    CENTERED_TO_POSITIVE,
    POSITIVE_TO_CENTERED,
};

[[nodiscard]] inline float applyResolvedModulationMapping(
    float value,
    ResolvedModulationMapping mapping
) {
    if (mapping == ResolvedModulationMapping::IDENTITY) return value;
    if (mapping == ResolvedModulationMapping::CENTERED_TO_POSITIVE) {
        return (value + 1.0f) * 0.5f;
    }
    if (mapping == ResolvedModulationMapping::POSITIVE_TO_CENTERED) {
        const float positive = value < 0.0f
            ? 0.0f
            : (value > 1.0f ? 1.0f : value);
        return positive * 2.0f - 1.0f;
    }
    return value;
}

enum class ModulationTransfer : uint8_t {
    LINEAR = 0,
};

inline constexpr uint8_t PROJECT_MODULATION_BINDING_FLAG_ENABLED = 0x01U;

/** Signed Q15 amount; INT16_MIN is deliberately non-canonical. */
struct ModulationBindingState {
    ModulationBindingId id{};
    ModulatorId sourceId{};
    ModulationDestination destination{};
    int16_t amountQ15 = 0;
    ModulationApplication application = ModulationApplication::NATURAL;
    ModulationTransfer transfer = ModulationTransfer::LINEAR;
    uint16_t slewMs = 0;
    uint8_t flags = PROJECT_MODULATION_BINDING_FLAG_ENABLED;
    uint8_t reserved = 0;
};

/**
 * Sparse non-unity multiplier owned by one logical destination.
 * Unity is canonical absence, so 512 records cover the 512-edge topology.
 */
struct ModulationDestinationScaleState {
    ModulationDestination destination{};
    uint16_t scaleQ15 = PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
};

enum class ModulationTriggerKind : uint8_t {
    TRANSPORT_START = 0,
    MANUAL,
    TRACK_NOTE,
};

inline constexpr uint8_t PROJECT_MODULATION_TRIGGER_ANY_CHANNEL = 0xFFU;
inline constexpr uint8_t PROJECT_MODULATION_TRIGGER_ANY_NOTE = 0xFFU;

struct ModulationTriggerRef {
    ModulationTriggerKind kind = ModulationTriggerKind::TRANSPORT_START;
    uint8_t track = 0;
    uint8_t channel = 0;
    uint8_t data = 0;
};

constexpr bool operator==(
    const ModulationTriggerRef& lhs,
    const ModulationTriggerRef& rhs
) {
    return lhs.kind == rhs.kind && lhs.track == rhs.track &&
           lhs.channel == rhs.channel && lhs.data == rhs.data;
}

constexpr bool operator!=(
    const ModulationTriggerRef& lhs,
    const ModulationTriggerRef& rhs
) {
    return !(lhs == rhs);
}

inline constexpr uint8_t PROJECT_MODULATION_TRIGGER_FLAG_ENABLED = 0x01U;

/** V1 reserves one trigger assignment per source. */
struct ModulationTriggerBindingState {
    ModulationBindingId id{};
    ModulatorId sourceId{};
    ModulationTriggerRef trigger{};
    uint8_t flags = PROJECT_MODULATION_TRIGGER_FLAG_ENABLED;
    std::array<uint8_t, 3> reserved{};
};

/**
 * Project-owned authored graph. Storage and availability are independent from
 * Track, Macro and UI context; only output bindings define where a source acts.
 * Historical Reach bytes exist solely at the persistence boundary and never
 * enter this live domain.
 */
struct ProjectModulationState {
    uint32_t nextSourceId = 1;
    uint32_t nextBindingId = 1;
    uint16_t sourceCount = 0;
    uint16_t outputBindingCount = 0;
    uint16_t triggerBindingCount = 0;
    uint16_t destinationScaleCount = 0;
    std::array<ModulatorSourceState, PROJECT_MODULATOR_CAPACITY> sources{};
    std::array<
        ModulationBindingState,
        PROJECT_MODULATION_BINDING_CAPACITY
    > outputBindings{};
    std::array<
        ModulationTriggerBindingState,
        PROJECT_MODULATION_TRIGGER_CAPACITY
    > triggerBindings{};
    std::array<
        ModulationDestinationScaleState,
        PROJECT_MODULATION_DESTINATION_SCALE_CAPACITY
    > destinationScales{};
};

static_assert(sizeof(ModulatorLfoParameters) == 16U);
static_assert(sizeof(ModulatorAdsrParameters) == 12U);
static_assert(sizeof(ModulatorParameters) == 16U);
static_assert(sizeof(ModulatorSourceState) == 40U);
static_assert(sizeof(ModulationBindingState) == 20U);
static_assert(sizeof(ModulationDestinationScaleState) == 6U);
static_assert(sizeof(ModulationTriggerRef) == 4U);
static_assert(sizeof(ModulationTriggerBindingState) == 16U);
static_assert(sizeof(ProjectModulationState) == 20496U);
static_assert(
    sizeof(ProjectModulationState) +
        sizeof(ProjectAutomationCurveDirectory) +
        sizeof(ProjectCurveArena) ==
    159516U
);
static_assert(std::is_trivially_copyable_v<ModulatorSourceState>);
static_assert(std::is_trivially_copyable_v<ModulationBindingState>);
static_assert(std::is_trivially_copyable_v<ModulationDestinationScaleState>);
static_assert(std::is_trivially_copyable_v<ProjectModulationState>);

}  // namespace core::state::modulation
