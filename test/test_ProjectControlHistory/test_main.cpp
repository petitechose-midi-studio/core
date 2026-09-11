#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstring>
#include <iostream>

#include "state/modulation/ProjectControlHistory.hpp"
#include "state/modulation/ProjectControlState.hpp"

using namespace core::state::modulation;

static void checkFullDomainReplay() {
    auto before = std::make_unique<ProjectControlDomainState>();
    auto after = std::make_unique<ProjectControlDomainState>();
    auto live = std::make_unique<ProjectControlState>();
    ProjectControlHistory history;
    // Reuse one history with early, middle, final, dense changes and an unchanged domain.
    for (const int edit : {4, 3, 2, 1, 0, 1, 3}) {
        std::memcpy(after.get(), before.get(), sizeof(*after));
        if (edit == 1) after->automation.entryCount = 1U;
        if (edit == 2) after->curves.points[10000] = {0x0102U, 0x0304};
        if (edit == 3) after->curves.points.back().value = 0x0100;
        if (edit == 4) after->curves.points.fill({0x0102U, 0x0304});
        assert(history.prepare(*before));
        std::memcpy(history.candidate(), after.get(), sizeof(*after));
        core::app::testing::ScopedExtmemAllocationFailure fail(1U);
        assert(history.sealCandidate(*before));
        assert(history.changed() == (edit != 0));
        live->authored() = *before;
        history.apply(*live);
        for (int cycle = 0; cycle < 3; ++cycle) {
            assert(history.matches(live->authored(), true));
            history.apply(*live);
            assert(std::memcmp(&live->authored(), before.get(), sizeof(*before)) == 0);
            assert(history.matches(live->authored(), false));
            history.apply(*live);
            assert(std::memcmp(&live->authored(), after.get(), sizeof(*after)) == 0);
        }
        // The integrity guard covers inactive points as well as active fields.
        live->authored().curves.points.back().value ^= 1;
        assert(!history.matches(live->authored(), true));
        assert(core::app::testing::extmemAllocationAttempt == 0U);
    }
}

int main() {
    {
        core::app::testing::ScopedExtmemAllocationFailure fail(1U);
        ProjectControlState unavailable;
        assert(!unavailable.hasAuthored());
        unavailable.clear();
    }
    checkFullDomainReplay();
    auto before = std::make_unique<ProjectControlDomainState>();
    before->curves.points.back() = {257U, -1200};
    auto after = std::make_unique<ProjectControlDomainState>(*before);
    after->curves.nextCurveId = 99U;
    after->curves.points.front() = {256U, 2200};
    after->curves.points.back() = {511U, -3300};

    {
        ProjectControlHistory history;
        assert(!history.ready() && !history.hasStorage());
        assert(!history.sealCandidate(*before));
        assert(!history.matches(*before, false));
        {
            core::app::testing::ScopedExtmemAllocationFailure fail(1U);
            assert(!history.prepare(*before));
            assert(!history.hasStorage());
        }
        assert(history.prepare(*before));
        assert(history.candidate() != nullptr);
        *history.candidate() = *after;
        {
            core::app::testing::ScopedExtmemAllocationFailure fail(1U);
            assert(history.sealCandidate(*before));
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        assert(history.ready() && history.changed());
        assert(history.candidate() == nullptr);
        assert(!history.sealCandidate(*before));
        auto live = std::make_unique<ProjectControlState>();
        live->authored() = *before;
        history.apply(*live);
        for (int cycle = 0; cycle < 4; ++cycle) {
            core::app::testing::ScopedExtmemAllocationFailure fail(1U);
            assert(history.matches(live->authored(), true));
            history.apply(*live);
            assert(std::memcmp(&live->authored(), before.get(), sizeof(*before)) == 0);
            assert(history.matches(live->authored(), false));
            history.apply(*live);
            assert(std::memcmp(&live->authored(), after.get(), sizeof(*after)) == 0);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        live->authored().curves.points.back().value ^= 1;
        assert(!history.matches(live->authored(), true));
        {
            core::app::testing::ScopedExtmemAllocationFailure fail(1U);
            assert(!history.prepare(*before));
            assert(history.ready() && history.matches(*after, true));
            live->authored() = *after;
            history.apply(*live);
            assert(std::memcmp(&live->authored(), before.get(), sizeof(*before)) == 0);
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

    {
        ProjectControlHistory unchanged;
        assert(unchanged.prepare(*before));
        assert(unchanged.sealCandidate(*before));
        assert(unchanged.ready() && !unchanged.hasStorage() && !unchanged.changed());
        assert(unchanged.matches(*before, false) && unchanged.matches(*before, true));
        auto live = std::make_unique<ProjectControlState>();
        live->authored() = *before;
        unchanged.apply(*live);
        assert(std::memcmp(&live->authored(), before.get(), sizeof(*before)) == 0);
    }
    std::cout << "[PASS] Prepared Control history is exact, sealed and allocation-free on replay\n";
}
