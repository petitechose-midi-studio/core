#include "handler/sequencer/SequencerPatternPresetDomainServices.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/PatternPresetFactoryLibrary.hpp"
#include "persistence/ProductFileService.hpp"
#include "persistence/SequencerPatternPresetCodec.hpp"
#include "state/CoreState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

namespace {

namespace codec = core::persistence::sequencer_pattern_preset_codec;
namespace seq = core::state::sequencer;

struct PatternPresetBuffer {
    uint8_t bytes[codec::MAX_ENCODED_SIZE];
};

struct LoadedPatternPreset {
    core::app::ExtmemUniquePtr<PatternPresetBuffer> buffer{};
    core::app::ExtmemUniquePtr<seq::SequencerState> staged{};
    core::app::ExtmemUniquePtr<seq::DrumTrackState> drum{};
    seq::SequencerPatternPresetMetadata metadata{};
    seq::SequencerPatternPresetStatus codecStatus =
        seq::SequencerPatternPresetStatus::OK;
    SequencerPatternPresetDomainStatus status =
        SequencerPatternPresetDomainStatus::OK;
    oc::type::ErrorCode fileError = oc::type::ErrorCode::OK;
    uint16_t bytes = 0U;
    uint32_t assetHash = 0U;

    [[nodiscard]] bool ok() const {
        return status == SequencerPatternPresetDomainStatus::OK &&
            codecStatus == seq::SequencerPatternPresetStatus::OK &&
            fileError == oc::type::ErrorCode::OK && buffer && staged;
    }
};

FLASHMEM void copyText(
    char* target,
    size_t targetSize,
    const char* source
) {
    if (target == nullptr || targetSize == 0U) return;
    const char* text = source ? source : "";
    std::strncpy(target, text, targetSize - 1U);
    target[targetSize - 1U] = '\0';
}

FLASHMEM void defaultPatternPresetName(
    seq::SequencerTrackKind trackKind,
    const char* presetId,
    char* out,
    size_t outSize
) {
    if (out == nullptr || outSize == 0U) return;
    const char* suffix = presetId != nullptr
        ? std::strrchr(presetId, '-')
        : nullptr;
    suffix = suffix != nullptr && suffix[1] != '\0'
        ? suffix + 1
        : nullptr;
    const int written = suffix != nullptr
        ? std::snprintf(
              out,
              outSize,
              "%s Pattern %s",
              trackKind == seq::SequencerTrackKind::DRUM
                  ? "Drum"
                  : "Instrument",
              suffix
          )
        : -1;
    if (written <= 0 || static_cast<size_t>(written) >= outSize) {
        seq::sequencerPresetSemanticName(presetId, out, outSize);
    }
}

FLASHMEM SequencerPatternPresetDomainStatus statusFromFileError(
    oc::type::ErrorCode code
) {
    switch (code) {
        case oc::type::ErrorCode::RESOURCE_NOT_FOUND:
            return SequencerPatternPresetDomainStatus::EMPTY;
        case oc::type::ErrorCode::HARDWARE_BUSY:
            return SequencerPatternPresetDomainStatus::QUEUED;
        case oc::type::ErrorCode::INVALID_STATE:
        case oc::type::ErrorCode::HARDWARE_NOT_FOUND:
        case oc::type::ErrorCode::HARDWARE_INIT_FAILED:
            return SequencerPatternPresetDomainStatus::STORAGE_UNAVAILABLE;
        default:
            return SequencerPatternPresetDomainStatus::FAILED;
    }
}

FLASHMEM SequencerPatternPresetDomainStatus statusFromCodec(
    seq::SequencerPatternPresetStatus status
) {
    switch (status) {
        case seq::SequencerPatternPresetStatus::OK:
            return SequencerPatternPresetDomainStatus::OK;
        case seq::SequencerPatternPresetStatus::UNSUPPORTED_VERSION:
            return SequencerPatternPresetDomainStatus::UNSUPPORTED_VERSION;
        case seq::SequencerPatternPresetStatus::INCOMPATIBLE_TARGET:
            return SequencerPatternPresetDomainStatus::INCOMPATIBLE;
        case seq::SequencerPatternPresetStatus::RESOURCE_EXHAUSTED:
            return SequencerPatternPresetDomainStatus::ALLOCATION_UNAVAILABLE;
        default:
            return SequencerPatternPresetDomainStatus::CORRUPT;
    }
}

FLASHMEM uint32_t hashPresetPayload(
    const char* presetId,
    const uint8_t* bytes,
    uint16_t size
) {
    uint32_t hash = seq::sequencerPresetIdHash(presetId);
    constexpr uint32_t FNV_PRIME = 16777619U;
    for (uint16_t i = 0U; i < size; ++i) {
        hash = (hash ^ bytes[i]) * FNV_PRIME;
    }
    return hash == 0U ? 1U : hash;
}

FLASHMEM LoadedPatternPreset loadPreset(
    core::persistence::ProductFileService& files,
    core::persistence::ProductDirectoryCatalog& catalog,
    const char* presetId
) {
    LoadedPatternPreset loaded{};
    loaded.buffer = core::app::makeExtmemUniqueForOverwrite<PatternPresetBuffer>();
    loaded.staged = core::app::makeExtmemUnique<seq::SequencerState>();
    if (!loaded.buffer || !loaded.staged) {
        loaded.status = SequencerPatternPresetDomainStatus::ALLOCATION_UNAVAILABLE;
        loaded.codecStatus = seq::SequencerPatternPresetStatus::RESOURCE_EXHAUSTED;
        return loaded;
    }

    core::persistence::PatternPresetFactoryDescriptor factory{};
    if (core::persistence::PatternPresetFactoryLibrary::describe(
            presetId,
            factory
        )) {
        if (factory.trackKind == seq::SequencerTrackKind::DRUM) {
            loaded.drum = core::app::makeExtmemUnique<seq::DrumTrackState>();
            if (!loaded.drum) {
                loaded.status =
                    SequencerPatternPresetDomainStatus::ALLOCATION_UNAVAILABLE;
                loaded.codecStatus =
                    seq::SequencerPatternPresetStatus::RESOURCE_EXHAUSTED;
                return loaded;
            }
        }
        const auto encoded =
            core::persistence::PatternPresetFactoryLibrary::encode(
                presetId,
                loaded.staged->pattern,
                loaded.drum.get(),
                loaded.metadata,
                loaded.buffer->bytes,
                codec::MAX_ENCODED_SIZE
            );
        loaded.codecStatus = encoded.status;
        loaded.bytes = encoded.bytesWritten;
        loaded.status = statusFromCodec(encoded.status);
        if (encoded.ok()) {
            loaded.assetHash = hashPresetPayload(
                presetId,
                loaded.buffer->bytes,
                loaded.bytes
            );
        }
        return loaded;
    }

    core::persistence::PatternPresetFileStore store(files, catalog);
    const auto result = store.load(
        presetId,
        loaded.buffer->bytes,
        codec::MAX_ENCODED_SIZE,
        loaded.bytes
    );
    if (!result) {
        loaded.status = statusFromFileError(result.error().code);
        loaded.fileError = result.error().code;
        return loaded;
    }
    loaded.assetHash = hashPresetPayload(
        presetId,
        loaded.buffer->bytes,
        loaded.bytes
    );

    codec::MetadataView metadataView{};
    if (!codec::decodeMetadata(
            loaded.buffer->bytes,
            loaded.bytes,
            metadataView,
            &loaded.codecStatus
        )) {
        loaded.status = statusFromCodec(loaded.codecStatus);
        return loaded;
    }
    if (metadataView.metadata.trackKind == seq::SequencerTrackKind::DRUM) {
        loaded.drum = core::app::makeExtmemUnique<seq::DrumTrackState>();
        if (!loaded.drum) {
            loaded.status =
                SequencerPatternPresetDomainStatus::ALLOCATION_UNAVAILABLE;
            loaded.codecStatus =
                seq::SequencerPatternPresetStatus::RESOURCE_EXHAUSTED;
            return loaded;
        }
    }
    if (!codec::decode(
            loaded.buffer->bytes,
            loaded.bytes,
            loaded.metadata,
            loaded.staged->pattern,
            loaded.drum.get(),
            &loaded.codecStatus
        )) {
        loaded.status = statusFromCodec(loaded.codecStatus);
        return loaded;
    }
    if (std::strcmp(loaded.metadata.technicalId, presetId) != 0) {
        loaded.status = SequencerPatternPresetDomainStatus::CORRUPT;
        loaded.codecStatus = seq::SequencerPatternPresetStatus::INVALID_FORMAT;
    }
    return loaded;
}

FLASHMEM bool sameTarget(
    const seq::SequencerPatternPresetTarget& lhs,
    const seq::SequencerPatternPresetTarget& rhs
) {
    return lhs.valid == rhs.valid &&
        lhs.trackIndex == rhs.trackIndex &&
        lhs.trackKind == rhs.trackKind &&
        lhs.projectRevision == rhs.projectRevision;
}

FLASHMEM seq::SequencerPatternPresetCompatibility compatibilityFor(
    const LoadedPatternPreset& loaded,
    const seq::SequencerPatternPresetTarget& target,
    const core::state::CoreState& state,
    bool targetMatches
) {
    if (!loaded.ok()) {
        return loaded.status ==
                SequencerPatternPresetDomainStatus::STORAGE_UNAVAILABLE
            ? seq::SequencerPatternPresetCompatibility::STORAGE_UNAVAILABLE
            : seq::SequencerPatternPresetCompatibility::CORRUPT;
    }
    if (!targetMatches) {
        return seq::SequencerPatternPresetCompatibility::STALE_TARGET;
    }
    if (loaded.metadata.trackKind != target.trackKind) {
        return seq::SequencerPatternPresetCompatibility::WRONG_TRACK_KIND;
    }
    if (target.trackKind == seq::SequencerTrackKind::DRUM &&
        (!loaded.drum || !seq::sequencerPatternPresetDrumKitCompatible(
            *loaded.drum,
            state.sequencerTracks.drumTrack(target.trackIndex)
        ))) {
        return seq::SequencerPatternPresetCompatibility::INCOMPATIBLE_DRUM_KIT;
    }
    return seq::SequencerPatternPresetCompatibility::READY;
}

FLASHMEM SequencerPatternPresetDomainStatus statusForCompatibility(
    seq::SequencerPatternPresetCompatibility compatibility
) {
    switch (compatibility) {
        case seq::SequencerPatternPresetCompatibility::READY:
            return SequencerPatternPresetDomainStatus::OK;
        case seq::SequencerPatternPresetCompatibility::STALE_TARGET:
            return SequencerPatternPresetDomainStatus::STALE_TARGET;
        case seq::SequencerPatternPresetCompatibility::STORAGE_UNAVAILABLE:
            return SequencerPatternPresetDomainStatus::STORAGE_UNAVAILABLE;
        case seq::SequencerPatternPresetCompatibility::CORRUPT:
            return SequencerPatternPresetDomainStatus::CORRUPT;
        default:
            return SequencerPatternPresetDomainStatus::INCOMPATIBLE;
    }
}

FLASHMEM bool prepareActivation(
    core::state::CoreState& state,
    uint8_t trackIndex,
    uint16_t& enabledMask,
    uint16_t& audibleMask,
    seq::SequencerTrackActivationBatch& batch
) {
    enabledMask = state.sequencerTracks.currentEnabledMask();
    audibleMask = core::state::project::audibleMask(
        state.projectTracks,
        enabledMask
    );
    return state.sequencerTrackActivations.prepare(
        static_cast<uint16_t>(1U << trackIndex),
        audibleMask,
        state.statusBar.playing.get(),
        batch,
        seq::SequencerTrackActivationOrigin::PRESET
    );
}

FLASHMEM void setActivationResult(
    const seq::SequencerTrackActivationBatch& batch,
    uint8_t trackIndex,
    SequencerPatternPresetActionResult& result
) {
    const uint16_t trackBit = static_cast<uint16_t>(1U << trackIndex);
    result.activationGeneration = batch.generation;
    result.activation = (batch.localLoopBoundaryMask & trackBit) != 0U
        ? SequencerPatternPresetActivation::QUEUED
        : SequencerPatternPresetActivation::APPLIED;
    result.status = result.activation == SequencerPatternPresetActivation::QUEUED
        ? SequencerPatternPresetDomainStatus::QUEUED
        : SequencerPatternPresetDomainStatus::OK;
}

FLASHMEM bool finalTargetMatches(
    const SequencerPatternPresetDomainServices& services,
    const seq::SequencerPatternPresetTarget& target,
    const core::state::CoreState& state,
    uint16_t enabledMask,
    uint16_t audibleMask
) {
    return services.targetMatches(target) &&
        services.projectRevision() == target.projectRevision &&
        state.sequencerTracks.currentEnabledMask() == enabledMask &&
        core::state::project::audibleMask(state.projectTracks, enabledMask) ==
            audibleMask;
}

using PatternPresetEntry =
    core::persistence::PatternPresetFileListEntry;
using PatternPresetDirection =
    core::persistence::PatternPresetFilePageDirection;

FLASHMEM bool factoryPresetIndex(
    seq::SequencerTrackKind trackKind,
    const char* presetId,
    uint8_t& outIndex
) {
    core::persistence::PatternPresetFactoryDescriptor descriptor{};
    const uint8_t count =
        core::persistence::PatternPresetFactoryLibrary::count(trackKind);
    for (uint8_t index = 0U; index < count; ++index) {
        if (!core::persistence::PatternPresetFactoryLibrary::descriptorAt(
                trackKind,
                index,
                descriptor
            )) {
            continue;
        }
        if (presetId != nullptr &&
            std::strcmp(descriptor.id, presetId) == 0) {
            outIndex = index;
            return true;
        }
    }
    return false;
}

FLASHMEM bool copyFactoryPresetEntry(
    seq::SequencerTrackKind trackKind,
    uint8_t index,
    PatternPresetEntry& out
) {
    core::persistence::PatternPresetFactoryDescriptor descriptor{};
    if (!core::persistence::PatternPresetFactoryLibrary::descriptorAt(
            trackKind,
            index,
            descriptor
        )) {
        return false;
    }
    out = {};
    copyText(out.id, sizeof(out.id), descriptor.id);
    copyText(
        out.semanticName,
        sizeof(out.semanticName),
        descriptor.semanticName
    );
    out.metadataReadable = true;
    return true;
}

FLASHMEM SequencerPatternPresetListResult listFactoryPresets(
    PatternPresetEntry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    PatternPresetDirection direction,
    seq::SequencerTrackKind trackKind
) {
    SequencerPatternPresetListResult result{};
    const uint8_t total =
        core::persistence::PatternPresetFactoryLibrary::count(trackKind);
    result.totalCount = total;
    if (capacity == 0U) return result;
    if (entries == nullptr) {
        result.status = SequencerPatternPresetDomainStatus::FAILED;
        result.fileError = oc::type::ErrorCode::INVALID_ARGUMENT;
        return result;
    }

    const bool hasAnchor =
        anchorExclusive != nullptr && anchorExclusive[0] != '\0';
    uint8_t anchorIndex = 0U;
    if (hasAnchor &&
        !factoryPresetIndex(trackKind, anchorExclusive, anchorIndex)) {
        result.status = SequencerPatternPresetDomainStatus::FAILED;
        result.fileError = oc::type::ErrorCode::INVALID_ARGUMENT;
        return result;
    }
    if (direction == PatternPresetDirection::BACKWARD && !hasAnchor) {
        result.status = SequencerPatternPresetDomainStatus::FAILED;
        result.fileError = oc::type::ErrorCode::INVALID_ARGUMENT;
        return result;
    }

    uint8_t begin = 0U;
    uint8_t end = total;
    if (direction == PatternPresetDirection::FORWARD) {
        begin = hasAnchor ? static_cast<uint8_t>(anchorIndex + 1U) : 0U;
        end = static_cast<uint8_t>(std::min<unsigned>(
            total,
            static_cast<unsigned>(begin) + capacity
        ));
    } else {
        end = anchorIndex;
        begin = end > capacity ? static_cast<uint8_t>(end - capacity) : 0U;
    }
    for (uint8_t index = begin; index < end; ++index) {
        if (copyFactoryPresetEntry(
                trackKind,
                index,
                entries[result.count]
            )) {
            ++result.count;
        }
    }
    result.hasPrevious = begin > 0U;
    result.hasNext = end < total;
    result.truncated = result.hasPrevious || result.hasNext;
    return result;
}

FLASHMEM uint16_t combinedPresetCount(
    uint8_t factoryCount,
    uint16_t userCount
) {
    return static_cast<uint16_t>(std::min<uint32_t>(
        UINT16_MAX,
        static_cast<uint32_t>(factoryCount) + userCount
    ));
}

struct UserPatternPresetPage {
    bool ok = false;
    oc::type::ErrorCode error = oc::type::ErrorCode::OK;
    core::persistence::PatternPresetFileListResult page{};
};

FLASHMEM UserPatternPresetPage listUserPresets(
    core::persistence::PatternPresetFileStore& store,
    PatternPresetEntry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    PatternPresetDirection direction
) {
    const auto listed = store.listPage(
        entries,
        capacity,
        anchorExclusive,
        direction
    );
    if (listed) return {true, oc::type::ErrorCode::OK, listed.value()};
    if (listed.error().code == oc::type::ErrorCode::RESOURCE_NOT_FOUND &&
        (anchorExclusive == nullptr || anchorExclusive[0] == '\0')) {
        return {true, oc::type::ErrorCode::OK, {}};
    }
    return {false, listed.error().code, {}};
}

}  // namespace

FLASHMEM SequencerPatternPresetDomainServices::
SequencerPatternPresetDomainServices(
    core::state::CoreState& state,
    core::persistence::ProductFileService& files,
    core::persistence::ProductDirectoryCatalog& catalog
) : state_(&state), files_(&files), catalog_(&catalog) {}

FLASHMEM SequencerPatternPresetDomainServices
SequencerPatternPresetDomainServices::fromCoreState(
    core::state::CoreState& state,
    core::persistence::ProductFileService& files,
    core::persistence::ProductDirectoryCatalog& catalog
) {
    return {state, files, catalog};
}

FLASHMEM SequencerPatternPresetListResult
SequencerPatternPresetDomainServices::listPresetsPage(
    Entry* entries,
    uint8_t capacity,
    const char* anchorExclusive,
    core::persistence::PatternPresetFilePageDirection direction,
    seq::SequencerPatternPresetSourceFilter filter,
    seq::SequencerTrackKind trackKind
) const {
    OC_PERF_SCOPE(perf, "persistence.pattern-preset.list-page");
    SequencerPatternPresetListResult result{};
    if (filter == seq::SequencerPatternPresetSourceFilter::FACTORY) {
        result = listFactoryPresets(
            entries,
            capacity,
            anchorExclusive,
            direction,
            trackKind
        );
        OC_PERF_UNITS(perf, result.count, result.totalCount);
        return result;
    }
    if (files_ == nullptr || catalog_ == nullptr) {
        result.status = SequencerPatternPresetDomainStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }
    core::persistence::PatternPresetFileStore store(*files_, *catalog_);

    if (filter == seq::SequencerPatternPresetSourceFilter::ALL) {
        if (entries == nullptr && capacity > 0U) {
            result.status = SequencerPatternPresetDomainStatus::FAILED;
            result.fileError = oc::type::ErrorCode::INVALID_ARGUMENT;
            return result;
        }
        const uint8_t factoryCount =
            core::persistence::PatternPresetFactoryLibrary::count(trackKind);
        const bool hasAnchor =
            anchorExclusive != nullptr && anchorExclusive[0] != '\0';
        uint8_t factoryAnchor = 0U;
        const bool anchorIsFactory = hasAnchor && factoryPresetIndex(
            trackKind,
            anchorExclusive,
            factoryAnchor
        );

        if (direction == PatternPresetDirection::BACKWARD &&
            anchorIsFactory) {
            result = listFactoryPresets(
                entries,
                capacity,
                anchorExclusive,
                direction,
                trackKind
            );
            Entry probe{};
            const auto users = listUserPresets(
                store,
                &probe,
                1U,
                nullptr,
                PatternPresetDirection::FORWARD
            );
            if (!users.ok) {
                result.status = statusFromFileError(users.error);
                result.fileError = users.error;
                return result;
            }
            result.totalCount = combinedPresetCount(
                factoryCount,
                users.page.totalCount
            );
            result.hasNext = true;
            result.truncated = result.hasPrevious || result.hasNext;
            OC_PERF_UNITS(perf, result.count, result.totalCount);
            return result;
        }

        if (direction == PatternPresetDirection::BACKWARD && hasAnchor) {
            const auto users = listUserPresets(
                store,
                entries,
                capacity,
                anchorExclusive,
                direction
            );
            if (!users.ok) {
                result.status = statusFromFileError(users.error);
                result.fileError = users.error;
                return result;
            }
            const auto& userPage = users.page;
            const uint8_t prependCount = static_cast<uint8_t>(
                std::min<unsigned>(
                    factoryCount,
                    capacity - userPage.count
                )
            );
            for (uint8_t index = userPage.count; index > 0U; --index) {
                entries[index - 1U + prependCount] = entries[index - 1U];
            }
            const uint8_t factoryBegin = static_cast<uint8_t>(
                factoryCount - prependCount
            );
            for (uint8_t index = 0U; index < prependCount; ++index) {
                (void)copyFactoryPresetEntry(
                    trackKind,
                    static_cast<uint8_t>(factoryBegin + index),
                    entries[index]
                );
            }
            result.count = static_cast<uint8_t>(
                userPage.count + prependCount
            );
            result.totalCount = combinedPresetCount(
                factoryCount,
                userPage.totalCount
            );
            result.hasPrevious = userPage.hasPrevious || factoryBegin > 0U;
            result.hasNext = true;
            result.truncated = result.hasPrevious || result.hasNext;
            OC_PERF_UNITS(perf, result.count, result.totalCount);
            return result;
        }

        if (direction == PatternPresetDirection::FORWARD &&
            (!hasAnchor || anchorIsFactory)) {
            uint8_t factoryBegin = 0U;
            if (anchorIsFactory) {
                factoryBegin = static_cast<uint8_t>(factoryAnchor + 1U);
            }
            while (factoryBegin < factoryCount && result.count < capacity) {
                if (copyFactoryPresetEntry(
                        trackKind,
                        factoryBegin,
                        entries[result.count]
                    )) {
                    ++result.count;
                }
                ++factoryBegin;
            }

            Entry probe{};
            const uint8_t remaining = static_cast<uint8_t>(
                capacity - result.count
            );
            Entry* userEntries = remaining > 0U
                ? entries + result.count
                : &probe;
            const auto users = listUserPresets(
                store,
                userEntries,
                remaining > 0U ? remaining : 1U,
                nullptr,
                PatternPresetDirection::FORWARD
            );
            if (!users.ok) {
                result.status = statusFromFileError(users.error);
                result.fileError = users.error;
                return result;
            }
            const auto& userPage = users.page;
            if (remaining > 0U) {
                result.count = static_cast<uint8_t>(
                    result.count + userPage.count
                );
            }
            result.totalCount = combinedPresetCount(
                factoryCount,
                userPage.totalCount
            );
            result.hasPrevious = hasAnchor;
            result.hasNext = factoryBegin < factoryCount ||
                userPage.totalCount > (remaining > 0U ? userPage.count : 0U);
            result.truncated = result.hasPrevious || result.hasNext;
            OC_PERF_UNITS(perf, result.count, result.totalCount);
            return result;
        }

        if (direction == PatternPresetDirection::FORWARD && hasAnchor) {
            const auto users = listUserPresets(
                store,
                entries,
                capacity,
                anchorExclusive,
                direction
            );
            if (!users.ok) {
                result.status = statusFromFileError(users.error);
                result.fileError = users.error;
                return result;
            }
            const auto& userPage = users.page;
            result.count = userPage.count;
            result.totalCount = combinedPresetCount(
                factoryCount,
                userPage.totalCount
            );
            result.hasPrevious = factoryCount > 0U || userPage.hasPrevious;
            result.hasNext = userPage.hasNext;
            result.truncated = result.hasPrevious || result.hasNext;
            OC_PERF_UNITS(perf, result.count, result.totalCount);
            return result;
        }
    }

    const auto listed = listUserPresets(
        store,
        entries,
        capacity,
        anchorExclusive,
        direction
    );
    if (!listed.ok) {
        result.status = statusFromFileError(listed.error);
        result.fileError = listed.error;
        return result;
    }
    const auto& page = listed.page;
    result.count = page.count;
    result.truncated = page.truncated;
    result.hasPrevious = page.hasPrevious;
    result.hasNext = page.hasNext;
    result.totalCount = page.totalCount;
    OC_PERF_UNITS(perf, page.count, page.totalCount);
    return result;
}

FLASHMEM SequencerPatternPresetActionResult
SequencerPatternPresetDomainServices::nextPresetId(
    char* out,
    size_t outSize
) const {
    SequencerPatternPresetActionResult result{};
    if (files_ == nullptr || catalog_ == nullptr) {
        result.status = SequencerPatternPresetDomainStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }
    core::persistence::PatternPresetFileStore store(*files_, *catalog_);
    const auto next = store.nextPresetId(out, outSize);
    if (!next) {
        result.status = statusFromFileError(next.error().code);
        result.fileError = next.error().code;
        return result;
    }
    copyText(result.presetId, sizeof(result.presetId), out);
    return result;
}

FLASHMEM seq::SequencerPatternPresetTarget
SequencerPatternPresetDomainServices::captureTarget() const {
    seq::SequencerPatternPresetTarget target{};
    if (state_ == nullptr) return target;
    target.trackIndex = state_->sequencerTracks.activeTrackIndex();
    target.trackKind = state_->sequencerTracks.trackKind(target.trackIndex);
    target.projectRevision = projectRevision();
    target.valid = state_->sequencerTracks.isTrackEnabled(target.trackIndex);
    return target;
}

FLASHMEM bool SequencerPatternPresetDomainServices::targetMatches(
    const seq::SequencerPatternPresetTarget& target
) const {
    return state_ != nullptr && target.valid &&
        sameTarget(captureTarget(), target);
}

FLASHMEM bool SequencerPatternPresetDomainServices::playbackActive() const {
    return state_ != nullptr && state_->statusBar.playing.get();
}

FLASHMEM uint32_t SequencerPatternPresetDomainServices::projectRevision() const {
    return state_ ? state_->project.metadata.modifiedCounter : 0U;
}

FLASHMEM SequencerPatternPresetInspectResult
SequencerPatternPresetDomainServices::inspectPreset(
    const char* presetId,
    const seq::SequencerPatternPresetTarget& target
) const {
    OC_PERF_SCOPE(perf, "persistence.pattern-preset.inspect");
    SequencerPatternPresetInspectResult result{};
    auto& descriptor = result.descriptor;
    descriptor.valid = true;
    descriptor.source =
        core::persistence::PatternPresetFactoryLibrary::contains(presetId)
        ? seq::SequencerPatternPresetSource::FACTORY
        : seq::SequencerPatternPresetSource::USER;
    descriptor.previewKey.targetHash = seq::sequencerPatternPresetTargetHash(target);
    descriptor.previewKey.projectRevision = target.projectRevision;
    if (state_ == nullptr || files_ == nullptr || catalog_ == nullptr) {
        result.status = SequencerPatternPresetDomainStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        descriptor.compatibility =
            seq::SequencerPatternPresetCompatibility::STORAGE_UNAVAILABLE;
        return result;
    }
    auto loaded = loadPreset(*files_, *catalog_, presetId);
    result.status = loaded.status;
    result.codecStatus = loaded.codecStatus;
    result.fileError = loaded.fileError;
    result.bytes = loaded.bytes;
    descriptor.previewKey.assetHash = loaded.assetHash;
    if (!loaded.ok()) {
        descriptor.compatibility = compatibilityFor(
            loaded,
            target,
            *state_,
            false
        );
        return result;
    }

    descriptor.metadata = loaded.metadata;
    descriptor.patternLength = loaded.staged->pattern.length.get();
    descriptor.stepsPerBeat = loaded.staged->pattern.stepsPerBeat.get();
    descriptor.drumLaneCount = loaded.drum ? loaded.drum->kit.laneCount : 0U;
    descriptor.compatibility = compatibilityFor(
        loaded,
        target,
        *state_,
        targetMatches(target)
    );
    result.status = statusForCompatibility(descriptor.compatibility);
    OC_PERF_UNITS(perf, result.bytes, descriptor.drumLaneCount);
    return result;
}

FLASHMEM SequencerPatternPresetActionResult
SequencerPatternPresetDomainServices::savePreset(
    const char* presetId,
    const seq::SequencerPatternPresetTarget& target,
    bool allowOverwrite
) const {
    OC_PERF_SCOPE(perf, "persistence.pattern-preset.save");
    SequencerPatternPresetActionResult result{};
    copyText(result.presetId, sizeof(result.presetId), presetId);
    if (core::persistence::PatternPresetFactoryLibrary::contains(presetId)) {
        result.status = SequencerPatternPresetDomainStatus::READ_ONLY;
        return result;
    }
    if (state_ == nullptr || files_ == nullptr || catalog_ == nullptr) {
        result.status = SequencerPatternPresetDomainStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }
    if (!targetMatches(target)) {
        result.status = SequencerPatternPresetDomainStatus::STALE_TARGET;
        return result;
    }

    core::persistence::PatternPresetFileStore store(*files_, *catalog_);
    const auto existing = store.exists(presetId);
    if (!existing) {
        result.status = statusFromFileError(existing.error().code);
        result.fileError = existing.error().code;
        return result;
    }
    if (existing.value() && !allowOverwrite) {
        result.status = SequencerPatternPresetDomainStatus::COLLISION;
        return result;
    }

    auto buffer = core::app::makeExtmemUniqueForOverwrite<PatternPresetBuffer>();
    if (!buffer) {
        result.status = SequencerPatternPresetDomainStatus::ALLOCATION_UNAVAILABLE;
        result.codecStatus = seq::SequencerPatternPresetStatus::RESOURCE_EXHAUSTED;
        return result;
    }
    char semanticName[seq::SEQUENCER_PRESET_SEMANTIC_NAME_SIZE]{};
    defaultPatternPresetName(
        target.trackKind,
        presetId,
        semanticName,
        sizeof(semanticName)
    );
    if (existing.value()) {
        uint16_t existingSize = 0U;
        const auto loaded = store.load(
            presetId,
            buffer->bytes,
            codec::MAX_ENCODED_SIZE,
            existingSize
        );
        codec::MetadataView existingMetadata{};
        if (loaded && codec::decodeMetadata(
                buffer->bytes,
                existingSize,
                existingMetadata
            ) &&
            std::strcmp(
                existingMetadata.metadata.technicalId,
                presetId
            ) == 0) {
            copyText(
                semanticName,
                sizeof(semanticName),
                existingMetadata.metadata.semanticName
            );
        }
    }

    seq::SequencerPatternPresetMetadata metadata{};
    if (!seq::setSequencerPatternPresetMetadata(
            metadata,
            target.trackKind,
            presetId,
            semanticName
        )) {
        result.status = SequencerPatternPresetDomainStatus::FAILED;
        result.codecStatus = seq::SequencerPatternPresetStatus::INVALID_ARGUMENT;
        return result;
    }
    const auto& pattern = seq::canonicalTrackPattern(
        state_->sequencerTracks,
        state_->sequencer,
        target.trackIndex
    );
    const auto* drum = target.trackKind == seq::SequencerTrackKind::DRUM
        ? &state_->sequencerTracks.drumTrack(target.trackIndex)
        : nullptr;
    const auto encoded = codec::encode(
        metadata,
        pattern,
        drum,
        buffer->bytes,
        codec::MAX_ENCODED_SIZE
    );
    result.codecStatus = encoded.status;
    result.bytes = encoded.bytesWritten;
    if (!encoded.ok()) {
        result.status = statusFromCodec(encoded.status);
        return result;
    }
    if (!targetMatches(target)) {
        result.status = SequencerPatternPresetDomainStatus::STALE_TARGET;
        return result;
    }
    const auto saved = store.save(
        presetId,
        buffer->bytes,
        encoded.bytesWritten
    );
    if (!saved) {
        result.status = statusFromFileError(saved.error().code);
        result.fileError = saved.error().code;
        return result;
    }
    result.bytes = static_cast<uint16_t>(saved.value().bytes);
    result.activation = SequencerPatternPresetActivation::APPLIED;
    OC_PERF_UNITS(perf, result.bytes, static_cast<uint32_t>(target.trackKind));
    return result;
}

FLASHMEM SequencerPatternPresetActionResult
SequencerPatternPresetDomainServices::applyPreset(
    const char* presetId,
    const seq::SequencerPatternPresetTarget& target,
    const seq::SequencerPatternPresetPreviewKey& expectedPreview
) const {
    OC_PERF_SCOPE(perf, "persistence.pattern-preset.load-apply");
    SequencerPatternPresetActionResult result{};
    copyText(result.presetId, sizeof(result.presetId), presetId);
    if (state_ == nullptr || files_ == nullptr || catalog_ == nullptr) {
        result.status = SequencerPatternPresetDomainStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }
    if (state_->hasPendingSequencerPatternHistoryCoalescing()) {
        result.status = SequencerPatternPresetDomainStatus::STALE_TARGET;
        return result;
    }

    auto loaded = loadPreset(*files_, *catalog_, presetId);
    result.status = loaded.status;
    result.codecStatus = loaded.codecStatus;
    result.fileError = loaded.fileError;
    result.bytes = loaded.bytes;
    if (!loaded.ok()) return result;

    const seq::SequencerPatternPresetPreviewKey actualPreview{
        .assetHash = loaded.assetHash,
        .targetHash = seq::sequencerPatternPresetTargetHash(target),
        .projectRevision = target.projectRevision,
    };
    const auto compatibility = compatibilityFor(
        loaded,
        target,
        *state_,
        targetMatches(target)
    );
    if (actualPreview != expectedPreview ||
        !seq::sequencerPatternPresetCanApply(compatibility)) {
        result.status = actualPreview != expectedPreview
            ? SequencerPatternPresetDomainStatus::STALE_TARGET
            : statusForCompatibility(compatibility);
        return result;
    }
    loaded.buffer.reset();

    const seq::SequencerHistoryDescriptor descriptor{
        .kind = seq::SequencerHistoryActionKind::PatternPreset,
        .trackIndex = target.trackIndex,
    };
    uint16_t enabledMask = 0U;
    uint16_t audibleMask = 0U;
    seq::SequencerTrackActivationBatch activation{};

    if (target.trackKind == seq::SequencerTrackKind::DRUM) {
        auto change = seq::prepareHistoryDrumChangeBefore(
            state_->sequencerTracks,
            state_->sequencer,
            target.trackIndex,
            descriptor
        );
        if (!change || !loaded.drum) {
            result.status =
                SequencerPatternPresetDomainStatus::ALLOCATION_UNAVAILABLE;
            result.codecStatus =
                seq::SequencerPatternPresetStatus::RESOURCE_EXHAUSTED;
            return result;
        }

        loaded.drum->kit =
            state_->sequencerTracks.drumTrack(target.trackIndex).kit;
        const auto& candidate = *loaded.drum;
        const auto* sourceGraph = seq::graphView(loaded.staged->pattern);
        if (!seq::captureDetachedHistoryDrumAfter(
                *change,
                seq::SequencerTrackKind::DRUM,
                candidate,
                sourceGraph,
                loaded.staged->pattern.graphRevision.get()
            )) {
            result.status =
                SequencerPatternPresetDomainStatus::ALLOCATION_UNAVAILABLE;
            result.codecStatus =
                seq::SequencerPatternPresetStatus::RESOURCE_EXHAUSTED;
            return result;
        }
        if (seq::sameMusicalHistoryDrumChange(*change)) {
            result.activation = SequencerPatternPresetActivation::APPLIED;
            return result;
        }

        core::app::ExtmemUniquePtr<
            oc::note::sequencer::StepSequencerGraph
        > bankGraph;
        if (!core::state::cloneSequencerGraph(bankGraph, sourceGraph)) {
            result.status =
                SequencerPatternPresetDomainStatus::ALLOCATION_UNAVAILABLE;
            result.codecStatus =
                seq::SequencerPatternPresetStatus::RESOURCE_EXHAUSTED;
            return result;
        }
        if (!prepareActivation(
                *state_,
                target.trackIndex,
                enabledMask,
                audibleMask,
                activation
            )) {
            result.status = SequencerPatternPresetDomainStatus::STALE_TARGET;
            return result;
        }
        change->activation.reference = seq::activationHistoryRef(activation);
        change->activation.targetAudibleMask = audibleMask;
        if (!state_->sequencerHistory.canRecordDrum(*change)) {
            result.status = SequencerPatternPresetDomainStatus::HISTORY_UNAVAILABLE;
            return result;
        }
        if (!finalTargetMatches(
                *this,
                target,
                *state_,
                enabledMask,
                audibleMask
            ) ||
            !state_->sequencerTrackActivations.armPrepared(activation)) {
            result.status = SequencerPatternPresetDomainStatus::STALE_TARGET;
            return result;
        }

        state_->sequencerTracks.restoreDrumTrack(
            target.trackIndex,
            seq::SequencerTrackKind::DRUM,
            candidate
        );
        state_->sequencer.pattern.graph =
            std::move(loaded.staged->pattern.graph);
        state_->sequencer.pattern.graphRevision.set(
            loaded.staged->pattern.graphRevision.get()
        );
        state_->sequencerTracks.track(target.trackIndex).graph =
            std::move(bankGraph);
        state_->sequencerTracks.track(target.trackIndex).graphRevision.set(
            loaded.staged->pattern.graphRevision.get()
        );
        seq::refreshContentView(state_->sequencer);
        state_->sequencer.drumSequencer.bump();
        state_->sequencer.invalidateVariationTelemetry();
        state_->sequencerHistory.recordPreparedDrum(std::move(change));
    } else {
        auto change = core::app::makeExtmemUnique<
            seq::SequencerHistoryPatternChange
        >();
        if (!change ||
            !seq::captureHistorySnapshot(state_->sequencer, change->before) ||
            !seq::captureHistorySnapshot(*loaded.staged, change->after)) {
            result.status =
                SequencerPatternPresetDomainStatus::ALLOCATION_UNAVAILABLE;
            result.codecStatus =
                seq::SequencerPatternPresetStatus::RESOURCE_EXHAUSTED;
            return result;
        }
        change->trackIndex = target.trackIndex;
        change->descriptor = descriptor;
        change->storage = seq::SequencerHistoryPatternStorage::FullGraph;
        if (seq::sameMusicalHistorySnapshot(change->before, change->after)) {
            result.activation = SequencerPatternPresetActivation::APPLIED;
            return result;
        }

        seq::SequencerPatternSnapshot flat{};
        seq::captureSnapshot(loaded.staged->pattern, flat);
        core::app::ExtmemUniquePtr<
            oc::note::sequencer::StepSequencerGraph
        > bankGraph;
        seq::SequencerCcLaneBankPtr bankCcLanes;
        if (!core::state::cloneSequencerGraph(
                bankGraph,
                seq::graphView(loaded.staged->pattern)
            ) ||
            !seq::cloneSequencerCcLaneBank(
                bankCcLanes,
                seq::sequencerCcLaneView(loaded.staged->pattern)
            )) {
            result.status =
                SequencerPatternPresetDomainStatus::ALLOCATION_UNAVAILABLE;
            result.codecStatus =
                seq::SequencerPatternPresetStatus::RESOURCE_EXHAUSTED;
            return result;
        }
        if (!prepareActivation(
                *state_,
                target.trackIndex,
                enabledMask,
                audibleMask,
                activation
            )) {
            result.status = SequencerPatternPresetDomainStatus::STALE_TARGET;
            return result;
        }
        change->auxiliary.activation.reference =
            seq::activationHistoryRef(activation);
        change->auxiliary.activation.targetAudibleMask = audibleMask;
        if (!state_->sequencerHistory.canRecordPattern(*change)) {
            result.status = SequencerPatternPresetDomainStatus::HISTORY_UNAVAILABLE;
            return result;
        }
        if (!finalTargetMatches(
                *this,
                target,
                *state_,
                enabledMask,
                audibleMask
            ) ||
            !state_->sequencerTrackActivations.armPrepared(activation)) {
            result.status = SequencerPatternPresetDomainStatus::STALE_TARGET;
            return result;
        }

        auto editorGraph = std::move(loaded.staged->pattern.graph);
        auto editorCcLanes = std::move(loaded.staged->pattern.ccLanes);
        seq::installTrackContentSnapshotToEditorWithOwnedPayload(
            state_->sequencer,
            flat,
            std::move(editorGraph),
            std::move(editorCcLanes)
        );
        seq::installTrackContentSnapshotWithOwnedPayload(
            state_->sequencerTracks.track(target.trackIndex),
            flat,
            std::move(bankGraph),
            std::move(bankCcLanes)
        );
        seq::refreshContentView(state_->sequencer);
        state_->sequencer.invalidateVariationTelemetry();
        state_->sequencerHistory.recordPreparedPattern(std::move(change));
    }

    state_->publishPreparedSequencerMutation();
    state_->sequencerTrackActivations.publishPrepared(activation);
    setActivationResult(activation, target.trackIndex, result);
    OC_PERF_UNITS(perf, result.bytes, static_cast<uint32_t>(result.activation));
    return result;
}

FLASHMEM seq::SequencerTrackActivationStatus
SequencerPatternPresetDomainServices::activationStatus(
    uint8_t trackIndex,
    uint32_t generation
) const {
    using Status = seq::SequencerTrackActivationStatus;
    if (state_ == nullptr || generation == 0U ||
        trackIndex >= seq::SequencerTrackBankState::TRACK_COUNT) {
        return Status::IDLE;
    }
    const auto telemetry =
        state_->sequencerTrackActivations.telemetry(trackIndex);
    return telemetry.generation == generation ? telemetry.status : Status::IDLE;
}

FLASHMEM SequencerPatternPresetActionResult
SequencerPatternPresetDomainServices::renamePreset(
    const char* presetId,
    const char* expectedSemanticName,
    const char* newSemanticName
) const {
    SequencerPatternPresetActionResult result{};
    copyText(result.presetId, sizeof(result.presetId), presetId);
    if (core::persistence::PatternPresetFactoryLibrary::contains(presetId)) {
        result.status = SequencerPatternPresetDomainStatus::READ_ONLY;
        return result;
    }
    if (files_ == nullptr || catalog_ == nullptr) {
        result.status = SequencerPatternPresetDomainStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }
    if (!seq::validSequencerPresetTechnicalId(presetId) ||
        !seq::validSequencerPresetSemanticName(expectedSemanticName) ||
        !seq::validSequencerPresetSemanticName(newSemanticName)) {
        result.status = SequencerPatternPresetDomainStatus::FAILED;
        result.codecStatus = seq::SequencerPatternPresetStatus::INVALID_ARGUMENT;
        return result;
    }

    auto loaded = loadPreset(*files_, *catalog_, presetId);
    result.status = loaded.status;
    result.codecStatus = loaded.codecStatus;
    result.fileError = loaded.fileError;
    result.bytes = loaded.bytes;
    if (!loaded.ok()) return result;
    if (std::strcmp(
            loaded.metadata.semanticName,
            expectedSemanticName
        ) != 0) {
        result.status = SequencerPatternPresetDomainStatus::STALE_TARGET;
        return result;
    }

    copyText(
        loaded.metadata.semanticName,
        sizeof(loaded.metadata.semanticName),
        newSemanticName
    );
    const auto encoded = codec::encode(
        loaded.metadata,
        loaded.staged->pattern,
        loaded.drum.get(),
        loaded.buffer->bytes,
        codec::MAX_ENCODED_SIZE
    );
    result.codecStatus = encoded.status;
    if (!encoded.ok()) {
        result.status = statusFromCodec(encoded.status);
        return result;
    }
    core::persistence::PatternPresetFileStore store(*files_, *catalog_);
    const auto saved = store.save(
        presetId,
        loaded.buffer->bytes,
        encoded.bytesWritten
    );
    if (!saved) {
        result.status = statusFromFileError(saved.error().code);
        result.fileError = saved.error().code;
        return result;
    }
    result.bytes = encoded.bytesWritten;
    result.activation = SequencerPatternPresetActivation::APPLIED;
    return result;
}

FLASHMEM SequencerPatternPresetActionResult
SequencerPatternPresetDomainServices::deletePreset(
    const char* presetId,
    const char* expectedSemanticName
) const {
    SequencerPatternPresetActionResult result{};
    copyText(result.presetId, sizeof(result.presetId), presetId);
    if (core::persistence::PatternPresetFactoryLibrary::contains(presetId)) {
        result.status = SequencerPatternPresetDomainStatus::READ_ONLY;
        return result;
    }
    if (files_ == nullptr || catalog_ == nullptr) {
        result.status = SequencerPatternPresetDomainStatus::STORAGE_UNAVAILABLE;
        result.fileError = oc::type::ErrorCode::INVALID_STATE;
        return result;
    }
    auto loaded = loadPreset(*files_, *catalog_, presetId);
    result.status = loaded.status;
    result.codecStatus = loaded.codecStatus;
    result.fileError = loaded.fileError;
    result.bytes = loaded.bytes;
    if (!loaded.ok()) return result;
    if (std::strcmp(
            loaded.metadata.semanticName,
            expectedSemanticName
        ) != 0) {
        result.status = SequencerPatternPresetDomainStatus::STALE_TARGET;
        return result;
    }
    core::persistence::PatternPresetFileStore store(*files_, *catalog_);
    const auto removed = store.remove(presetId);
    if (!removed) {
        result.status = statusFromFileError(removed.error().code);
        result.fileError = removed.error().code;
        return result;
    }
    result.activation = SequencerPatternPresetActivation::APPLIED;
    return result;
}

}  // namespace core::handler
