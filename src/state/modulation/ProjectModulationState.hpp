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

/** Musical modifier applied independently to each synchronized duration. */
enum class ModulatorEnvelopeFeel : uint8_t {
    STRAIGHT = 0,
    TRIPLET,
    DOTTED,
};

enum class ModulatorEnvelopeTimeParameter : uint8_t {
    DELAY = 0,
    ATTACK,
    HOLD,
    DECAY,
    RELEASE,
    SMOOTH,
};

inline constexpr uint8_t PROJECT_MODULATOR_SOURCE_SCHEMA_VERSION = 1U;
inline constexpr uint8_t PROJECT_MODULATOR_ADSR_SCHEMA_VERSION = 2U;

inline constexpr uint16_t PROJECT_MODULATOR_ADSR_TIMING_MASK = 0x0001U;
inline constexpr uint16_t PROJECT_MODULATOR_ADSR_RETRIGGER_MASK = 0x0002U;
inline constexpr uint8_t PROJECT_MODULATOR_ADSR_CURVE_SHIFT = 2U;
inline constexpr uint16_t PROJECT_MODULATOR_ADSR_CURVE_MASK = 0x000CU;
inline constexpr uint8_t PROJECT_MODULATOR_ADSR_FEEL_SHIFT = 4U;
inline constexpr uint16_t PROJECT_MODULATOR_ADSR_FEEL_MASK = 0x0003U;

[[nodiscard]] constexpr uint8_t modulatorEnvelopeFeelShift(
    ModulatorEnvelopeTimeParameter parameter
) {
    return static_cast<uint8_t>(
        PROJECT_MODULATOR_ADSR_FEEL_SHIFT +
        2U * static_cast<uint8_t>(parameter)
    );
}

[[nodiscard]] constexpr uint16_t makeModulatorAdsrTraits(
    ModulatorTimingMode timing,
    ModulatorAdsrRetriggerMode retrigger,
    ModulatorAdsrCurve curve
) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(timing) |
        (static_cast<uint16_t>(retrigger) << 1U) |
        (static_cast<uint16_t>(curve) << PROJECT_MODULATOR_ADSR_CURVE_SHIFT)
    );
}

[[nodiscard]] constexpr ModulatorTimingMode modulatorAdsrTiming(
    uint16_t traits
) {
    return static_cast<ModulatorTimingMode>(
        traits & PROJECT_MODULATOR_ADSR_TIMING_MASK
    );
}

[[nodiscard]] constexpr ModulatorAdsrRetriggerMode modulatorAdsrRetrigger(
    uint16_t traits
) {
    return static_cast<ModulatorAdsrRetriggerMode>(
        (traits & PROJECT_MODULATOR_ADSR_RETRIGGER_MASK) >> 1U
    );
}

[[nodiscard]] constexpr ModulatorAdsrCurve modulatorAdsrCurve(
    uint16_t traits
) {
    return static_cast<ModulatorAdsrCurve>(
        (traits & PROJECT_MODULATOR_ADSR_CURVE_MASK) >>
        PROJECT_MODULATOR_ADSR_CURVE_SHIFT
    );
}

[[nodiscard]] constexpr ModulatorEnvelopeFeel modulatorAdsrFeel(
    uint16_t traits,
    ModulatorEnvelopeTimeParameter parameter
) {
    const uint8_t shift = modulatorEnvelopeFeelShift(parameter);
    return static_cast<ModulatorEnvelopeFeel>(
        (traits >> shift) & PROJECT_MODULATOR_ADSR_FEEL_MASK
    );
}

[[nodiscard]] constexpr uint16_t withModulatorAdsrTiming(
    uint16_t traits,
    ModulatorTimingMode timing
) {
    return static_cast<uint16_t>(
        (traits & ~PROJECT_MODULATOR_ADSR_TIMING_MASK) |
        static_cast<uint16_t>(timing)
    );
}

[[nodiscard]] constexpr uint16_t withModulatorAdsrRetrigger(
    uint16_t traits,
    ModulatorAdsrRetriggerMode retrigger
) {
    return static_cast<uint16_t>(
        (traits & ~PROJECT_MODULATOR_ADSR_RETRIGGER_MASK) |
        (static_cast<uint16_t>(retrigger) << 1U)
    );
}

[[nodiscard]] constexpr uint16_t withModulatorAdsrCurve(
    uint16_t traits,
    ModulatorAdsrCurve curve
) {
    return static_cast<uint16_t>(
        (traits & ~PROJECT_MODULATOR_ADSR_CURVE_MASK) |
        (static_cast<uint16_t>(curve) <<
         PROJECT_MODULATOR_ADSR_CURVE_SHIFT)
    );
}

[[nodiscard]] constexpr uint16_t withModulatorAdsrFeel(
    uint16_t traits,
    ModulatorEnvelopeTimeParameter parameter,
    ModulatorEnvelopeFeel feel
) {
    const uint8_t shift = modulatorEnvelopeFeelShift(parameter);
    const uint16_t mask = static_cast<uint16_t>(
        PROJECT_MODULATOR_ADSR_FEEL_MASK << shift
    );
    return static_cast<uint16_t>(
        (traits & ~mask) | (static_cast<uint16_t>(feel) << shift)
    );
}

inline constexpr uint16_t PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15 = 32768U;
inline constexpr uint16_t PROJECT_MODULATOR_ADSR_DEFAULT_SUSTAIN_Q15 = 22938U;

/**
 * Compact positive-domain DAHDSR payload. Durations use milliseconds in FREE
 * mode and Project control ticks in SYNC mode. `traits` deliberately uses
 * explicit masks instead of implementation-defined C++ bitfields.
 */
struct ModulatorAdsrParameters {
    uint16_t delay = 0U;
    uint16_t attack = 16U;
    uint16_t hold = 0U;
    uint16_t decay = 250U;
    uint16_t release = 500U;
    uint16_t sustainQ15 = PROJECT_MODULATOR_ADSR_DEFAULT_SUSTAIN_Q15;
    uint16_t smooth = 0U;
    uint16_t traits = makeModulatorAdsrTraits(
        ModulatorTimingMode::FREE,
        ModulatorAdsrRetriggerMode::RETRIGGER,
        ModulatorAdsrCurve::EXPONENTIAL
    );
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
    uint8_t schemaVersion = PROJECT_MODULATOR_SOURCE_SCHEMA_VERSION;
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

struct ModulationTriggerRef {
    ModulationTriggerKind kind = ModulationTriggerKind::TRANSPORT_START;
    uint8_t track = 0;
    uint8_t channel = 0;
    uint8_t data = 0;
};

/** Authored Track-owned note filter; physical Channel belongs to the event. */
struct ModulationTriggerFilter {
    ModulationTriggerKind kind = ModulationTriggerKind::TRANSPORT_START;
    uint8_t track = 0U;
    uint8_t noteMin = 0U;
    uint8_t noteMax = 127U;
};

constexpr bool operator==(
    const ModulationTriggerFilter& lhs,
    const ModulationTriggerFilter& rhs
) {
    return lhs.kind == rhs.kind && lhs.track == rhs.track &&
           lhs.noteMin == rhs.noteMin && lhs.noteMax == rhs.noteMax;
}

constexpr bool operator!=(
    const ModulationTriggerFilter& lhs,
    const ModulationTriggerFilter& rhs
) {
    return !(lhs == rhs);
}

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
    ModulationTriggerFilter trigger{};
    uint8_t velocityMin = 0U;
    uint8_t velocityMax = 127U;
    uint8_t flags = PROJECT_MODULATION_TRIGGER_FLAG_ENABLED;
    uint8_t reserved = 0U;
};

/**
 * Project-owned authored graph. Storage and availability are independent from
 * Track, Macro and UI context; only output bindings define where a source acts.
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
static_assert(sizeof(ModulatorAdsrParameters) == 16U);
static_assert(sizeof(ModulatorParameters) == 16U);
static_assert(sizeof(ModulatorSourceState) == 40U);
static_assert(sizeof(ModulationBindingState) == 20U);
static_assert(sizeof(ModulationDestinationScaleState) == 6U);
static_assert(sizeof(ModulationTriggerRef) == 4U);
static_assert(sizeof(ModulationTriggerFilter) == 4U);
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
