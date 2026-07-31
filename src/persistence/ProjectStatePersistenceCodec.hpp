#pragma once

#include <cstdint>

#include "persistence/ProjectFileContainer.hpp"
#include "persistence/ProjectStatePersistencePayloads.hpp"

namespace core::persistence::project_state_codec {

bool fillMetaPayload(const core::state::project::ProjectMetadata& source,
                     ProjectMetaPayload& out);
bool encodeMetaPayload(const ProjectMetaPayload& payload,
                       uint8_t* out,
                       uint32_t outCapacity);
bool decodeMetaPayload(const uint8_t* data,
                       uint32_t size,
                       ProjectMetaPayload& out);
void applyMetaPayload(const ProjectMetaPayload& payload,
                      core::state::project::ProjectMetadata& target);

bool fillTransportPayload(const core::state::project::ProjectTransportState& source,
                          ProjectTransportPayload& out);
bool encodeTransportPayload(const ProjectTransportPayload& payload,
                            uint8_t* out,
                            uint32_t outCapacity);
bool decodeTransportPayload(const uint8_t* data,
                            uint32_t size,
                            ProjectTransportPayload& out);
void applyTransportPayload(const ProjectTransportPayload& payload,
                           core::state::project::ProjectTransportState& target);

bool fillMusicalContextPayload(const core::state::project::ProjectMusicalContext& source,
                               ProjectMusicalContextPayload& out);
bool encodeMusicalContextPayload(const ProjectMusicalContextPayload& payload,
                                 uint8_t* out,
                                 uint32_t outCapacity);
bool decodeMusicalContextPayload(const uint8_t* data,
                                 uint32_t size,
                                 ProjectMusicalContextPayload& out);
void applyMusicalContextPayload(const ProjectMusicalContextPayload& payload,
                                core::state::project::ProjectMusicalContext& target);

bool fillEditingPayload(const core::state::project::ProjectEditingState& source,
                        ProjectEditingPayload& out);
bool encodeEditingPayload(const ProjectEditingPayload& payload,
                          uint8_t* out,
                          uint32_t outCapacity);
bool decodeEditingPayload(const uint8_t* data,
                          uint32_t size,
                          ProjectEditingPayload& out);
void applyEditingPayload(const ProjectEditingPayload& payload,
                         core::state::project::ProjectEditingState& target);

bool applyProjectStateChunks(
    const core::persistence::project_file::DecodedChunkView* chunks,
    uint16_t chunkCount,
    core::state::project::ProjectState& target,
    core::persistence::project_file::LoadReport* report = nullptr
);

}  // namespace core::persistence::project_state_codec
