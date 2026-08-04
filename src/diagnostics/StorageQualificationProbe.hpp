#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/Config.hpp>

namespace core::diagnostics::storage_qualification {

inline constexpr size_t TRACE_RECORD_CAPACITY = 8192U;

enum class EventKind : uint8_t {
    None = 0,
    StoragePrimitive,
    JobAdmission,
    JobAdvance,
    JobTerminal,
    SaveToken,
    Writer,
    CounterSnapshot,
};

enum class OperationKind : uint8_t {
    None = 0,
    StorageInit,
    Stat,
    List,
    CreateDirectory,
    Remove,
    Rename,
    Read,
    Write,
    Flush,
    BeginWrite,
    AppendWrite,
    FinishWrite,
    AbortWrite,
    PersistenceJob,
    ProjectAutosave,
    StepToggle,
};

enum class PhaseKind : uint8_t {
    None = 0,
    Begin,
    End,
    Admit,
    Claim,
    Advance,
    Complete,
    Cancel,
    Expire,
    Invalidate,
};

enum SaveTokenFlag : uint32_t {
    SaveTokenFlagNone = 0U,
    SaveTokenFlagDurableMutation = 1U << 0U,
    SaveTokenFlagExplicitRequest = 1U << 1U,
    SaveTokenFlagSessionReplacement = 1U << 2U,
    SaveTokenFlagAcknowledged = 1U << 3U,
    SaveTokenFlagRecovery = 1U << 4U,
};

constexpr uint32_t saveTokenStageFlags(uint8_t stage) {
    return static_cast<uint32_t>(stage) << 8U;
}

struct StorageQualificationRecord {
    uint32_t sequence = 0;
    uint32_t timestampUs = 0;
    uint32_t durationUs = 0;
    uint32_t operationId = 0;
    uint32_t requestId = 0;
    uint32_t jobId = 0;
    uint32_t mediaGeneration = 0;
    uint32_t storageEpoch = 0;
    EventKind event = EventKind::None;
    OperationKind operation = OperationKind::None;
    PhaseKind phase = PhaseKind::None;
    uint8_t result = 0;
    uint32_t data[6] = {};
    uint8_t filesystemCalls = 0;
    uint8_t allocations = 0;
    uint8_t nodes = 0;
    uint8_t queueHighWater = 0;
};

static_assert(sizeof(StorageQualificationRecord) == 64U,
              "storage qualification record must remain exactly 64 bytes");
static_assert(alignof(StorageQualificationRecord) == 4U,
              "storage qualification record alignment drift");
static_assert(TRACE_RECORD_CAPACITY * sizeof(StorageQualificationRecord) == 524288U,
              "storage qualification trace must remain exactly 512 KiB");

struct TraceSnapshot {
    uint32_t generation = 0;
    uint32_t nextSequence = 0;
    uint32_t count = 0;
    uint32_t dropped = 0;
    bool armed = false;
};

/**
 * Fixed, allocation-free, no-overwrite trace queue.
 *
 * The owner supplies its storage. arm() intentionally clears the previous
 * cell, while disarm() freezes it for ordered export. An append while disarmed
 * is ignored; an append at capacity increments the explicit dropped counter.
 */
class TraceBuffer {
public:
    TraceBuffer(StorageQualificationRecord* storage, size_t capacity);

    void arm();
    void disarm();
    bool append(StorageQualificationRecord record);
    bool pop(StorageQualificationRecord& record);
    TraceSnapshot snapshot() const;

private:
    StorageQualificationRecord* storage_ = nullptr;
    size_t capacity_ = 0;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
    uint32_t generation_ = 0;
    uint32_t next_sequence_ = 0;
    uint32_t dropped_ = 0;
    bool armed_ = false;
};

#if defined(MS_STORAGE_QUALIFICATION)

void begin();
void update();
void foregroundBegin();
void foregroundEnd();
void timerPulse();

uint32_t beginStoragePrimitive(
    OperationKind operation,
    uint32_t mediaGeneration,
    uint32_t storageEpoch
);
void endStoragePrimitive(
    uint32_t startedAtUs,
    OperationKind operation,
    uint8_t result,
    uint32_t mediaGeneration,
    uint32_t storageEpoch,
    uint32_t bytes = 0,
    uint16_t entries = 0
);

#if OC_ENABLE_STATS

void setRequestId(uint32_t requestId);
void clearRequestId();
void recordJobAdmission(uint32_t jobId, uint8_t owner, uint8_t depth, uint8_t highWater);
void recordJobClaim(uint32_t jobId, uint8_t owner);
void recordJobAdvance(
    uint32_t jobId,
    uint8_t owner,
    uint32_t bytes,
    uint32_t wallMicros,
    uint16_t entries,
    uint8_t filesystemCalls,
    uint8_t allocations,
    uint8_t nodes,
    uint8_t queueHighWater,
    bool safeYield,
    bool quotaExceeded
);
void recordJobTerminal(uint32_t jobId, uint8_t owner, PhaseKind phase, uint8_t result);
void recordSaveToken(
    PhaseKind phase,
    uint32_t bootGeneration,
    uint32_t sessionEpoch,
    uint32_t mutationEpoch,
    uint32_t requestId,
    uint32_t modifiedCounter,
    uint8_t result,
    uint32_t flags = 0
);
void recordWriter(OperationKind operation, PhaseKind phase, uint8_t result, uint32_t detail = 0);

#else

inline void setRequestId(uint32_t) {}
inline void clearRequestId() {}
inline void recordJobAdmission(uint32_t, uint8_t, uint8_t, uint8_t) {}
inline void recordJobClaim(uint32_t, uint8_t) {}
inline void recordJobAdvance(
    uint32_t,
    uint8_t,
    uint32_t,
    uint32_t,
    uint16_t,
    uint8_t,
    uint8_t,
    uint8_t,
    uint8_t,
    bool,
    bool
) {}
inline void recordJobTerminal(uint32_t, uint8_t, PhaseKind, uint8_t) {}
inline void recordSaveToken(
    PhaseKind,
    uint32_t,
    uint32_t,
    uint32_t,
    uint32_t,
    uint32_t,
    uint8_t,
    uint32_t = 0
) {}
inline void recordWriter(OperationKind, PhaseKind, uint8_t, uint32_t = 0) {}

#endif

#else

inline void begin() {}
inline void update() {}
inline void foregroundBegin() {}
inline void foregroundEnd() {}
inline void timerPulse() {}

inline uint32_t beginStoragePrimitive(OperationKind, uint32_t, uint32_t) { return 0U; }
inline void endStoragePrimitive(
    uint32_t,
    OperationKind,
    uint8_t,
    uint32_t,
    uint32_t,
    uint32_t = 0,
    uint16_t = 0
) {}

inline void setRequestId(uint32_t) {}
inline void clearRequestId() {}
inline void recordJobAdmission(uint32_t, uint8_t, uint8_t, uint8_t) {}
inline void recordJobClaim(uint32_t, uint8_t) {}
inline void recordJobAdvance(
    uint32_t,
    uint8_t,
    uint32_t,
    uint32_t,
    uint16_t,
    uint8_t,
    uint8_t,
    uint8_t,
    uint8_t,
    bool,
    bool
) {}
inline void recordJobTerminal(uint32_t, uint8_t, PhaseKind, uint8_t) {}
inline void recordSaveToken(
    PhaseKind,
    uint32_t,
    uint32_t,
    uint32_t,
    uint32_t,
    uint32_t,
    uint8_t,
    uint32_t = 0
) {}
inline void recordWriter(OperationKind, PhaseKind, uint8_t, uint32_t = 0) {}

#endif

}  // namespace core::diagnostics::storage_qualification
