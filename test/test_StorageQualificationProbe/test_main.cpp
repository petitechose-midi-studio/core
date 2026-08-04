#include <cassert>
#include <cstdint>
#include <iostream>

#include "diagnostics/StorageQualificationProbe.hpp"

namespace sq = core::diagnostics::storage_qualification;

int main() {
    static_assert(sizeof(sq::StorageQualificationRecord) == 64U);
    static_assert(sq::TRACE_RECORD_CAPACITY * sizeof(sq::StorageQualificationRecord) == 524288U);

    sq::StorageQualificationRecord storage[3]{};
    sq::TraceBuffer trace(storage, 3U);

    assert(!trace.snapshot().armed);
    assert(!trace.append({}));

    trace.arm();
    auto snapshot = trace.snapshot();
    assert(snapshot.armed);
    assert(snapshot.generation == 1U);
    assert(snapshot.count == 0U);
    assert(snapshot.dropped == 0U);

    for (uint32_t i = 0; i < 3U; ++i) {
        sq::StorageQualificationRecord record{};
        record.operationId = 100U + i;
        assert(trace.append(record));
    }
    assert(!trace.append({}));
    assert(!trace.append({}));

    snapshot = trace.snapshot();
    assert(snapshot.count == 3U);
    assert(snapshot.nextSequence == 3U);
    assert(snapshot.dropped == 2U);

    sq::StorageQualificationRecord record{};
    assert(!trace.pop(record));
    trace.disarm();

    for (uint32_t i = 0; i < 3U; ++i) {
        assert(trace.pop(record));
        assert(record.sequence == i);
        assert(record.operationId == 100U + i);
    }
    assert(!trace.pop(record));
    assert(!trace.append({}));

    trace.arm();
    snapshot = trace.snapshot();
    assert(snapshot.armed);
    assert(snapshot.generation == 2U);
    assert(snapshot.count == 0U);
    assert(snapshot.nextSequence == 0U);
    assert(snapshot.dropped == 0U);

    trace.disarm();
    sq::TraceBuffer unavailable(nullptr, 0U);
    unavailable.arm();
    assert(!unavailable.snapshot().armed);
    assert(!unavailable.append({}));

    std::cout << "StorageQualificationProbe tests passed\n";
    return 0;
}
