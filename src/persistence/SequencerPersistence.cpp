#include "persistence/SequencerPersistence.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/SequencerPersistenceEnvelope.hpp"

namespace core::persistence {

namespace {

FLASHMEM core::app::ExtmemUniquePtr<sequencer_codec::EnvelopeBuffer> makeEnvelopeBuffer() {
    return core::app::makeExtmemUnique<sequencer_codec::EnvelopeBuffer>();
}

}  // namespace

FLASHMEM SequencerPersistence::SequencerPersistence(
    oc::interface::IStorage& workspaceStorage,
    oc::interface::IStorage& patternLibraryStorage,
    oc::interface::IStorage& setLibraryStorage
)
    : workspace_store_(workspaceStorage,
                       {.fileMagic = WORKSPACE_MAGIC,
                        .domainVersion = WORKSPACE_DATA_VERSION,
                        .slotCount = WORKSPACE_SLOT_COUNT,
                        .slotPayloadSize = sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE})
    , pattern_library_store_(patternLibraryStorage,
                             {.fileMagic = PATTERN_LIBRARY_MAGIC,
                              .domainVersion = LIBRARY_DATA_VERSION,
                              .slotCount = PATTERN_LIBRARY_SLOT_COUNT,
                              .slotPayloadSize = sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE})
    , set_library_store_(setLibraryStorage,
                         {.fileMagic = SET_LIBRARY_MAGIC,
                          .domainVersion = LIBRARY_DATA_VERSION,
                          .slotCount = SET_LIBRARY_SLOT_COUNT,
                          .slotPayloadSize = sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE}) {}

FLASHMEM bool SequencerPersistence::init() {
    return initStatus() == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus SequencerPersistence::initStatus() {
    if (!workspace_store_.init(true)) return PersistenceWriteStatus::INVALID_CONFIG;
    if (!pattern_library_store_.init(true)) return PersistenceWriteStatus::INVALID_CONFIG;
    if (!set_library_store_.init(true)) return PersistenceWriteStatus::INVALID_CONFIG;
    return syncWorkspaceJournal_();
}

FLASHMEM bool SequencerPersistence::loadWorkspace(
    state::sequencer::SequencerTrackBankState& trackBank,
    state::sequencer::SequencerState& sequencer
) {
    auto buffer = makeEnvelopeBuffer();
    if (!buffer) return false;
    const auto latest = workspace_store_.loadLatest(
        buffer->bytes.data(),
        sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE
    );
    if (latest.status != SlotLoadStatus::OK) {
        return false;
    }

    if (!sequencer_codec::applyWorkspaceEnvelope(
            buffer->bytes.data(),
            latest.metadata.payloadSize,
            trackBank,
            sequencer
        )) {
        return false;
    }

    next_workspace_counter_ = latest.metadata.saveCounter + 1;
    next_workspace_slot_ =
        static_cast<uint16_t>((latest.slotIndex + 1) % WORKSPACE_SLOT_COUNT);
    return true;
}

FLASHMEM bool SequencerPersistence::saveWorkspace(
    const state::sequencer::SequencerTrackBankState& trackBank,
    const state::sequencer::SequencerState& sequencer
) {
    return saveWorkspaceStatus(trackBank, sequencer) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus SequencerPersistence::saveWorkspaceStatus(
    const state::sequencer::SequencerTrackBankState& trackBank,
    const state::sequencer::SequencerState& sequencer
) {
    auto buffer = makeEnvelopeBuffer();
    if (!buffer) return PersistenceWriteStatus::STORAGE_UNAVAILABLE;
    const auto encoded = sequencer_codec::fillWorkspaceEnvelope(
        trackBank,
        sequencer,
        buffer->bytes.data(),
        sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE
    );
    if (!encoded.ok) return PersistenceWriteStatus::PAYLOAD_TOO_LARGE;

    const auto status = workspace_store_.saveSlotStatus(
        next_workspace_slot_,
        buffer->bytes.data(),
        encoded.size,
        next_workspace_counter_
    );
    if (status != PersistenceWriteStatus::OK) return status;

    next_workspace_counter_ += 1;
    next_workspace_slot_ =
        static_cast<uint16_t>((next_workspace_slot_ + 1) % WORKSPACE_SLOT_COUNT);
    return PersistenceWriteStatus::OK;
}

FLASHMEM bool SequencerPersistence::savePatternSlot(
    uint8_t slotIndex,
    const state::sequencer::SequencerState& sequencer
) {
    return savePatternSlotStatus(slotIndex, sequencer) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus SequencerPersistence::savePatternSlotStatus(
    uint8_t slotIndex,
    const state::sequencer::SequencerState& sequencer
) {
    if (slotIndex >= PATTERN_LIBRARY_SLOT_COUNT) return PersistenceWriteStatus::OUT_OF_RANGE;

    auto buffer = makeEnvelopeBuffer();
    if (!buffer) return PersistenceWriteStatus::STORAGE_UNAVAILABLE;
    const auto encoded = sequencer_codec::fillPatternEnvelope(
        sequencer.pattern,
        buffer->bytes.data(),
        sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE
    );
    if (!encoded.ok) return PersistenceWriteStatus::PAYLOAD_TOO_LARGE;

    const uint32_t counter = static_cast<uint32_t>(slotIndex) + 1;
    return pattern_library_store_.saveSlotStatus(
        slotIndex,
        buffer->bytes.data(),
        encoded.size,
        counter
    );
}

FLASHMEM SlotLoadStatus SequencerPersistence::loadPatternSlot(
    uint8_t slotIndex,
    state::sequencer::SequencerState& sequencer
) {
    if (slotIndex >= PATTERN_LIBRARY_SLOT_COUNT) return SlotLoadStatus::OUT_OF_RANGE;

    auto buffer = makeEnvelopeBuffer();
    if (!buffer) return SlotLoadStatus::STORAGE_UNAVAILABLE;
    SlotMetadata metadata{};
    const SlotLoadStatus status = pattern_library_store_.loadSlot(
        slotIndex,
        buffer->bytes.data(),
        sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE,
        &metadata
    );
    if (status != SlotLoadStatus::OK) {
        return status;
    }

    if (!sequencer_codec::applyPatternEnvelope(
            buffer->bytes.data(),
            metadata.payloadSize,
            sequencer.pattern
        )) {
        return SlotLoadStatus::HEADER_MISMATCH;
    }

    const uint8_t len = sequencer.pattern.length.get();
    const uint8_t focused =
        (len == 0)
            ? 0
            : static_cast<uint8_t>(std::min<uint16_t>(sequencer.focusedStep.get(), len - 1U));
    sequencer.focusedStep.set(focused);
    sequencer.page.set(sequencer.pageForStep(sequencer.focusedStep.get()));
    return SlotLoadStatus::OK;
}

FLASHMEM bool SequencerPersistence::erasePatternSlot(uint8_t slotIndex) {
    return erasePatternSlotStatus(slotIndex) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus SequencerPersistence::erasePatternSlotStatus(uint8_t slotIndex) {
    if (slotIndex >= PATTERN_LIBRARY_SLOT_COUNT) return PersistenceWriteStatus::OUT_OF_RANGE;
    return pattern_library_store_.eraseSlotStatus(slotIndex);
}

FLASHMEM bool SequencerPersistence::saveSetSlot(
    uint8_t slotIndex,
    const state::sequencer::SequencerTrackBankState& trackBank,
    const state::sequencer::SequencerState& sequencer
) {
    return saveSetSlotStatus(slotIndex, trackBank, sequencer) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus SequencerPersistence::saveSetSlotStatus(
    uint8_t slotIndex,
    const state::sequencer::SequencerTrackBankState& trackBank,
    const state::sequencer::SequencerState& sequencer
) {
    if (slotIndex >= SET_LIBRARY_SLOT_COUNT) return PersistenceWriteStatus::OUT_OF_RANGE;

    auto buffer = makeEnvelopeBuffer();
    if (!buffer) return PersistenceWriteStatus::STORAGE_UNAVAILABLE;
    const auto encoded = sequencer_codec::fillSetEnvelope(
        trackBank,
        sequencer,
        buffer->bytes.data(),
        sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE
    );
    if (!encoded.ok) return PersistenceWriteStatus::PAYLOAD_TOO_LARGE;

    const uint32_t counter = static_cast<uint32_t>(slotIndex) + 1;
    return set_library_store_.saveSlotStatus(
        slotIndex,
        buffer->bytes.data(),
        encoded.size,
        counter
    );
}

FLASHMEM SlotLoadStatus SequencerPersistence::loadSetSlot(
    uint8_t slotIndex,
    state::sequencer::SequencerTrackBankState& trackBank,
    state::sequencer::SequencerState& sequencer
) {
    if (slotIndex >= SET_LIBRARY_SLOT_COUNT) return SlotLoadStatus::OUT_OF_RANGE;

    auto buffer = makeEnvelopeBuffer();
    if (!buffer) return SlotLoadStatus::STORAGE_UNAVAILABLE;
    SlotMetadata metadata{};
    const SlotLoadStatus status = set_library_store_.loadSlot(
        slotIndex,
        buffer->bytes.data(),
        sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE,
        &metadata
    );
    if (status != SlotLoadStatus::OK) {
        return status;
    }

    if (!sequencer_codec::applySetEnvelope(
            buffer->bytes.data(),
            metadata.payloadSize,
            trackBank,
            sequencer
        )) {
        return SlotLoadStatus::HEADER_MISMATCH;
    }

    return SlotLoadStatus::OK;
}

FLASHMEM bool SequencerPersistence::eraseSetSlot(uint8_t slotIndex) {
    return eraseSetSlotStatus(slotIndex) == PersistenceWriteStatus::OK;
}

FLASHMEM PersistenceWriteStatus SequencerPersistence::eraseSetSlotStatus(uint8_t slotIndex) {
    if (slotIndex >= SET_LIBRARY_SLOT_COUNT) return PersistenceWriteStatus::OUT_OF_RANGE;
    return set_library_store_.eraseSlotStatus(slotIndex);
}

FLASHMEM PersistenceWriteStatus SequencerPersistence::syncWorkspaceJournal_() {
    auto buffer = makeEnvelopeBuffer();
    if (!buffer) return PersistenceWriteStatus::STORAGE_UNAVAILABLE;
    const auto latest = workspace_store_.loadLatest(
        buffer->bytes.data(),
        sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE
    );
    if (latest.status == SlotLoadStatus::OK) {
        next_workspace_counter_ = latest.metadata.saveCounter + 1;
        next_workspace_slot_ =
            static_cast<uint16_t>((latest.slotIndex + 1) % WORKSPACE_SLOT_COUNT);
    } else {
        next_workspace_counter_ = 1;
        next_workspace_slot_ = 0;
    }
    return PersistenceWriteStatus::OK;
}

}  // namespace core::persistence
