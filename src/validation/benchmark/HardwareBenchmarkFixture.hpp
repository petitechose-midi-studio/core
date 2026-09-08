#pragma once

namespace core::state { struct CoreState; }

namespace core::validation::benchmark {

inline constexpr char VERSION[] = "bench-macro-v1";

// Once per cold boot, immediately after CoreState construction and before
// runtime/input/UI creation. This is deliberately not a runtime reset API.
[[nodiscard]] bool prepareHardwareBenchmarkFixture(core::state::CoreState& state);

}  // namespace core::validation::benchmark
