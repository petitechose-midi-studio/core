#include "validation/ux/SemanticUxSurface.hpp"

namespace core::validation::ux {

bool SemanticUxSurfaceRegistry::add(const SemanticUxSurface& surface, uint8_t priority) {
    if (count_ >= CAPACITY) {
        return false;
    }

    std::size_t insertAt = count_;
    while (insertAt > 0 && entries_[insertAt - 1].priority > priority) {
        entries_[insertAt] = entries_[insertAt - 1];
        --insertAt;
    }

    entries_[insertAt] = Entry{&surface, priority};
    ++count_;
    return true;
}

void SemanticUxSurfaceRegistry::clear() {
    for (std::size_t i = 0; i < count_; ++i) {
        entries_[i] = Entry{};
    }
    count_ = 0;
}

std::size_t SemanticUxSurfaceRegistry::count() const {
    return count_;
}

void SemanticUxSurfaceRegistry::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    SemanticUxContext& out
) const {
    for (std::size_t i = 0; i < count_; ++i) {
        if (entries_[i].surface && entries_[i].surface->captureSemanticUxContext(event, out)) {
            return;
        }
    }
}

}  // namespace core::validation::ux
