#pragma once
#include <etl/flat_map.h>
#include <etl/vector.h>

#include <memory>

#include "Encoder.hpp"
#include "config/System.hpp"
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
    explicit EncoderController(
        const etl::vector<Hardware::Encoder, System::Hardware::ENCODERS_COUNT>& encoderSetups,
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
    etl::vector<Encoder, System::Hardware::ENCODERS_COUNT> encoders_;

    etl::flat_map<EncoderID, size_t, System::Hardware::ENCODERS_COUNT> idToIndex_;
};