#pragma once

#include <vector>

#include "config/MidiMapping.hpp"
#include "struct/MidiCCMapping.hpp"

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
    static std::vector<MidiCCMapping> createDefault() {
        std::vector<MidiCCMapping> mappings;
        mappings.reserve(std::size(Config::MIDI_MAPPINGS));

        for (const auto& mapping : Config::MIDI_MAPPINGS) { mappings.push_back(mapping); }

        return mappings;
    }
};
