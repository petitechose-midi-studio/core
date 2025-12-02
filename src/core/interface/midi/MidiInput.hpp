#pragma once

/**
 * @brief Interface for MIDI input ports
 *
 * This interface defines the contract for receiving MIDI messages.
 * Implementations should handle the actual MIDI reception and notify
 * registered callbacks when messages are received.
 */
class IMidiInput {
protected:
    ~IMidiInput() = default;
public:
    /**
     * @brief Process any pending MIDI messages
     * Should be called regularly to process incoming MIDI data
     */
    virtual void processPendingMessages() = 0;
};