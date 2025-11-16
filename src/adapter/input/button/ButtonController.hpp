#pragma once

#include <etl/flat_map.h>
#include <etl/vector.h>

#include "UnifiedButton.hpp"
#include "config/System.hpp"
#include "core/Type.hpp"
#include "core/struct/Button.hpp"

class IEventBus;
class Multiplexer;

class ButtonController {
public:
    explicit ButtonController(
        const etl::vector<Hardware::Button, System::Hardware::BUTTONS_COUNT>& buttonSetups,
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
    etl::vector<std::unique_ptr<UnifiedButton>, System::Hardware::BUTTONS_COUNT> ownedButtons_;

    etl::vector<bool, System::Hardware::BUTTONS_COUNT> lastStates_;
    etl::vector<uint32_t, System::Hardware::BUTTONS_COUNT> lastChangeTime_;  // Software debounce

    etl::flat_map<ButtonID, size_t, System::Hardware::BUTTONS_COUNT> idToIndex_;

    IEventBus& eventBus_;
};