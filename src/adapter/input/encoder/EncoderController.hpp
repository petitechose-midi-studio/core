#pragma once

#include "Encoder.hpp"

#include <map>
#include <memory>
#include <vector>

#include "core/Type.hpp"
#include "core/struct/Encoder.hpp"

class IEventBus;

/**
 * Controller for multiple encoders with lazy hardware initialization.
 *
 * IMPORTANT: Call init() after Arduino setup() before using encoders.
 */
class EncoderController {
public:
    explicit EncoderController(const std::vector<Hardware::Encoder>& encoderSetups,
                               IEventBus& eventBus);

    /**
     * Initialize all encoder hardware. Must be called after Arduino setup().
     * Safe to call multiple times.
     */
    void init();

    void flushAllEvents();

    void resetEncoderPosition(EncoderID encoderId, float normalizedValue);

    void setDiscreteSteps(EncoderID encoderId, uint16_t steps);
    void setContinuous(EncoderID encoderId);

    void setMode(EncoderID encoderId, Hardware::EncoderMode mode);
    void setBounds(EncoderID encoderId, float min, float max);
    void setDelta(EncoderID encoderId, float delta);

    Encoder* getEncoder(EncoderID id);
    const Encoder* getEncoder(EncoderID id) const;
private:
    std::vector<std::unique_ptr<Encoder>> encoders_;
    std::map<EncoderID, size_t> id_to_index_;
};