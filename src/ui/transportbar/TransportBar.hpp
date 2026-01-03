#pragma once

#include <functional>
#include <memory>

#include <lvgl.h>
#include <oc/ui/lvgl/widget/StateIndicator.hpp>

namespace ui {

/**
 * @brief Props for TransportBar component
 *
 * Pulse signals (noteInActive, etc.) trigger visual blink when transitioning
 * false→true. Callbacks are invoked when pulse timer completes, allowing
 * the orchestrator to reset state signals.
 */
struct TransportBarProps {
    // MIDI activity indicators (pulse on true)
    bool noteInActive = false;
    bool noteOutActive = false;
    bool ccInActive = false;
    bool ccOutActive = false;

    // Transport state
    bool playing = false;
    float tempo = 120.0f;

    // Beat indicator (pulse on true)
    bool beatPulse = false;

    // Pulse completion callbacks (called when visual timer expires)
    std::function<void()> onNoteInPulseComplete;
    std::function<void()> onNoteOutPulseComplete;
    std::function<void()> onCcInPulseComplete;
    std::function<void()> onCcOutPulseComplete;
    std::function<void()> onBeatPulseComplete;
};

/**
 * @brief Transport bar at bottom of screen
 *
 * Layout (3 columns):
 * - Cell 1 (Left): MIDI indicators (Note IN/OUT, CC IN/OUT)
 * - Cell 2 (Center): Play icon
 * - Cell 3 (Right): Beat indicator + Tempo
 *
 * Stateless component following Props pattern.
 * Rendered by orchestrator (StandaloneContext) when state changes.
 */
class TransportBar {
public:
    explicit TransportBar(lv_obj_t* parent);
    ~TransportBar();

    TransportBar(const TransportBar&) = delete;
    TransportBar& operator=(const TransportBar&) = delete;

    /**
     * @brief Render with given props
     * @param props Display properties and callbacks
     */
    void render(const TransportBarProps& props);

private:
    using StateIndicator = oc::ui::lvgl::StateIndicator;

    lv_obj_t* container_ = nullptr;

    // Cell 1: MIDI indicators (Note + CC)
    lv_obj_t* noteInIcon_ = nullptr;
    lv_obj_t* noteOutIcon_ = nullptr;
    lv_obj_t* ccInIcon_ = nullptr;
    lv_obj_t* ccOutIcon_ = nullptr;

    // Cell 2: Transport
    lv_obj_t* playIcon_ = nullptr;

    // Cell 3: Tempo
    lv_obj_t* tempoLabel_ = nullptr;

    // Cell 4: Beat indicator
    std::unique_ptr<StateIndicator> beatIndicator_;

    // Pulse timers (for auto-reset after blink)
    lv_timer_t* noteInTimer_ = nullptr;
    lv_timer_t* noteOutTimer_ = nullptr;
    lv_timer_t* ccInTimer_ = nullptr;
    lv_timer_t* ccOutTimer_ = nullptr;
    lv_timer_t* beatTimer_ = nullptr;

    // Cached props for change detection
    TransportBarProps currentProps_;

    void createLayout(lv_obj_t* parent);
    void createMidiIndicators(lv_obj_t* parent);
    void createTransportCenter(lv_obj_t* parent);
    void createTempoWithBeat(lv_obj_t* parent);

    void setPlaying(bool playing);
    void setTempo(float bpm);

    // Pulse helper for icon-based indicators
    void pulseIcon(lv_obj_t* icon, lv_timer_t*& timer, lv_color_t activeColor,
                   uint32_t duration, lv_timer_cb_t callback);

    static void onNoteInTimeout(lv_timer_t* timer);
    static void onNoteOutTimeout(lv_timer_t* timer);
    static void onCcInTimeout(lv_timer_t* timer);
    static void onCcOutTimeout(lv_timer_t* timer);
    static void onBeatTimeout(lv_timer_t* timer);
};

}  // namespace ui
