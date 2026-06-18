#pragma once

#include <array>
#include <cstddef>

#include <oc/type/Ids.hpp>

namespace core::handler {

/**
 * Tracks button releases that must be ignored after a long-press path handled
 * the gesture while the physical button was still held.
 */
template <std::size_t Capacity>
class ButtonReleaseLatch {
public:
    static_assert(Capacity > 0, "ButtonReleaseLatch capacity must be positive");

    template <typename ButtonIdT>
    bool arm(ButtonIdT button) {
        return armId(static_cast<oc::type::ButtonID>(button));
    }

    template <typename ButtonIdT>
    bool consume(ButtonIdT button) {
        return consumeId(static_cast<oc::type::ButtonID>(button));
    }

    void clear() {
        armed_.fill(false);
    }

private:
    bool armId(oc::type::ButtonID button) {
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (armed_[i] && buttons_[i] == button) {
                return true;
            }
        }

        for (std::size_t i = 0; i < Capacity; ++i) {
            if (!armed_[i]) {
                buttons_[i] = button;
                armed_[i] = true;
                return true;
            }
        }

        return false;
    }

    bool consumeId(oc::type::ButtonID button) {
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (armed_[i] && buttons_[i] == button) {
                armed_[i] = false;
                return true;
            }
        }
        return false;
    }

    std::array<oc::type::ButtonID, Capacity> buttons_{};
    std::array<bool, Capacity> armed_{};
};

}  // namespace core::handler
