#pragma once

#include <cstddef>
#include <cstdint>

#include "validation/ux/SemanticUxContext.hpp"

namespace core::validation::ux {

class SemanticUxSurface {
public:
    virtual ~SemanticUxSurface() = default;

    virtual bool captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        SemanticUxContext& out
    ) const = 0;
};

class SemanticUxSurfaceRegistry final : public SemanticUxContextProvider {
public:
    // The standalone application currently owns 19 semantic surfaces. Keep
    // headroom here so adding a surface cannot silently evict global controls.
    static constexpr std::size_t CAPACITY = 24;

    bool add(const SemanticUxSurface& surface, uint8_t priority = 128);
    void clear();
    std::size_t count() const;

    void captureSemanticUxContext(
        const oc::core::input::InputBindingTraceEvent& event,
        SemanticUxContext& out
    ) const override;

private:
    struct Entry {
        const SemanticUxSurface* surface = nullptr;
        uint8_t priority = 128;
    };

    Entry entries_[CAPACITY] = {};
    std::size_t count_ = 0;
};

}  // namespace core::validation::ux
