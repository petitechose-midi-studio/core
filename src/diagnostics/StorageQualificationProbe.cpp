#include "diagnostics/StorageQualificationProbe.hpp"

#if defined(MS_STORAGE_QUALIFICATION)

#include <algorithm>
#include <limits>

#include <config/PlatformCompat.hpp>

#if defined(ARDUINO)
#include "diagnostics/MemoryFootprintReporter.hpp"
#include <oc/hal/teensy/QualificationTelemetry.hpp>
#include <oc/log/Log.hpp>
#else
#include <chrono>
#endif

namespace core::diagnostics::storage_qualification {

namespace {

#if OC_ENABLE_STATS

struct ActiveContext {
    uint32_t operationId = 0U;
    uint32_t requestId = 0U;
    uint32_t jobId = 0U;
};

FLASHMEM uint32_t saturatingIncrement(uint32_t value) {
    return value == std::numeric_limits<uint32_t>::max() ? value : value + 1U;
}
#endif

#if OC_ENABLE_STATS
uint32_t nowMicros() {
#if defined(ARDUINO)
    return micros();
#else
    using Clock = std::chrono::steady_clock;
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch()
        ).count()
    );
#endif
}
#endif

#if OC_ENABLE_STATS

alignas(StorageQualificationRecord)
EXTMEM StorageQualificationRecord traceStorage[TRACE_RECORD_CAPACITY];
DMAMEM TraceBuffer traceBuffer(traceStorage, TRACE_RECORD_CAPACITY);
DMAMEM ActiveContext activeContext{};
DMAMEM bool previousGate = false;
DMAMEM bool exporting = false;
DMAMEM uint32_t exportGeneration = 0;
DMAMEM uint32_t exportInitialCount = 0;
DMAMEM uint32_t exportInitialDropped = 0;
DMAMEM uint32_t exportCount = 0;

FLASHMEM void appendCounterSnapshot(PhaseKind phase) {
#if defined(ARDUINO)
    const auto counters = oc::hal::teensy::qualification::snapshot();

    StorageQualificationRecord first{};
    first.timestampUs = nowMicros();
    first.event = EventKind::CounterSnapshot;
    first.phase = phase;
    first.data[0] = counters.foregroundTicks;
    first.data[1] = counters.storagePrimitives;
    first.data[2] = counters.timerEntries;
    first.data[3] = counters.midiInputs;
    first.data[4] = counters.midiOutputs;
    first.data[5] = counters.midiOutputDrops;
    (void)traceBuffer.append(first);

    StorageQualificationRecord second{};
    second.timestampUs = first.timestampUs;
    second.event = EventKind::CounterSnapshot;
    second.phase = phase;
    second.result = 1U;
    second.data[0] = counters.clockInputs;
    second.data[1] = counters.clockOutputs;
    second.data[2] = counters.noteOffInputs;
    second.data[3] = counters.noteOffOutputs;
    second.data[4] = counters.displaySubmissions;
    (void)traceBuffer.append(second);

    const auto memory = core::diagnostics::dynamicMemorySnapshot();
    StorageQualificationRecord third{};
    third.timestampUs = first.timestampUs;
    third.event = EventKind::CounterSnapshot;
    third.phase = phase;
    third.result = static_cast<uint8_t>(
        (memory.trackerReady ? 1U : 0U) |
        (memory.trackerOverflow ? 2U : 0U)
    );
    third.data[0] = memory.psramFreeBytes;
    third.data[1] = memory.psramLargestBlock;
    third.data[2] = memory.psramAllocatedBytes;
    third.data[3] = memory.psramUserBytes;
    third.data[4] = memory.psramBlocks;
    third.data[5] = memory.psramAllocationFailures;
    (void)traceBuffer.append(third);
#else
    (void)phase;
#endif
}

FLASHMEM void appendRecord(StorageQualificationRecord record) {
    record.timestampUs = record.timestampUs == 0U ? nowMicros() : record.timestampUs;
    record.operationId = record.operationId == 0U
        ? activeContext.operationId
        : record.operationId;
    record.requestId = record.requestId == 0U
        ? activeContext.requestId
        : record.requestId;
    record.jobId = record.jobId == 0U ? activeContext.jobId : record.jobId;
    (void)traceBuffer.append(record);
}

#if defined(ARDUINO)
const char kRecordLineA[] PROGMEM =
    "[SQ1:A] gen={} seq={} ts={} dur={} event={} op={} phase={} result={} "
    "opid={} req={} job={} media={} epoch={}";
const char kRecordLineB[] PROGMEM =
    "[SQ1:B] gen={} seq={} d0={} d1={} d2={} d3={} d4={} d5={} fs={} alloc={} nodes={} q={}";
const char kCaptureStartLine[] PROGMEM =
    "[SQ1:S] gen={} records={} dropped={}";
const char kCaptureEndLine[] PROGMEM =
    "[SQ1:E] gen={} records={} exported={} dropped={}";

FLASHMEM void exportRecord(const StorageQualificationRecord& record) {
    OC_LOG_INFO(
        kRecordLineA,
        exportGeneration,
        record.sequence,
        record.timestampUs,
        record.durationUs,
        static_cast<uint8_t>(record.event),
        static_cast<uint8_t>(record.operation),
        static_cast<uint8_t>(record.phase),
        record.result,
        record.operationId,
        record.requestId,
        record.jobId,
        record.mediaGeneration,
        record.storageEpoch
    );
    OC_LOG_INFO(
        kRecordLineB,
        exportGeneration,
        record.sequence,
        record.data[0],
        record.data[1],
        record.data[2],
        record.data[3],
        record.data[4],
        record.data[5],
        record.filesystemCalls,
        record.allocations,
        record.nodes,
        record.queueHighWater
    );
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM void updateCold(bool gate, bool previousGateValue) {
    if (gate && !previousGateValue) {
        exporting = false;
        exportCount = 0U;
        traceBuffer.arm();
        appendCounterSnapshot(PhaseKind::Begin);
    } else if (!gate && previousGateValue) {
        appendCounterSnapshot(PhaseKind::End);
        const auto snapshot = traceBuffer.snapshot();
        traceBuffer.disarm();
        exportGeneration = snapshot.generation;
        exportInitialCount = snapshot.count;
        exportInitialDropped = snapshot.dropped;
        exportCount = 0U;
        exporting = true;
        OC_LOG_INFO(
            kCaptureStartLine,
            exportGeneration,
            exportInitialCount,
            exportInitialDropped
        );
    }

    if (!gate && exporting) {
        StorageQualificationRecord record{};
        if (traceBuffer.pop(record)) {
            exportRecord(record);
            exportCount = saturatingIncrement(exportCount);
        } else {
            OC_LOG_INFO(
                kCaptureEndLine,
                exportGeneration,
                exportInitialCount,
                exportCount,
                exportInitialDropped
            );
            exporting = false;
        }
    }
}
#endif

#endif  // OC_ENABLE_STATS

}  // namespace

#if OC_ENABLE_STATS

FLASHMEM TraceBuffer::TraceBuffer(StorageQualificationRecord* storage, size_t capacity)
    : storage_(storage), capacity_(capacity) {}

FLASHMEM void TraceBuffer::arm() {
    head_ = 0U;
    tail_ = 0U;
    count_ = 0U;
    next_sequence_ = 0U;
    dropped_ = 0U;
    generation_ = saturatingIncrement(generation_);
    armed_ = storage_ != nullptr && capacity_ > 0U;
}

FLASHMEM void TraceBuffer::disarm() {
    armed_ = false;
}

FLASHMEM bool TraceBuffer::append(StorageQualificationRecord record) {
    if (!armed_ || storage_ == nullptr || capacity_ == 0U) return false;
    if (count_ >= capacity_) {
        dropped_ = saturatingIncrement(dropped_);
        return false;
    }

    record.sequence = next_sequence_;
    next_sequence_ = saturatingIncrement(next_sequence_);
    storage_[tail_] = record;
    tail_ = (tail_ + 1U) % capacity_;
    ++count_;
    return true;
}

FLASHMEM bool TraceBuffer::pop(StorageQualificationRecord& record) {
    if (armed_ || storage_ == nullptr || count_ == 0U) return false;
    record = storage_[head_];
    head_ = (head_ + 1U) % capacity_;
    --count_;
    return true;
}

FLASHMEM TraceSnapshot TraceBuffer::snapshot() const {
    return {
        .generation = generation_,
        .nextSequence = next_sequence_,
        .count = static_cast<uint32_t>(count_),
        .dropped = dropped_,
        .armed = armed_,
    };
}

#endif

FLASHMEM void begin() {
#if defined(ARDUINO)
    oc::hal::teensy::qualification::begin();
#endif

#if OC_ENABLE_STATS
    // DMAMEM is NOLOAD on Teensy. Reconstruct every byte of the runtime state
    // at boot instead of depending on startup zeroing.
    traceBuffer = TraceBuffer(traceStorage, TRACE_RECORD_CAPACITY);
    activeContext = {};
    previousGate = false;
    exporting = false;
    exportGeneration = 0U;
    exportInitialCount = 0U;
    exportInitialDropped = 0U;
    exportCount = 0U;
#if defined(ARDUINO)
    previousGate = oc::hal::teensy::qualification::captureGateActive();
#else
    previousGate = false;
#endif
    if (previousGate) {
        traceBuffer.arm();
        appendCounterSnapshot(PhaseKind::Begin);
    }
#endif
}

void update() {
#if OC_ENABLE_STATS && defined(ARDUINO)
    const bool gate = oc::hal::teensy::qualification::captureGateActive();
    const bool previousGateValue = previousGate;
    previousGate = gate;
    if (gate != previousGateValue || (!gate && exporting)) {
        updateCold(gate, previousGateValue);
    }
#endif
}

void foregroundBegin() {
#if defined(ARDUINO)
    oc::hal::teensy::qualification::foregroundBegin();
#endif
}

void foregroundEnd() {
#if defined(ARDUINO)
    oc::hal::teensy::qualification::foregroundEnd();
#endif
}

void timerPulse() {
#if defined(ARDUINO)
    oc::hal::teensy::qualification::timerPulse();
#endif
}

uint32_t beginStoragePrimitive(
    OperationKind,
    uint32_t,
    uint32_t
) {
#if defined(ARDUINO)
    oc::hal::teensy::qualification::storageBegin();
#endif
#if OC_ENABLE_STATS
    return nowMicros();
#else
    return 0U;
#endif
}

void endStoragePrimitive(
    uint32_t startedAtUs,
    OperationKind operation,
    uint8_t result,
    uint32_t mediaGeneration,
    uint32_t storageEpoch,
    uint32_t bytes,
    uint16_t entries
) {
#if OC_ENABLE_STATS
    const uint32_t endedAtUs = nowMicros();
#endif
#if defined(ARDUINO)
    oc::hal::teensy::qualification::storageEnd();
#endif

#if OC_ENABLE_STATS
    StorageQualificationRecord record{};
    record.timestampUs = startedAtUs;
    record.durationUs = endedAtUs - startedAtUs;
    record.mediaGeneration = mediaGeneration;
    record.storageEpoch = storageEpoch;
    record.event = EventKind::StoragePrimitive;
    record.operation = operation;
    record.phase = PhaseKind::End;
    record.result = result;
    record.data[0] = bytes;
    record.data[1] = entries;
    record.filesystemCalls = 1U;
    appendRecord(record);
#else
    (void)operation;
    (void)result;
    (void)mediaGeneration;
    (void)storageEpoch;
    (void)bytes;
    (void)entries;
#endif
}

#if OC_ENABLE_STATS

FLASHMEM void setRequestId(uint32_t requestId) {
    activeContext.requestId = requestId;
}

FLASHMEM void clearRequestId() {
    activeContext.requestId = 0U;
}

FLASHMEM void recordJobAdmission(
    uint32_t jobId,
    uint8_t owner,
    uint8_t depth,
    uint8_t highWater
) {
    StorageQualificationRecord record{};
    record.operationId = jobId;
    record.jobId = jobId;
    record.event = EventKind::JobAdmission;
    record.operation = OperationKind::PersistenceJob;
    record.phase = PhaseKind::Admit;
    record.data[0] = owner;
    record.data[1] = depth;
    record.queueHighWater = highWater;
    appendRecord(record);
}

FLASHMEM void recordJobClaim(uint32_t jobId, uint8_t owner) {
    activeContext.operationId = jobId;
    activeContext.jobId = jobId;
    StorageQualificationRecord record{};
    record.operationId = jobId;
    record.jobId = jobId;
    record.event = EventKind::JobAdvance;
    record.operation = OperationKind::PersistenceJob;
    record.phase = PhaseKind::Claim;
    record.data[0] = owner;
    appendRecord(record);
}

FLASHMEM void recordJobAdvance(
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
) {
    StorageQualificationRecord record{};
    record.operationId = jobId;
    record.jobId = jobId;
    record.durationUs = wallMicros;
    record.event = EventKind::JobAdvance;
    record.operation = OperationKind::PersistenceJob;
    record.phase = PhaseKind::Advance;
    record.result = quotaExceeded ? 1U : 0U;
    record.data[0] = owner;
    record.data[1] = bytes;
    record.data[2] = entries;
    record.data[3] = safeYield ? 1U : 0U;
    record.filesystemCalls = filesystemCalls;
    record.allocations = allocations;
    record.nodes = nodes;
    record.queueHighWater = queueHighWater;
    appendRecord(record);
    activeContext.operationId = 0U;
    activeContext.jobId = 0U;
    activeContext.requestId = 0U;
}

FLASHMEM void recordJobTerminal(
    uint32_t jobId,
    uint8_t owner,
    PhaseKind phase,
    uint8_t result
) {
    StorageQualificationRecord record{};
    record.operationId = jobId;
    record.jobId = jobId;
    record.event = EventKind::JobTerminal;
    record.operation = OperationKind::PersistenceJob;
    record.phase = phase;
    record.result = result;
    record.data[0] = owner;
    appendRecord(record);
    if (activeContext.jobId == jobId) activeContext = {};
}

FLASHMEM void recordSaveToken(
    PhaseKind phase,
    uint32_t bootGeneration,
    uint32_t sessionEpoch,
    uint32_t mutationEpoch,
    uint32_t requestId,
    uint32_t modifiedCounter,
    uint8_t result,
    uint32_t flags
) {
    StorageQualificationRecord record{};
    record.requestId = requestId;
    record.event = EventKind::SaveToken;
    record.operation = OperationKind::ProjectAutosave;
    record.phase = phase;
    record.result = result;
    record.data[0] = bootGeneration;
    record.data[1] = sessionEpoch;
    record.data[2] = mutationEpoch;
    record.data[3] = requestId;
    record.data[4] = modifiedCounter;
    record.data[5] = flags;
    appendRecord(record);
}

FLASHMEM void recordWriter(
    OperationKind operation,
    PhaseKind phase,
    uint8_t result,
    uint32_t detail
) {
    StorageQualificationRecord record{};
    record.event = EventKind::Writer;
    record.operation = operation;
    record.phase = phase;
    record.result = result;
    record.data[0] = detail;
    appendRecord(record);
}

#endif

}  // namespace core::diagnostics::storage_qualification

#endif
