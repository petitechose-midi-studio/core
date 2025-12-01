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
    std::vector<std::unique_ptr<UnifiedButton>> ownedButtons_;
    std::vector<bool> lastStates_;
    std::vector<uint32_t> lastChangeTime_;
    std::map<ButtonID, size_t> idToIndex_;

    IEventBus& eventBus_;
};