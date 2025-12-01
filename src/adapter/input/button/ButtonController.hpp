#pragma once

#include <map>
#include <memory>
#include <vector>

#include "UnifiedButton.hpp"
#include "config/System.hpp"
#include "core/Type.hpp"
#include "core/struct/Button.hpp"

class IEventBus;
class Multiplexer;

class ButtonController {
public:
    explicit ButtonController(
        const std::vector<Hardware::Button>& buttonSetups,
        Multiplexer& mux, IEventBus& eventBus);
    ~ButtonController();

    ButtonController(const ButtonController&) = delete;
    ButtonController& operator=(const ButtonController&) = delete;
    ButtonController(ButtonController&&) = default;
    ButtonController& operator=(ButtonController&&) = default;

    void updateAll();

    UnifiedButton* getButton(ButtonID id);
    const UnifiedButton* getButton(ButtonID id) const;

private:
    std::vector<std::unique_ptr<UnifiedButton>> owned_buttons_;
    std::vector<bool> last_states_;
    std::vector<uint32_t> last_change_time_;
    std::map<ButtonID, size_t> id_to_index_;

    IEventBus& event_bus_;
};