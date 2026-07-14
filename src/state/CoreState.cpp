#include "state/CoreState.hpp"

#include <new>
#include <cstdio>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <wiring.h>
#endif

#include "state/CoreStateBootstrap.hpp"
#include "state/CoreStateLifecycle.hpp"
#include "state/shared/SharedTrackCoordinator.hpp"
#include "macro/MacroPersistenceWorkflow.hpp"
#include "macro/MacroWorkflow.hpp"
#include "midi/MidiUtils.hpp"
#include "sequencer/SequencerPersistenceWorkflow.hpp"
#include "sequencer/SequencerContentViewOps.hpp"
#include "sequencer/SequencerStructureHistory.hpp"
#include "sequencer/SequencerTrackBankOps.hpp"

namespace core::state {

namespace {

[[noreturn]] FLASHMEM void failCoreStateAllocation(const char* label) {
    OC_LOG_ERROR("[CoreState] Failed to allocate {}", label);
    while (true) {}
}

FLASHMEM SequencerDomainState::PendingApply* createPendingApply() {
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    void* memory = extmem_malloc(sizeof(SequencerDomainState::PendingApply));
    if (!memory) return nullptr;
    return new(memory) SequencerDomainState::PendingApply();
#else
    return new SequencerDomainState::PendingApply();
#endif
}

FLASHMEM core::app::ExtmemUniquePtr<UiSystemState> createUiSystemState() {
    auto state = core::app::makeExtmemUnique<UiSystemState>();
    if (!state) failCoreStateAllocation("UI system state");
    return state;
}

FLASHMEM core::app::ExtmemUniquePtr<sequencer::SequencerState> createSequencerEditorState() {
    auto state = core::app::makeExtmemUnique<sequencer::SequencerState>();
    if (!state) failCoreStateAllocation("sequencer editor state");
    return state;
}

FLASHMEM core::app::ExtmemUniquePtr<sequencer::SequencerTrackBankState> createSequencerTrackBankState() {
    auto state = core::app::makeExtmemUnique<sequencer::SequencerTrackBankState>();
    if (!state) failCoreStateAllocation("sequencer track bank");
    return state;
}

FLASHMEM int32_t sequencerHistoryValueForProperty(
    const sequencer::SequencerHistoryPatternSnapshot& snapshot,
    uint8_t step,
    sequencer::StepProperty property
) {
    if (step >= sequencer::SequencerPatternState::MAX_STEPS) {
        return 0;
    }

    switch (property) {
        case sequencer::StepProperty::NOTE:
            return snapshot.flat.note[step];
        case sequencer::StepProperty::VELOCITY:
            return snapshot.flat.velocity[step];
        case sequencer::StepProperty::GATE:
            return snapshot.flat.gate[step];
        case sequencer::StepProperty::NUDGE:
            return snapshot.flat.nudge[step];
        case sequencer::StepProperty::PROBABILITY:
            return snapshot.flat.probability[step];
        default:
            return 0;
    }
}

FLASHMEM sequencer::SequencerHistoryDescriptor makeStepPropertyHistoryDescriptor(
    uint8_t track,
    uint8_t step,
    sequencer::StepProperty property,
    const sequencer::SequencerHistoryPatternSnapshot& before,
    const sequencer::SequencerHistoryPatternSnapshot& after
) {
    const int32_t beforeValue = sequencerHistoryValueForProperty(before, step, property);
    const int32_t afterValue = sequencerHistoryValueForProperty(after, step, property);
    if (beforeValue == afterValue) {
        return sequencer::SequencerHistoryDescriptor{
            .kind = sequencer::SequencerHistoryActionKind::StepEdit,
            .trackIndex = track,
            .stepIndex = step,
            .property = property,
            .hasValue = false,
        };
    }

    return sequencer::SequencerHistoryDescriptor{
        .kind = sequencer::SequencerHistoryActionKind::StepPropertyEdit,
        .trackIndex = track,
        .stepIndex = step,
        .property = property,
        .hasValue = true,
        .beforeValue = beforeValue,
        .afterValue = afterValue,
    };
}

FLASHMEM const char* historyDirectionLabel(sequencer::SequencerHistoryDirection direction) {
    return direction == sequencer::SequencerHistoryDirection::Redo ? "REDO" : "UNDO";
}

FLASHMEM const char* historyPropertyLabel(sequencer::StepProperty property) {
    switch (property) {
        case sequencer::StepProperty::NOTE:
            return "Pitch";
        case sequencer::StepProperty::VELOCITY:
            return "Velocity";
        case sequencer::StepProperty::GATE:
            return "Gate";
        case sequencer::StepProperty::NUDGE:
            return "Nudge";
        case sequencer::StepProperty::PROBABILITY:
            return "Chance";
        default:
            return "Property";
    }
}

FLASHMEM const char* historyActionLabel(sequencer::SequencerHistoryActionKind kind) {
    switch (kind) {
        case sequencer::SequencerHistoryActionKind::StepToggle:
            return "Step Toggle";
        case sequencer::SequencerHistoryActionKind::StepPropertyEdit:
            return "Step Property";
        case sequencer::SequencerHistoryActionKind::StepEdit:
            return "Step Edit";
        case sequencer::SequencerHistoryActionKind::QuickControls:
            return "Quick Controls";
        case sequencer::SequencerHistoryActionKind::PatternSettings:
            return "Pattern Settings";
        case sequencer::SequencerHistoryActionKind::PatternVariation:
            return "Variation Range";
        case sequencer::SequencerHistoryActionKind::ProjectScaleSettings:
            return "Project Scale";
        case sequencer::SequencerHistoryActionKind::PageStructure:
            return "Page Structure";
        case sequencer::SequencerHistoryActionKind::TrackStructure:
            return "Track Structure";
        case sequencer::SequencerHistoryActionKind::FullBank:
            return "Sequencer Set";
        case sequencer::SequencerHistoryActionKind::PatternEdit:
        default:
            return "Pattern Edit";
    }
}

FLASHMEM void formatHistoryValue(
    char* buffer,
    size_t bufferSize,
    sequencer::StepProperty property,
    int32_t value
) {
    if (!buffer || bufferSize == 0) return;

    if (property == sequencer::StepProperty::NOTE) {
        core::midi::formatNoteName(
            buffer,
            bufferSize,
            static_cast<uint8_t>(value < 0 ? 0 : (value > 127 ? 127 : value))
        );
        return;
    }

    if (property == sequencer::StepProperty::GATE) {
        std::snprintf(buffer, bufferSize, "%ld%%", static_cast<long>(value));
        return;
    }

    if (property == sequencer::StepProperty::NUDGE) {
        std::snprintf(buffer, bufferSize, "%+ld", static_cast<long>(value));
        return;
    }

    std::snprintf(buffer, bufferSize, "%ld", static_cast<long>(value));
}

FLASHMEM void formatHistoryVariationValue(
    char* buffer,
    size_t bufferSize,
    sequencer::StepProperty property,
    int32_t value
) {
    if (!buffer || bufferSize == 0) return;

    if (property == sequencer::StepProperty::NOTE) {
        std::snprintf(buffer, bufferSize, "+/-%ldst", static_cast<long>(value));
        return;
    }

    if (property == sequencer::StepProperty::GATE) {
        std::snprintf(buffer, bufferSize, "+/-%ld%%", static_cast<long>(value));
        return;
    }

    if (property == sequencer::StepProperty::NUDGE) {
        std::snprintf(buffer, bufferSize, "+/-%ld", static_cast<long>(value));
        return;
    }

    std::snprintf(buffer, bufferSize, "+/-%ld", static_cast<long>(value));
}

FLASHMEM void formatHistoryStructureValue(
    char* buffer,
    size_t bufferSize,
    sequencer::SequencerHistoryActionKind kind,
    int32_t value
) {
    if (!buffer || bufferSize == 0) return;

    const char* unit = kind == sequencer::SequencerHistoryActionKind::TrackStructure
        ? "track"
        : "page";
    std::snprintf(
        buffer,
        bufferSize,
        "%ld %s%s",
        static_cast<long>(value),
        unit,
        value == 1 ? "" : "s"
    );
}

FLASHMEM void showSequencerHistoryFeedback(
    sequencer::SequencerState& sequencerState,
    const sequencer::SequencerHistoryApplyResult& result,
    uint32_t nowMs
) {
    if (!result.applied) return;

    const auto& descriptor = result.descriptor;
    char line1[sequencer::SequencerHistoryFeedbackState::LINE_SIZE]{};
    char line2[sequencer::SequencerHistoryFeedbackState::LINE_SIZE]{};
    char line3[sequencer::SequencerHistoryFeedbackState::LINE_SIZE]{};

    const char* direction = historyDirectionLabel(result.direction);
    if (descriptor.trackIndex != sequencer::SequencerHistoryDescriptor::INVALID_INDEX) {
        std::snprintf(
            line1,
            sizeof(line1),
            "%s T%02u",
            direction,
            static_cast<unsigned>(descriptor.trackIndex + 1U)
        );
    } else {
        std::snprintf(line1, sizeof(line1), "%s", direction);
    }

    if (descriptor.stepIndex != sequencer::SequencerHistoryDescriptor::INVALID_INDEX) {
        std::snprintf(
            line2,
            sizeof(line2),
            "Step %02u %s",
            static_cast<unsigned>(descriptor.stepIndex + 1U),
            historyPropertyLabel(descriptor.property)
        );
    } else if (descriptor.kind == sequencer::SequencerHistoryActionKind::PatternVariation) {
        std::snprintf(
            line2,
            sizeof(line2),
            "Range %s",
            historyPropertyLabel(descriptor.property)
        );
    } else {
        std::snprintf(line2, sizeof(line2), "%s", historyActionLabel(descriptor.kind));
    }

    if (descriptor.hasValue) {
        const int32_t fromValue = result.direction == sequencer::SequencerHistoryDirection::Undo
            ? descriptor.afterValue
            : descriptor.beforeValue;
        const int32_t toValue = result.direction == sequencer::SequencerHistoryDirection::Undo
            ? descriptor.beforeValue
            : descriptor.afterValue;

        if (descriptor.kind == sequencer::SequencerHistoryActionKind::StepToggle) {
            std::snprintf(
                line3,
                sizeof(line3),
                "%s -> %s",
                fromValue != 0 ? "On" : "Off",
                toValue != 0 ? "On" : "Off"
            );
        } else if (descriptor.kind == sequencer::SequencerHistoryActionKind::PageStructure ||
                   descriptor.kind == sequencer::SequencerHistoryActionKind::TrackStructure) {
            char fromText[14]{};
            char toText[14]{};
            formatHistoryStructureValue(
                fromText,
                sizeof(fromText),
                descriptor.kind,
                fromValue
            );
            formatHistoryStructureValue(
                toText,
                sizeof(toText),
                descriptor.kind,
                toValue
            );
            std::snprintf(line3, sizeof(line3), "%s -> %s", fromText, toText);
        } else if (descriptor.kind == sequencer::SequencerHistoryActionKind::PatternVariation) {
            char fromText[12]{};
            char toText[12]{};
            formatHistoryVariationValue(fromText, sizeof(fromText), descriptor.property, fromValue);
            formatHistoryVariationValue(toText, sizeof(toText), descriptor.property, toValue);
            std::snprintf(line3, sizeof(line3), "%s -> %s", fromText, toText);
        } else {
            char fromText[12]{};
            char toText[12]{};
            formatHistoryValue(fromText, sizeof(fromText), descriptor.property, fromValue);
            formatHistoryValue(toText, sizeof(toText), descriptor.property, toValue);
            std::snprintf(line3, sizeof(line3), "%s -> %s", fromText, toText);
        }
    } else {
        std::snprintf(line3, sizeof(line3), "Applied");
    }

    sequencerState.historyFeedback.show(line1, line2, line3, nowMs);
}

FLASHMEM shared::SharedTrackCoordinator::StateRefs sharedTrackRefs(CoreState& state) {
    return shared::SharedTrackCoordinator::StateRefs{
        state.sharedTrackActive,
        state.sharedTrackEnabledMask,
        state.pages,
        state.sequencerTracks,
        state.sequencer,
    };
}

FLASHMEM void syncSequencerStructureUiFromRestoredHistory(CoreState& state) {
    state.trackNavigation.previewAddSlot.set(false);
    state.trackNavigation.syncPreviewTrack(state.sharedTrackActive.get());

    state.sequencer.structureUi.previewAddPageSlot.set(false);
    state.sequencer.structureUi.syncPreviewPage(state.sequencer.visiblePage());
}

constexpr uint32_t nextNonZeroRuntimeRevision(uint32_t current) {
    const uint32_t next = current + 1U;
    return next == 0U ? 1U : next;
}

}  // namespace

FLASHMEM MacroDomainState::MacroDomainState(oc::interface::IStorage& libraryStorage)
    : runtime(core::app::makeExtmemUnique<MacroState>())
    , pages(core::app::makeExtmemUnique<macro::MacroPagesState>())
    , persistence(libraryStorage) {
    if (!runtime) failCoreStateAllocation("macro runtime state");
    if (!pages) failCoreStateAllocation("macro pages state");
}

FLASHMEM MacroDomainState::~MacroDomainState() = default;

FLASHMEM SequencerDomainState::SequencerDomainState(
    oc::interface::IStorage& patternLibraryStorage,
    oc::interface::IStorage& setLibraryStorage
)
    : editor(createSequencerEditorState())
    , tracks(createSequencerTrackBankState())
    , persistence(patternLibraryStorage, setLibraryStorage)
    , pendingApply(nullptr) {}

FLASHMEM SequencerDomainState::~SequencerDomainState() = default;

FLASHMEM void SequencerDomainState::PendingApplyDeleter::operator()(PendingApply* ptr) const noexcept {
    if (!ptr) return;
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    ptr->~PendingApply();
    extmem_free(ptr);
#else
    delete ptr;
#endif
}

FLASHMEM CoreState::CoreState(oc::interface::IStorage& settingsStorage,
                              oc::interface::IStorage& macroLibraryStorage,
                              oc::interface::IStorage& sequencerPatternLibraryStorage,
                              oc::interface::IStorage& sequencerSetLibraryStorage)
    : macroDomain_(macroLibraryStorage)
    , sequencerDomain_(sequencerPatternLibraryStorage,
                       sequencerSetLibraryStorage)
    , systemUi_(createUiSystemState())
    , settings(settingsStorage)
    , macros(*macroDomain_.runtime)
    , pages(*macroDomain_.pages)
    , macroHistory(macroDomain_.history)
    , macroRuntimeOwnerRevision(macroDomain_.runtimeOwnerRevision)
    , configRevision(macroDomain_.configRevision)
    , macroPersistence(macroDomain_.persistence)
    , sequencer(*sequencerDomain_.editor)
    , sequencerTracks(*sequencerDomain_.tracks)
    , sequencerHistory(sequencerDomain_.history)
    , sequencerTrackActivations(sequencerDomain_.trackActivations)
    , sequencerRuntimeProjectRevision(sequencerDomain_.runtimeProjectRevision)
    , sequencerPersistence(sequencerDomain_.persistence)
    , project(project_)
    , overlays(systemUi_->overlays)
    , activeView(systemUi_->activeView)
    , structureNavigationFocus(systemUi_->structureNavigationFocus)
    , sharedTrackActive(systemUi_->sharedTracks.activeIndex)
    , sharedTrackEnabledMask(systemUi_->sharedTracks.enabledMask)
    , trackNavigation(systemUi_->trackNavigation)
    , structureClipboard(systemUi_->structureClipboard)
    , viewSelector(systemUi_->viewSelector)
    , statusBar(systemUi_->statusBar)
    , midiSync(systemUi_->midiSync)
    , deviceSettings(systemUi_->deviceSettings)
    , sequencerSettings(systemUi_->sequencerSettings)
    , patternPitchSettings(systemUi_->patternPitchSettings)
    , dataManager(systemUi_->dataManager)
    , macroEdit(systemUi_->macroEdit)
    , macroUi(systemUi_->macroUi)
    , projectNavigation(systemUi_->projectNavigation) {
    sequencerDomain_.pendingApply.reset(createPendingApply());
    if (!sequencerDomain_.pendingApply) {
        failCoreStateAllocation("sequencer pending apply buffer");
    }
    CoreStateBootstrap::initialize(*this);
}

void CoreState::update() {
    CoreStateLifecycle::update(*this);
}

FLASHMEM void CoreState::factoryReset() {
    CoreStateLifecycle::factoryReset(*this);
}

FLASHMEM void CoreState::flush() {
    commitSequencerPatternHistoryCoalescing();
    CoreStateLifecycle::flush(*this);
}

FLASHMEM void CoreState::flushProjectMutationCoalescing() {
    CoreStateLifecycle::flushProjectMutationCoalescing(*this);
}

FLASHMEM void CoreState::resetStandaloneTransientUi() {
    commitSequencerPatternHistoryCoalescing();
    CoreStateLifecycle::resetStandaloneTransientUi(*this);
}

FLASHMEM void CoreState::resetMusicalProject() {
    commitSequencerPatternHistoryCoalescing();
    CoreStateLifecycle::resetMusicalProject(*this);
}

FLASHMEM void CoreState::requestMacroRuntimeOwnerActivation() {
    macroRuntimeOwnerRevision.set(nextNonZeroRuntimeRevision(macroRuntimeOwnerRevision.get()));
}

FLASHMEM void CoreState::requestSequencerRuntimeProjectReset() {
    sequencerTrackActivations.reset();
    sequencerRuntimeProjectRevision.set(
        nextNonZeroRuntimeRevision(sequencerRuntimeProjectRevision.get())
    );
}

void CoreState::markMacroValueEdited(uint8_t index) {
    if (index >= MACRO_COUNT) return;
    if (macroDomain_.mutationCoalescer) {
        macroDomain_.mutationCoalescer->markChanged();
        return;
    }
    markProjectMutated();
}

FLASHMEM void CoreState::markProjectMutated() {
    ++project.metadata.modifiedCounter;
    if (project.metadata.modifiedCounter == 0) {
        project.metadata.modifiedCounter = 1;
    }
    project.metadata.dirty = true;
    requestProjectSessionSave_();
    projectNavigation.notifyContentChanged();
}

FLASHMEM void CoreState::requestProjectSessionSave() {
    requestProjectSessionSave_();
}

FLASHMEM void CoreState::acknowledgeProjectSessionSave(uint32_t savedModifiedCounter) {
    if (project.metadata.modifiedCounter != savedModifiedCounter) {
        requestProjectSessionSave_();
        return;
    }

    projectSessionSavePending_ = false;
    projectSessionSaveTimestampMs_ = 0;
}

bool CoreState::hasPendingProjectSessionSave() const {
    return projectSessionSavePending_;
}

uint32_t CoreState::projectSessionSaveTimestampMs() const {
    return projectSessionSaveTimestampMs_;
}

bool CoreState::hasPendingProjectMutationCoalescing() const {
    const bool macroPending =
        macroDomain_.mutationCoalescer && macroDomain_.mutationCoalescer->hasPendingChanges();
    const bool sequencerPending =
        sequencerDomain_.mutationCoalescer &&
        sequencerDomain_.mutationCoalescer->hasPendingChanges();
    return macroPending || sequencerPending ||
           hasPendingSequencerPatternHistoryCoalescing();
}

bool CoreState::isMacroPersistenceReady() const {
    return macroDomain_.persistenceReady;
}

bool CoreState::isSequencerPersistenceReady() const {
    return sequencerDomain_.persistenceReady;
}

FLASHMEM void CoreState::markSequencerProjectMutated() {
    markSequencerProjectMutated_();
}

FLASHMEM bool CoreState::recordSequencerPatternHistory(
    sequencer::SequencerHistoryPatternSnapshot before,
    sequencer::SequencerHistoryPatternSnapshot after,
    sequencer::SequencerHistoryDescriptor descriptor,
    sequencer::SequencerHistoryPatternStorage storage
) {
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    uint8_t targetTrack = activeTrack;
    if (descriptor.trackIndex == sequencer::SequencerHistoryDescriptor::INVALID_INDEX) {
        descriptor.trackIndex = activeTrack;
    } else {
        targetTrack = sequencer::SequencerTrackBankState::clampTrackIndex(descriptor.trackIndex);
        descriptor.trackIndex = targetTrack;
    }

    const bool recorded = storage == sequencer::SequencerHistoryPatternStorage::FlatOnly
        ? sequencerHistory.recordFlatPattern(
              targetTrack,
              std::move(before),
              std::move(after),
              descriptor
          )
        : sequencerHistory.recordPattern(
              targetTrack,
              std::move(before),
              std::move(after),
              descriptor
          );
    if (!recorded) {
        return false;
    }

    const bool synchronized = storage == sequencer::SequencerHistoryPatternStorage::FlatOnly
        ? sequencer::storeActiveTrackPreservingGraph(sequencerTracks, sequencer)
        : sequencer::storeActiveTrack(sequencerTracks, sequencer);
    if (!synchronized) {
        OC_LOG_ERROR("[CoreState] Failed to synchronize active sequencer graph after history");
    }
    markProjectMutated();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::recordSequencerPatternHistory(
    sequencer::SequencerHistoryPatternChangePtr change
) {
    if (!change) return false;

    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    const uint8_t targetTrack =
        change->descriptor.trackIndex == sequencer::SequencerHistoryDescriptor::INVALID_INDEX
            ? activeTrack
            : sequencer::SequencerTrackBankState::clampTrackIndex(
                  change->descriptor.trackIndex
              );
    change->trackIndex = targetTrack;
    change->descriptor.trackIndex = targetTrack;
    const auto storage = change->storage;
    if (!sequencerHistory.recordPattern(std::move(change))) return false;

    const bool synchronized = storage == sequencer::SequencerHistoryPatternStorage::FlatOnly
        ? sequencer::storeActiveTrackPreservingGraph(sequencerTracks, sequencer)
        : sequencer::storeActiveTrack(sequencerTracks, sequencer);
    if (!synchronized) {
        OC_LOG_ERROR("[CoreState] Failed to synchronize active sequencer graph after history");
    }
    markProjectMutated();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::recordSequencerBankHistory(
    sequencer::SequencerHistoryTrackBankSnapshot before,
    sequencer::SequencerHistoryTrackBankSnapshot after,
    sequencer::SequencerHistoryDescriptor descriptor
) {
    if (!sequencerHistory.recordFullBank(
            std::move(before),
            std::move(after),
            descriptor
        )) {
        return false;
    }

    markSequencerProjectMutated_();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::recordSequencerBankHistory(
    sequencer::SequencerHistoryFullBankChangePtr change
) {
    if (!sequencerHistory.recordFullBank(std::move(change))) {
        return false;
    }

    markSequencerProjectMutated_();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::recordSequencerStructureHistory(
    sequencer::SequencerHistoryTrackStructureChangePtr change
) {
    if (!sequencerHistory.recordStructure(std::move(change))) {
        return false;
    }

    markSequencerProjectMutated_();
    refreshSharedTrackStateFromSequencer();
    return true;
}

FLASHMEM bool CoreState::canRecordSequencerStructureHistory(
    const sequencer::SequencerHistoryTrackStructureChange& change
) const {
    return sequencerHistory.canRecordStructure(change);
}

FLASHMEM void CoreState::recordPreparedSequencerStructureHistory(
    sequencer::SequencerHistoryTrackStructureChangePtr change
) {
    sequencerHistory.recordPreparedStructure(std::move(change));
    // The prepared Track transaction has already synchronized its bank/editor
    // state. Marking the project dirty is allocation-free and deliberately
    // avoids markSequencerProjectMutated_()/refreshSharedTrackStateFromSequencer().
    markProjectMutated();
}

FLASHMEM bool CoreState::beginOrContinueSequencerPatternHistoryCoalescing(
    uint8_t step,
    sequencer::StepProperty property,
    uint32_t nowMs
) {
    auto& pending = sequencerDomain_.coalescedPatternHistory;
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();

    if (pending.matches(activeTrack, step, property)) {
        pending.lastTouchedMs = nowMs;
        return true;
    }

    if (pending.pending) {
        commitSequencerPatternHistoryCoalescing();
    }

    sequencer::SequencerHistoryPatternSnapshot before;
    if (!sequencer::captureHistorySnapshot(sequencer, before)) {
        pending.clear();
        return false;
    }

    pending.clear();
    pending.pending = true;
    pending.activeTrack = activeTrack;
    pending.step = step;
    pending.property = property;
    pending.lastTouchedMs = nowMs;
    pending.before = std::move(before);
    return true;
}

FLASHMEM bool CoreState::commitSequencerPatternHistoryCoalescing() {
    auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending) {
        return false;
    }

    const uint8_t targetTrack = pending.activeTrack;
    const uint8_t targetStep = pending.step;
    const auto targetProperty = pending.property;
    sequencer::SequencerHistoryPatternSnapshot before = std::move(pending.before);
    pending.clear();

    const uint32_t currentGraphRevision =
        targetTrack == sequencerTracks.activeTrackIndex()
            ? sequencer.pattern.graphRevision.get()
            : sequencerTracks.track(targetTrack).graphRevision.get();
    const auto storage = before.flat.graphRevision == currentGraphRevision
        ? sequencer::SequencerHistoryPatternStorage::FlatOnly
        : sequencer::SequencerHistoryPatternStorage::FullGraph;

    sequencer::SequencerHistoryPatternSnapshot after;
    if (storage == sequencer::SequencerHistoryPatternStorage::FlatOnly) {
        before.graph.reset();
        sequencer::captureFlatHistorySnapshot(sequencerTracks, sequencer, targetTrack, after);
    } else if (!sequencer::captureHistorySnapshot(
                   sequencerTracks,
                   sequencer,
                   targetTrack,
                   after
               )) {
        return false;
    }

    auto descriptor = makeStepPropertyHistoryDescriptor(
        targetTrack,
        targetStep,
        targetProperty,
        before,
        after
    );

    return recordSequencerPatternHistory(
        std::move(before),
        std::move(after),
        descriptor,
        storage
    );
}

FLASHMEM bool CoreState::updateSequencerPatternHistoryCoalescing(uint32_t nowMs) {
    const auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending) {
        return false;
    }

    if (static_cast<uint32_t>(nowMs - pending.lastTouchedMs) <
        SequencerDomainState::COALESCED_PATTERN_HISTORY_IDLE_MS) {
        return false;
    }

    return commitSequencerPatternHistoryCoalescing();
}

bool CoreState::hasPendingSequencerPatternHistoryCoalescing() const {
    return sequencerDomain_.coalescedPatternHistory.pending;
}

FLASHMEM bool CoreState::undoSequencerHistory() {
    commitSequencerPatternHistoryCoalescing();

    sequencer::SequencerTrackActivationHistoryPlan activation;
    const bool hasActivation = sequencerHistory.peekUndoTrackActivation(activation);
    sequencer::SequencerTrackActivationHistoryTransition activationTransition;
    if (hasActivation && !sequencerTrackActivations.prepareHistoryTransition(
            activation.reference,
            sequencer::SequencerTrackActivationTarget::BEFORE,
            activation.targetEnabledMask,
            activation.targetMutedMask,
            statusBar.playing.get(),
            activationTransition
        )) {
        return false;
    }
    const auto result = sequencerHistory.undoWithResult(sequencerTracks, sequencer);
    if (!result.applied) {
        if (hasActivation) {
            sequencerTrackActivations.rollbackHistoryTransition(activationTransition);
        }
        return false;
    }
    if (hasActivation) {
        sequencerTrackActivations.commitHistoryTransition(activationTransition);
    }

    markSequencerProjectMutated_();
    sequencer::refreshContentView(sequencer);
    sequencer.contentView.bump();
    showSequencerHistoryFeedback(sequencer, result, oc::time::millis());
    refreshSharedTrackStateFromSequencer();
    syncSequencerStructureUiFromRestoredHistory(*this);
    return true;
}

FLASHMEM bool CoreState::redoSequencerHistory() {
    commitSequencerPatternHistoryCoalescing();

    sequencer::SequencerTrackActivationHistoryPlan activation;
    const bool hasActivation = sequencerHistory.peekRedoTrackActivation(activation);
    sequencer::SequencerTrackActivationHistoryTransition activationTransition;
    if (hasActivation && !sequencerTrackActivations.prepareHistoryTransition(
            activation.reference,
            sequencer::SequencerTrackActivationTarget::AFTER,
            activation.targetEnabledMask,
            activation.targetMutedMask,
            statusBar.playing.get(),
            activationTransition
        )) {
        return false;
    }
    const auto result = sequencerHistory.redoWithResult(sequencerTracks, sequencer);
    if (!result.applied) {
        if (hasActivation) {
            sequencerTrackActivations.rollbackHistoryTransition(activationTransition);
        }
        return false;
    }
    if (hasActivation) {
        sequencerTrackActivations.commitHistoryTransition(activationTransition);
    }

    markSequencerProjectMutated_();
    sequencer::refreshContentView(sequencer);
    sequencer.contentView.bump();
    showSequencerHistoryFeedback(sequencer, result, oc::time::millis());
    refreshSharedTrackStateFromSequencer();
    syncSequencerStructureUiFromRestoredHistory(*this);
    return true;
}

FLASHMEM void CoreState::clearSequencerHistory() {
    sequencerDomain_.coalescedPatternHistory.clear();
    sequencerHistory.clear();
}

FLASHMEM bool CoreState::queuePendingSequencerApply(
    sequencer::SequencerState& staged,
    bool merge
) {
    return queueSequencerApply_(staged, merge);
}

FLASHMEM bool CoreState::queuePendingSequencerBankApply(
    sequencer::SequencerTrackBankState& stagedBank,
    sequencer::SequencerState& staged
) {
    return queueSequencerBankApply_(stagedBank, staged);
}

FLASHMEM void CoreState::clearPendingSequencerApply() {
    clearPendingSequencerApply_();
}

bool CoreState::hasPendingSequencerApply() const {
    return sequencerDomain_.pendingApply && sequencerDomain_.pendingApply->valid;
}

uint16_t CoreState::currentSharedTrackEnabledMask() const {
    return sharedTrackEnabledMask.get();
}

uint8_t CoreState::currentSharedActiveTrack() const {
    return sharedTrackActive.get();
}

bool CoreState::setSharedTrackState(uint16_t enabledMask, uint8_t activeTrack) {
    return setSharedTrackState_(enabledMask, activeTrack, true);
}

void CoreState::publishPreparedSequencerTrackState(uint16_t enabledMask, uint8_t activeTrack) {
    const auto result = shared::SharedTrackCoordinator::publishPreparedSequencerState(
        sharedTrackRefs(*this),
        enabledMask,
        activeTrack
    );
    if (result.changed) {
        requestSharedTrackPersist_();
    }
}

bool CoreState::refreshSharedTrackStateFromMacroPages() {
    return refreshSharedTrackStateFromMacroPages_(true);
}

bool CoreState::refreshSharedTrackStateFromSequencer() {
    return refreshSharedTrackStateFromSequencer_(true);
}

FLASHMEM persistence::PersistenceWriteStatus CoreState::recoverPersistenceFromRamAfterStorageReopen() {
    auto status = settings.saveAllStatus(
        midiSync,
        sharedTrackEnabledMask.get(),
        sharedTrackActive.get()
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;

    status = settings.saveDataManagerMacroShortcutLeftStatus(
        static_cast<uint8_t>(dataManager.macroShortcutLeft.get())
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;
    status = settings.saveDataManagerMacroShortcutRightStatus(
        static_cast<uint8_t>(dataManager.macroShortcutRight.get())
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;
    status = settings.saveDataManagerSeqShortcutLeftStatus(
        static_cast<uint8_t>(dataManager.seqShortcutLeft.get())
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;
    status = settings.saveDataManagerSeqShortcutRightStatus(
        static_cast<uint8_t>(dataManager.seqShortcutRight.get())
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;
    status = settings.commitStatus();
    if (status != persistence::PersistenceWriteStatus::OK) return status;

    status = macroPersistence.initStatus();
    macroDomain_.persistenceReady = status == persistence::PersistenceWriteStatus::OK;
    if (status != persistence::PersistenceWriteStatus::OK) return status;

    status = sequencerPersistence.initStatus();
    sequencerDomain_.persistenceReady = status == persistence::PersistenceWriteStatus::OK;
    if (status != persistence::PersistenceWriteStatus::OK) return status;

    sharedTrackPersistPending_ = false;
    sharedTrackPersistTimestampMs_ = 0;
    return persistence::PersistenceWriteStatus::OK;
}

FLASHMEM bool CoreState::queueSequencerApply_(
    sequencer::SequencerState& staged,
    bool merge
) {
    commitSequencerPatternHistoryCoalescing();
    return CoreStateLifecycle::queuePendingSequencerApply(*this, staged, merge);
}

FLASHMEM bool CoreState::queueSequencerBankApply_(
    sequencer::SequencerTrackBankState& stagedBank,
    sequencer::SequencerState& staged
) {
    commitSequencerPatternHistoryCoalescing();
    return CoreStateLifecycle::queuePendingSequencerBankApply(*this, stagedBank, staged);
}

FLASHMEM void CoreState::requestProjectSessionSave_() {
    if (!projectSessionTrackingEnabled_) return;

    projectSessionSavePending_ = true;
    projectSessionSaveTimestampMs_ = oc::time::millis();
}

FLASHMEM void CoreState::markSequencerProjectMutated_() {
    if (!sequencer::storeActiveTrack(sequencerTracks, sequencer)) {
        OC_LOG_ERROR("[CoreState] Failed to synchronize active sequencer graph");
    }
    markProjectMutated();
}

FLASHMEM void CoreState::requestSharedTrackPersist_() {
    sharedTrackPersistPending_ = true;
    sharedTrackPersistTimestampMs_ = oc::time::millis();
}

FLASHMEM void CoreState::persistSharedTrackState_() {
    if (!sharedTrackPersistPending_) return;

    const auto persistStatus = settings.saveSharedTrackStateStatus(
        sharedTrackEnabledMask.get(),
        sharedTrackActive.get()
    );
    if (persistStatus == persistence::PersistenceWriteStatus::OK) {
        sharedTrackPersistPending_ = false;
        sharedTrackPersistTimestampMs_ = 0;
        return;
    }

    OC_LOG_WARN("[CoreState] Failed to persist shared track state: {}",
                persistence::persistenceWriteStatusLabel(persistStatus));
    if (persistStatus == persistence::PersistenceWriteStatus::STORAGE_UNAVAILABLE) {
        sharedTrackPersistPending_ = true;
        sharedTrackPersistTimestampMs_ = oc::time::millis();
    } else {
        sharedTrackPersistPending_ = false;
        sharedTrackPersistTimestampMs_ = 0;
    }
}

FLASHMEM void CoreState::clearPendingSequencerApply_() {
    CoreStateLifecycle::clearPendingSequencerApply(*this);
}

FLASHMEM bool CoreState::refreshSharedTrackStateFromMacroPages_(bool persist) {
    const auto result = shared::SharedTrackCoordinator::refreshFromMacroPages(sharedTrackRefs(*this));
    if (result.changed && persist) {
        requestSharedTrackPersist_();
    }
    return result.changed;
}

FLASHMEM bool CoreState::refreshSharedTrackStateFromSequencer_(bool persist) {
    const auto result = shared::SharedTrackCoordinator::refreshFromSequencer(sharedTrackRefs(*this));
    if (result.changed && persist) {
        requestSharedTrackPersist_();
    }
    return result.changed;
}

FLASHMEM bool CoreState::setSharedTrackState_(uint16_t enabledMask, uint8_t activeTrack, bool persist) {
    commitSequencerPatternHistoryCoalescing();

    const auto result = shared::SharedTrackCoordinator::apply(
        sharedTrackRefs(*this),
        enabledMask,
        activeTrack
    );
    if (result.changed && persist) {
        requestSharedTrackPersist_();
    }
    return result.changed;
}

}  // namespace core::state
