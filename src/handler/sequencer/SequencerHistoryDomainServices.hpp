#pragma once

#include "state/sequencer/SequencerHistory.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

class SequencerHistoryDomainServices {
public:
    using RecordPatternFn = bool (*)(
        void* context,
        core::state::sequencer::SequencerHistoryPatternSnapshot before,
        core::state::sequencer::SequencerHistoryPatternSnapshot after
    );
    using CommandFn = bool (*)(void* context);

    struct Operations {
        void* context = nullptr;
        RecordPatternFn recordPattern = nullptr;
        CommandFn undo = nullptr;
        CommandFn redo = nullptr;
    };

    SequencerHistoryDomainServices() = default;
    explicit SequencerHistoryDomainServices(Operations operations);
    static SequencerHistoryDomainServices fromCoreState(core::state::CoreState& state);

    bool recordPattern(
        core::state::sequencer::SequencerHistoryPatternSnapshot before,
        core::state::sequencer::SequencerHistoryPatternSnapshot after
    ) const;
    bool undo() const;
    bool redo() const;

private:
    Operations operations_{};
};

}  // namespace core::handler
