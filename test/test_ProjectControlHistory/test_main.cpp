#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstring>
#include <iostream>

#include "state/modulation/ProjectControlHistory.hpp"

using namespace core::state::modulation;

static void checkPrefixBoundaries() {
    auto before = std::make_unique<ProjectControlDomainState>();
    auto after = std::make_unique<ProjectControlDomainState>();
    auto live = std::make_unique<ProjectControlDomainState>();
    for (const bool detached : {false, true}) {
        ProjectControlHistory history;
        // Reuse one history with growing/shrinking prefixes and an empty delta.
        for (const int edit : {4, 3, 2, 1, 0, 1, 3}) {
            std::memcpy(after.get(), before.get(), sizeof(*after));
            if (edit == 1) after->automation.entryCount = 1U;
            if (edit == 2) after->curves.points[10000] = {0x0102U, 0x0304};
            if (edit == 3) after->curves.points.back().value = 0x0100;
            if (edit == 4) after->curves.points.fill({0x0102U, 0x0304});
            assert(history.prepare(*before));
            if (detached) std::memcpy(history.candidate(), after.get(), sizeof(*after));
            core::app::testing::ScopedExtmemAllocationFailure fail(1U);
            assert(detached ? history.sealCandidate(*before) : history.captureAfter(*after));
            assert(history.changed() == (edit != 0));
            std::memcpy(live.get(), after.get(), sizeof(*live));
            for (int cycle = 0; cycle < 3; ++cycle) {
                assert(history.matches(*live, true));
                history.apply(*live);
                assert(std::memcmp(live.get(), before.get(), sizeof(*live)) == 0);
                assert(history.matches(*live, false));
                history.apply(*live);
                assert(std::memcmp(live.get(), after.get(), sizeof(*live)) == 0);
            }
            // The integrity guard still covers the suffix excluded from XOR.
            live->curves.points.back().value ^= 1;
            assert(!history.matches(*live, true));
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
    }
}

int main() {
    checkPrefixBoundaries();
    auto before = std::make_unique<ProjectControlDomainState>();
    before->curves.points.back() = {257U, -1200};
    auto after = std::make_unique<ProjectControlDomainState>(*before);
    after->curves.nextCurveId = 99U;
    after->curves.points.front() = {256U, 2200};
    after->curves.points.back() = {511U, -3300};

    for (const bool detached : {false, true}) {
        ProjectControlHistory history;
        assert(!history.ready() && !history.hasStorage());
        assert(!history.captureAfter(*after));
        assert(!history.sealCandidate(*before));
        assert(!history.matches(*before, false));
        {
            core::app::testing::ScopedExtmemAllocationFailure fail(1U);
            assert(!history.prepare(*before));
            assert(!history.hasStorage());
        }
        assert(history.prepare(*before));
        assert(history.candidate() != nullptr);
        if (detached) *history.candidate() = *after;
        {
            core::app::testing::ScopedExtmemAllocationFailure fail(1U);
            assert(detached ? history.sealCandidate(*before) : history.captureAfter(*after));
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        assert(history.ready() && history.changed());
        assert(history.candidate() == nullptr);
        assert(!history.captureAfter(*after) && !history.sealCandidate(*before));
        auto live = std::make_unique<ProjectControlDomainState>(*after);
        for (int cycle = 0; cycle < 4; ++cycle) {
            core::app::testing::ScopedExtmemAllocationFailure fail(1U);
            assert(history.matches(*live, true));
            history.apply(*live);
            assert(std::memcmp(live.get(), before.get(), sizeof(*live)) == 0);
            assert(history.matches(*live, false));
            history.apply(*live);
            assert(std::memcmp(live.get(), after.get(), sizeof(*live)) == 0);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        live->curves.points.back().value ^= 1;
        assert(!history.matches(*live, true));
        {
            core::app::testing::ScopedExtmemAllocationFailure fail(1U);
            assert(!history.prepare(*before));
            assert(history.ready() && history.matches(*after, true));
            std::memcpy(live.get(), after.get(), sizeof(*live));
            history.apply(*live);
            assert(std::memcmp(live.get(), before.get(), sizeof(*live)) == 0);
        }
    }

    ProjectControlHistory stale;
    assert(stale.prepare(*before));
    *stale.candidate() = *after;
    auto drifted = std::make_unique<ProjectControlDomainState>(*before);
    ++drifted->curves.nextCurveId;
    assert(!stale.sealCandidate(*drifted));
    assert(!stale.ready());
    assert(std::memcmp(stale.candidate(), after.get(), sizeof(*after)) == 0);
    assert(drifted->curves.nextCurveId == before->curves.nextCurveId + 1U);
    assert(stale.sealCandidate(*before));

    for (const bool detached : {false, true}) {
        ProjectControlHistory unchanged;
        assert(unchanged.prepare(*before));
        assert(detached ? unchanged.sealCandidate(*before) : unchanged.captureAfter(*before));
        assert(unchanged.ready() && !unchanged.hasStorage() && !unchanged.changed());
        assert(unchanged.matches(*before, false) && unchanged.matches(*before, true));
        auto live = std::make_unique<ProjectControlDomainState>(*before);
        unchanged.apply(*live);
        assert(std::memcmp(live.get(), before.get(), sizeof(*live)) == 0);
    }
    std::cout << "[PASS] live and detached Control history are exact, sealed and allocation-free on replay\n";
}
