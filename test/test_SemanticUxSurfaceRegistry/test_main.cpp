#include <cassert>
#include <cstring>
#include <iostream>

#include "../../src/validation/ux/SemanticUxSurface.hpp"

namespace {

class NoMatchSurface final : public core::validation::ux::SemanticUxSurface {
public:
    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent&,
        core::validation::ux::SemanticUxContext&
    ) const override {
        ++calls;
        return false;
    }

    mutable int calls = 0;
};

class MatchSurface final : public core::validation::ux::SemanticUxSurface {
public:
    explicit MatchSurface(const char* mode) : mode_(mode) {}

    bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent&,
        core::validation::ux::SemanticUxContext& out
    ) const override {
        ++calls;
        out.mode = mode_;
        return true;
    }

    mutable int calls = 0;

private:
    const char* mode_;
};

oc::core::input::InputBindingTraceEvent traceEvent() {
    oc::core::input::InputBindingTraceEvent event{};
    event.stage = oc::core::input::InputBindingTraceStage::Dispatch;
    event.dispatched = true;
    return event;
}

void test_first_matching_surface_wins() {
    core::validation::ux::SemanticUxSurfaceRegistry registry;
    NoMatchSurface noMatch;
    MatchSurface first{"first"};
    MatchSurface second{"second"};

    assert(registry.add(noMatch));
    assert(registry.add(first));
    assert(registry.add(second));

    core::validation::ux::SemanticUxContext context{};
    registry.captureSemanticUxContext(traceEvent(), context);

    assert(std::strcmp(context.mode, "first") == 0);
    assert(noMatch.calls == 1);
    assert(first.calls == 1);
    assert(second.calls == 0);
    std::cout << "[PASS] test_first_matching_surface_wins\n";
}

void test_no_match_leaves_context_empty() {
    core::validation::ux::SemanticUxSurfaceRegistry registry;
    NoMatchSurface noMatch;

    assert(registry.add(noMatch));

    core::validation::ux::SemanticUxContext context{};
    registry.captureSemanticUxContext(traceEvent(), context);

    assert(context.mode == nullptr);
    assert(noMatch.calls == 1);
    std::cout << "[PASS] test_no_match_leaves_context_empty\n";
}

void test_capacity_is_deterministic() {
    core::validation::ux::SemanticUxSurfaceRegistry registry;
    MatchSurface surface{"surface"};

    for (std::size_t i = 0; i < core::validation::ux::SemanticUxSurfaceRegistry::CAPACITY; ++i) {
        assert(registry.add(surface));
    }

    assert(registry.count() == core::validation::ux::SemanticUxSurfaceRegistry::CAPACITY);
    assert(!registry.add(surface));
    assert(registry.count() == core::validation::ux::SemanticUxSurfaceRegistry::CAPACITY);
    std::cout << "[PASS] test_capacity_is_deterministic\n";
}

void test_lower_priority_runs_first() {
    core::validation::ux::SemanticUxSurfaceRegistry registry;
    MatchSurface low{"low"};
    MatchSurface high{"high"};

    assert(registry.add(high, 200));
    assert(registry.add(low, 10));

    core::validation::ux::SemanticUxContext context{};
    registry.captureSemanticUxContext(traceEvent(), context);

    assert(std::strcmp(context.mode, "low") == 0);
    assert(low.calls == 1);
    assert(high.calls == 0);
    std::cout << "[PASS] test_lower_priority_runs_first\n";
}

void test_clear_resets_registry() {
    core::validation::ux::SemanticUxSurfaceRegistry registry;
    MatchSurface surface{"surface"};

    assert(registry.add(surface));
    registry.clear();

    assert(registry.count() == 0);
    core::validation::ux::SemanticUxContext context{};
    registry.captureSemanticUxContext(traceEvent(), context);

    assert(context.mode == nullptr);
    assert(surface.calls == 0);
    std::cout << "[PASS] test_clear_resets_registry\n";
}

}  // namespace

int main() {
    test_first_matching_surface_wins();
    test_no_match_leaves_context_empty();
    test_capacity_is_deterministic();
    test_lower_priority_runs_first();
    test_clear_resets_registry();

    std::cout << "All SemanticUxSurfaceRegistry tests passed\n";
    return 0;
}
