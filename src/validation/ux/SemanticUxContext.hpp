#pragma once

#include <cstdint>

#include <oc/core/input/InputBindingTrace.hpp>

namespace core::validation::ux {

struct SemanticUxContext {
    const char* mode = nullptr;
    const char* effect = nullptr;
    const char* outcome = nullptr;
    const char* reason = nullptr;
    const char* target = nullptr;
    int16_t targetIndex = -1;
    int16_t targetStep = -1;
    int16_t targetCount = -1;
    int16_t targetPage = -1;
    int32_t targetMask = -1;
    int32_t sourceMask = -1;
    int32_t createMask = -1;
    int32_t overwriteMask = -1;
    const char* routePolicy = nullptr;
    const char* projection = nullptr;
    const char* source = nullptr;
    const char* winner = nullptr;
    const char* winnerSource = nullptr;
    const char* activationOrigin = nullptr;
    bool hasActivationGeneration = false;
    uint32_t activationGeneration = 0;
    int16_t mappingIndex = -1;
    int16_t mappingCount = -1;
    int16_t sourceTrack = -1;
    int16_t targetTrack = -1;
    const char* targetKind = nullptr;
    int16_t inheritedLaneCount = -1;
    int16_t pinnedLaneCount = -1;
    const char* operationOrigin = nullptr;
    bool hasOperationGeneration = false;
    uint32_t operationGeneration = 0;
    const char* operationStatus = nullptr;
    bool hasTargetRoute = false;
    uint8_t targetRoute = 0;
    bool targetRouteValid = false;
    const char* property = nullptr;
    char valueLabel[16] = {};
    bool hasConflict = false;
    bool conflict = false;
    bool hasAuthoredValue = false;
    uint8_t authoredValue = 0;
    bool hasResolvedValue = false;
    uint8_t resolvedValue = 0;
    int16_t controller = -1;
    int16_t defaultController = -1;
    bool hasStepOn = false;
    bool stepOn = false;
    bool hasResolvedStep = false;
    uint8_t resolvedNote = 0;
    uint8_t resolvedVelocity = 0;
    uint16_t resolvedGate = 0;
    int8_t resolvedNudge = 0;
    uint8_t resolvedProbability = 0;
    bool resolvedVariationVisible = false;

    // Transactional Step-content authoring. These fields intentionally expose
    // only the end-user contract: what is being authored, whether it is still
    // a dirty draft, which decision/action is offered, and whether publication
    // succeeded. The retained graph/scratch representation stays private.
    const char* draftKind = nullptr;
    bool hasDraftActive = false;
    bool draftActive = false;
    bool hasPublished = false;
    bool published = false;
    bool hasDraftDirty = false;
    bool draftDirty = false;
    const char* exitChoice = nullptr;
    const char* draftFailure = nullptr;
    const char* action = nullptr;
};

class SemanticUxContextProvider {
public:
    virtual ~SemanticUxContextProvider() = default;
    virtual void captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        SemanticUxContext& out
    ) const = 0;
};

void setCurrentSemanticUxContextProvider(SemanticUxContextProvider* provider);
void clearCurrentSemanticUxContextProvider(SemanticUxContextProvider* provider);
SemanticUxContextProvider* currentSemanticUxContextProvider();

}  // namespace core::validation::ux
