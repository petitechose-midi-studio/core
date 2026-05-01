#include "validation/ux/SemanticUxContext.hpp"

namespace core::validation::ux {
namespace {
SemanticUxContextProvider* currentProvider = nullptr;
}

void setCurrentSemanticUxContextProvider(SemanticUxContextProvider* provider) {
    currentProvider = provider;
}

void clearCurrentSemanticUxContextProvider(SemanticUxContextProvider* provider) {
    if (currentProvider == provider) {
        currentProvider = nullptr;
    }
}

SemanticUxContextProvider* currentSemanticUxContextProvider() {
    return currentProvider;
}

}  // namespace core::validation::ux
