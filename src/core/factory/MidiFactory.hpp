#pragma once

#include <etl/vector.h>

#include "config/MidiMapping.hpp"
#include "config/System.hpp"
#include "core/struct/MidiCCMapping.hpp"

/**
 * @brief Factory to create MIDI mappings from config data
 *
 * Converts compile-time constexpr arrays from config/ into runtime containers.
 * This is the only place where MIDI mapping config is loaded.
 */
class MidiFactory {
public:
    /**
     * @brief Load MIDI mappings from Config::MIDI_MAPPINGS
     */
    static etl::vector<MidiCCMapping, System::Memory::MAX_MIDI_MAPPINGS> createDefault() {
        etl::vector<MidiCCMapping, System::Memory::MAX_MIDI_MAPPINGS> mappings;

        for (const auto& mapping : Config::MIDI_MAPPINGS) {
            mappings.push_back(mapping);
        }

        return mappings;
    }
};
