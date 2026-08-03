#pragma once

#include <cstdint>

namespace core::state::project {

/** Runtime identity for one published Project session. Never persisted. */
struct ProjectSessionIdentity {
    uint32_t bootGeneration = 1U;
    uint32_t sessionEpoch = 1U;

    constexpr bool operator==(const ProjectSessionIdentity& other) const {
        return bootGeneration == other.bootGeneration &&
               sessionEpoch == other.sessionEpoch;
    }

    constexpr bool operator!=(const ProjectSessionIdentity& other) const {
        return !(*this == other);
    }
};

/** Exact identity of one recovery-save request. */
struct ProjectSaveToken {
    ProjectSessionIdentity session{};
    uint32_t mutationEpoch = 0U;
    uint32_t requestId = 0U;
    uint32_t modifiedCounter = 0U;

    constexpr bool operator==(const ProjectSaveToken& other) const {
        return session == other.session &&
               mutationEpoch == other.mutationEpoch &&
               requestId == other.requestId &&
               modifiedCounter == other.modifiedCounter;
    }

    constexpr bool operator!=(const ProjectSaveToken& other) const {
        return !(*this == other);
    }
};

/** Token plus every authored revision consumed by incremental capture. */
struct ProjectCaptureGuard {
    ProjectSaveToken token{};
    uint32_t authoredRevision = 0U;
    uint32_t projectTrackRevision = 0U;

    constexpr bool operator==(const ProjectCaptureGuard& other) const {
        return token == other.token &&
               authoredRevision == other.authoredRevision &&
               projectTrackRevision == other.projectTrackRevision;
    }

    constexpr bool operator!=(const ProjectCaptureGuard& other) const {
        return !(*this == other);
    }
};

/** Allocation-free lifecycle control retained by CoreState in ordinary RAM. */
struct ProjectSessionControlState {
    ProjectSessionIdentity session{};
    uint32_t mutationEpoch = 0U;
    uint32_t requestId = 0U;
    uint32_t requestTimestampMs = 0U;
    bool trackingEnabled = false;
    bool savePending = false;
};

static_assert(sizeof(ProjectSessionIdentity) == 8U,
              "Project session identity layout changed");
static_assert(sizeof(ProjectSaveToken) == 20U,
              "Project save token layout changed");
static_assert(sizeof(ProjectCaptureGuard) == 28U,
              "Project capture guard layout changed");
static_assert(sizeof(ProjectSessionControlState) == 24U,
              "Project session control layout changed");

}  // namespace core::state::project

