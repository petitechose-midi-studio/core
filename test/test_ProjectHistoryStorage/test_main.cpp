#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

#include "state/project/ProjectSettingsHistory.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/project/ProjectTrackHistory.hpp"

namespace {
size_t allocations = 0;
}

void* operator new(size_t size) {
    ++allocations;
    if (void* result = std::malloc(size == 0 ? 1 : size)) return result;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, size_t) noexcept { std::free(ptr); }

namespace {
namespace project = core::state::project;
using Direction = project::ProjectHistoryDirection;

struct Event {
    char type;
    project::ProjectHistoryDomain domain;
    uintptr_t identity;
    uint8_t detail;
};

struct Events {
    std::array<Event, 16> values{};
    size_t count = 0;
    project::ProjectHistoryEventSink sink{
        this,
        [](void* p, auto d, uintptr_t id, uint8_t kind) { static_cast<Events*>(p)->add({'C', d, id, kind}); },
        [](void* p, auto d, uintptr_t id) { static_cast<Events*>(p)->add({'E', d, id, 0}); },
        [](void* p, auto d) { static_cast<Events*>(p)->add({'X', d, 0, 0}); },
        [](void* p, auto d, uintptr_t id, Direction direction) {
            static_cast<Events*>(p)->add({'A', d, id, static_cast<uint8_t>(direction)});
        },
    };
    void add(Event event) { assert(count < values.size()); values[count++] = event; }
    std::vector<uintptr_t> evicted() const {
        std::vector<uintptr_t> result;
        for (size_t i = 0; i < count; ++i) if (values[i].type == 'E') result.push_back(values[i].identity);
        std::sort(result.begin(), result.end());
        return result;
    }
};

template <typename Action>
auto withoutAllocation(Action action) {
    const auto before = allocations;
    const auto result = action();
    assert(allocations == before);
    return result;
}

struct Track {
    static constexpr auto domain = project::ProjectHistoryDomain::Track;
    static constexpr auto kind = project::ProjectTrackHistoryActionKind::Delay;
    project::ProjectTrackState state;
    project::ProjectTrackHistoryService history;
    int value() const { return state.authored.delayMs[0]; }
    void set(int value) { (void)project::setProjectTrackDelayMs(state, 0, value); }
    bool record(int value) {
        assert(history.beginGesture(state, kind, 0));
        set(value);
        return history.commitGesture(state);
    }
    bool apply(Direction direction) { return direction == Direction::Undo ? history.undo(state) : history.redo(state); }
};

struct Settings {
    static constexpr auto domain = project::ProjectHistoryDomain::Settings;
    static constexpr auto kind = project::ProjectSettingsHistoryActionKind::Tempo;
    core::state::StatusBarState status;
    project::ProjectNavigationState navigation;
    project::ProjectSettingsHistoryService history;
    int value() const { return static_cast<int>(status.tempo.get()); }
    void set(int value) { status.tempo.set(static_cast<float>(value)); }
    bool record(int value, bool coalesce = false) {
        const auto before = project::captureProjectSettingsHistorySnapshot(status, navigation);
        set(value);
        return history.record(before, project::captureProjectSettingsHistorySnapshot(status, navigation), kind, 0, coalesce);
    }
    bool apply(Direction direction) { return direction == Direction::Undo ? history.undo(status, navigation) : history.redo(status, navigation); }
};

// Semantic oracle: one chronological list with a cursor, independent of the
// product's stable-slot pool and its two stacks of indices.
template <typename Fixture>
void exerciseHistory() {
    Fixture fixture;
    Events events;
    fixture.history.setProjectHistoryEventSink(&events.sink);
    struct Command { int before; int after; uintptr_t identity; };
    std::vector<Command> commands;
    size_t cursor = 0;
    int value = fixture.value();
    uint32_t random = 20260911;
    for (unsigned step = 0; step < 4096; ++step) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const unsigned action = random % 12;
        events.count = 0;
        std::vector<uintptr_t> evictions;
        if (action < 6) {
            const int next = action == 0 ? value : 21 + (random >> 8) % 70;
            assert(withoutAllocation([&] { return fixture.record(next); }) == (next != value));
            if (next != value) {
                for (size_t i = cursor; i < commands.size(); ++i) evictions.push_back(commands[i].identity);
                commands.resize(cursor);
                if (commands.size() == fixture.history.ENTRY_LIMIT) {
                    evictions.push_back(commands.front().identity);
                    commands.erase(commands.begin());
                }
                const uintptr_t identity = fixture.history.projectHistoryUndoIdentity();
                assert(identity != 0);
                for (const auto& command : commands) assert(command.identity != identity);
                assert(events.count == evictions.size() + 1);
                const auto& committed = events.values[events.count - 1];
                assert(committed.type == 'C' && committed.identity == identity &&
                       committed.detail == static_cast<uint8_t>(Fixture::kind));
                commands.push_back({value, next, identity});
                cursor = commands.size();
                value = next;
            } else assert(events.count == 0);
        } else if (action < 9) {
            const auto direction = action == 6 ? Direction::Undo : Direction::Redo;
            const bool available = direction == Direction::Undo ? cursor != 0 : cursor != commands.size();
            assert(withoutAllocation([&] { return fixture.apply(direction); }) == available);
            if (available) {
                const auto& command = commands[direction == Direction::Undo ? cursor - 1 : cursor];
                value = direction == Direction::Undo ? command.before : command.after;
                assert(events.count == 1 && events.values[0].type == 'A' &&
                       events.values[0].identity == command.identity &&
                       events.values[0].detail == static_cast<uint8_t>(direction));
                if (direction == Direction::Undo) --cursor; else ++cursor;
            } else assert(events.count == 0);
        } else if (action == 9) {
            for (size_t i = cursor; i < commands.size(); ++i) evictions.push_back(commands[i].identity);
            withoutAllocation([&] { fixture.history.discardRedoBranch(); return true; });
            assert(events.count == evictions.size());
            commands.resize(cursor);
        } else if (action == 10) {
            withoutAllocation([&] { fixture.history.clear(); return true; });
            assert(events.count == 1 && events.values[0].type == 'X');
            commands.clear(); cursor = 0;
        } else {
            fixture.set(95); // Outside every generated authored state.
            assert(!withoutAllocation([&] { return fixture.apply(Direction::Undo); }));
            assert(!withoutAllocation([&] { return fixture.apply(Direction::Redo); }));
            assert(events.count == 0 && fixture.value() == 95);
            fixture.set(value);
        }
        assert(fixture.value() == value);
        assert(fixture.history.projectHistoryUndoIdentity() == (cursor ? commands[cursor - 1].identity : 0));
        assert(fixture.history.projectHistoryRedoIdentity() == (cursor < commands.size() ? commands[cursor].identity : 0));
        for (size_t i = 0; i < events.count; ++i) assert(events.values[i].domain == Fixture::domain);
        std::sort(evictions.begin(), evictions.end());
        assert(events.evicted() == evictions);
    }
}

void settingsCoalescing() {
    Settings fixture;
    Events events;
    fixture.history.setProjectHistoryEventSink(&events.sink);
    const int before = fixture.value();
    assert(fixture.record(130, true));
    const auto identity = fixture.history.projectHistoryUndoIdentity();
    assert(withoutAllocation([&] { return fixture.record(140, true); }));
    assert(events.count == 1 && fixture.history.projectHistoryUndoIdentity() == identity);
    assert(fixture.apply(Direction::Undo) && fixture.value() == before);
    events.count = 0;
    assert(fixture.record(150, true)); // Undo ended the previous gesture.
    assert(events.count == 2 && events.values[0].type == 'E' && events.values[0].identity == identity);
    assert(!fixture.apply(Direction::Redo));
    fixture.history.endCoalescing();
    assert(fixture.record(160, true));
    assert(fixture.apply(Direction::Undo) && fixture.value() == 150);
    assert(fixture.apply(Direction::Undo) && fixture.value() == before);
}
} // namespace

int main() {
    exerciseHistory<Track>();
    exerciseHistory<Settings>();
    settingsCoalescing();
    std::cout << "[PASS] 8192 public history operations: capacity, identities, chronology, drift and zero allocations\n";
}
