#pragma once

#include <cstdint>

#include "persistence/ProjectFileContainer.hpp"
#include "persistence/ProjectStatePersistencePayloads.hpp"

namespace core::persistence::project_state_codec {

struct DecodeResult {
    bool ok = false;
    core::persistence::project_file::Status containerStatus =
        core::persistence::project_file::Status::OK;
    core::persistence::project_file::LoadStatus loadStatus =
        core::persistence::project_file::LoadStatus::OK;
    bool overwriteSafe = true;
};

void fillMetaPayload(const core::state::project::ProjectMetadata& source,
                     ProjectMetaPayload& out);
void applyMetaPayload(const ProjectMetaPayload& payload,
                      core::state::project::ProjectMetadata& target);

void fillTransportPayload(const core::state::project::ProjectTransportState& source,
                          ProjectTransportPayload& out);
void applyTransportPayload(const ProjectTransportPayload& payload,
                           core::state::project::ProjectTransportState& target);

void fillMusicalContextPayload(const core::state::project::ProjectMusicalContext& source,
                               ProjectMusicalContextPayload& out);
void applyMusicalContextPayload(const ProjectMusicalContextPayload& payload,
                                core::state::project::ProjectMusicalContext& target);

void fillRoutingPayload(const core::state::project::ProjectRoutingState& source,
                        ProjectRoutingPayload& out);
void applyRoutingPayload(const ProjectRoutingPayload& payload,
                         core::state::project::ProjectRoutingState& target);

core::persistence::project_file::EncodeResult encodeProjectState(
    const core::state::project::ProjectState& state,
    uint8_t* out,
    uint32_t outCapacity
);

void applyProjectStateChunks(
    const core::persistence::project_file::DecodedChunkView* chunks,
    uint16_t chunkCount,
    core::state::project::ProjectState& target,
    core::persistence::project_file::LoadReport* report = nullptr
);

DecodeResult decodeProjectState(const uint8_t* data,
                                uint32_t size,
                                core::state::project::ProjectState& out,
                                core::persistence::project_file::LoadReport* report = nullptr);

}  // namespace core::persistence::project_state_codec
