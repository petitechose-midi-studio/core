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
    int32_t targetMask = -1;
    const char* property = nullptr;
    char valueLabel[16] = {};
    bool hasStepOn = false;
    bool stepOn = false;
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
