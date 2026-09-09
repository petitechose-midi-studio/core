#include "validation/benchmark/HardwareBenchmarkRun.hpp"
#include <cassert>
#include <vector>
#include <cstdio>
using namespace core::validation::benchmark;
namespace {
void put16(std::vector<uint8_t>& p, uint16_t n) { p.push_back(n); p.push_back(n >> 8); }
void put32(std::vector<uint8_t>& p, uint32_t n) { put16(p, n); put16(p, n >> 16); }
std::vector<uint8_t> script(uint32_t duration = 1000, uint32_t budget = 100) {
    std::vector<uint8_t> p;
    put32(p, 42); put16(p, 2); put32(p, duration); put32(p, budget);
    put32(p, 100); p.push_back(1); p.push_back(0); put16(p, 31); put32(p, 1);
    put32(p, 500); p.push_back(1); p.push_back(0); put16(p, 31); put32(p, 0);
    return p;
}
}
int main() {
    HardwareBenchmarkRun run;
    auto p = script();
    assert(run.upload(p.data(), p.size()) == RpcStatus::OK);
    assert(run.upload(p.data(), p.size()) == RpcStatus::OK);
    p[0] = 43;
    assert(run.upload(p.data(), p.size()) == RpcStatus::CONFLICT);
    assert(run.start(43, 1000) == RpcStatus::CONFLICT);
    assert(run.start(42, 1000) == RpcStatus::OK);
    assert(!run.beginIfDue(500999));
    assert(run.beginIfDue(501000));
    assert(!run.takeDue(501099));
    assert(run.takeDue(501100)->value == 1);
    assert(run.start(42, 501400) == RpcStatus::OK);
    assert(run.startedAtUs() == 501000);
    assert(run.takeDue(501501)->value == 0);
    assert(!run.completeIfDue(501999));
    assert(run.completeIfDue(502000));
    assert(run.maxLatenessUs() == 1);
    assert(run.upload(p.data(), p.size()) == RpcStatus::INVALID_STATE);

    HardwareBenchmarkRun late;
    p = script(); assert(late.upload(p.data(), p.size()) == RpcStatus::OK);
    late.start(42, UINT32_MAX - 100000);
    const auto start = late.startedAtUs();
    assert(late.beginIfDue(start));
    assert(!late.takeDue(start + 201));
    assert(late.state() == RunState::FAILED && late.consumed() == 0);
    assert(late.maxLatenessUs() == 101);

    HardwareBenchmarkRun invalid;
    for (size_t size = 0; size < p.size(); ++size)
        assert(invalid.upload(p.data(), size) != RpcStatus::OK);
    p[14 + 6] = 255;
    assert(invalid.upload(p.data(), p.size()) == RpcStatus::INVALID_ARGUMENT);
    p = script(); p[26 + 8] = 1;
    assert(invalid.upload(p.data(), p.size()) == RpcStatus::INVALID_ARGUMENT);
    p = script(); p[19] = 1;
    assert(invalid.upload(p.data(), p.size()) == RpcStatus::INVALID_ARGUMENT);
    assert(!validButton(0) && !validButton(255) && validButton(38));
    assert(!validEncoder(300) && validEncoder(410));
    // Fault injection is hardware-only, bounded and cannot hide overlapping
    // scheduled actions or extend past the declared measurement interval.
    auto block = script(2'000'000, 100);
    block[18] = uint8_t(ActionKind::BLOCK_FOREGROUND);
    block[20] = 0;
    block[22] = 0xA0; block[23] = 0x86; block[24] = 1; block[25] = 0; // 100000 us
    HardwareBenchmarkRun overlap;
    assert(overlap.upload(block.data(), block.size()) == RpcStatus::INVALID_ARGUMENT);
    // Replace the second event by a harmless marker after the block.
    block[26] = 0x10; block[27] = 0xAD; block[28] = 1; block[29] = 0; // 109840 us
    block[30] = uint8_t(ActionKind::MARKER);
    HardwareBenchmarkRun bounded;
    assert(bounded.upload(block.data(), block.size()) == RpcStatus::OK);
    block[22] = block[23] = block[24] = block[25] = 0;
    HardwareBenchmarkRun zero;
    assert(zero.upload(block.data(), block.size()) == RpcStatus::INVALID_ARGUMENT);
    puts("HardwareBenchmarkRun: PASS");
}
