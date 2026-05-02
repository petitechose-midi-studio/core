#include "validation/ux/SemanticUxContext.hpp"

#include <config/PlatformCompat.hpp>

namespace core::validation::ux {
namespace {
SemanticUxContextProvider* currentProvider = nullptr;
}

FLASHMEM void setCurrentSemanticUxContextProvider(SemanticUxContextProvider* provider) {
    currentProvider = provider;
}

FLASHMEM void clearCurrentSemanticUxContextProvider(SemanticUxContextProvider* provider) {
    if (currentProvider == provider) {
        currentProvider = nullptr;
    }
}

FLASHMEM SemanticUxContextProvider* currentSemanticUxContextProvider() {
    return currentProvider;
}

}  // namespace core::validation::ux
