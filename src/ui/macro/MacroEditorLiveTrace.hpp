#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "ui/macro/MacroEditorPreviewModel.hpp"

namespace core::ui {

inline constexpr uint16_t MACRO_EDITOR_LIVE_TRACE_CAPACITY = 320U;
inline constexpr uint32_t MACRO_EDITOR_LIVE_TRACE_WINDOW_MS = 2000U;

struct MacroEditorLiveTraceEntry {
    uint32_t timestampMs = 0U;
    uint16_t baseQ16 = 0U;
    int16_t modulationQ15 = 0;
    uint16_t outQ16 = 0U;
    uint8_t flags = 0U;
    uint8_t reserved = 0U;
};

class MacroEditorLiveTrace {
public:
    struct Cursor {
        uint16_t logicalIndex = 0U;
    };

    void clear() {
        head_ = 0U;
        count_ = 0U;
        contextKey_ = 0U;
        hasContext_ = false;
        ++revision_;
        if (revision_ == 0U) revision_ = 1U;
    }

    void append(
        uint32_t contextKey,
        uint32_t timestampMs,
        const MacroEditorLiveValue& value
    ) {
        if (!value.valid) return;
        if (!hasContext_ || contextKey_ != contextKey) {
            clear();
            contextKey_ = contextKey;
            hasContext_ = true;
        }
        const MacroEditorLiveTraceEntry next{
            .timestampMs = timestampMs,
            .baseQ16 = quantizeUnipolar(value.base),
            .modulationQ15 = quantizeBipolar(value.modulation),
            // Destination output is MIDI CC: retain its real 7-bit staircase.
            .outQ16 = quantizeMidiCc(value.out),
            .flags = static_cast<uint8_t>(
                (value.clippedLow ? 0x01U : 0U) |
                (value.clippedHigh ? 0x02U : 0U)
            ),
        };
        if (count_ > 0U) {
            auto& last = entries_[physicalIndex(count_ - 1U)];
            if (last.timestampMs == timestampMs) {
                if (last.baseQ16 != next.baseQ16 ||
                    last.modulationQ15 != next.modulationQ15 ||
                    last.outQ16 != next.outQ16 ||
                    last.flags != next.flags) {
                    last = next;
                    bumpRevision();
                }
                return;
            }
            if (static_cast<int32_t>(timestampMs - last.timestampMs) < 0) {
                clear();
                contextKey_ = contextKey;
                hasContext_ = true;
            }
        }
        if (count_ < entries_.size()) {
            entries_[physicalIndex(count_)] = next;
            ++count_;
        } else {
            entries_[head_] = next;
            head_ = static_cast<uint16_t>((head_ + 1U) % entries_.size());
        }
        bumpRevision();
    }

    [[nodiscard]] bool sample(
        uint16_t positionQ16,
        uint32_t nowMs,
        Cursor& cursor,
        MacroEditorPreviewSample& out
    ) const {
        if (count_ == 0U) return false;
        cursor.logicalIndex = std::min<uint16_t>(
            cursor.logicalIndex,
            static_cast<uint16_t>(count_ - 1U)
        );
        const uint32_t windowStart = nowMs > MACRO_EDITOR_LIVE_TRACE_WINDOW_MS
            ? nowMs - MACRO_EDITOR_LIVE_TRACE_WINDOW_MS
            : 0U;
        const uint32_t target = windowStart + static_cast<uint32_t>(
            (static_cast<uint64_t>(MACRO_EDITOR_LIVE_TRACE_WINDOW_MS) *
             positionQ16) / 65535U
        );
        while (cursor.logicalIndex + 1U < count_ &&
               entry(static_cast<uint16_t>(cursor.logicalIndex + 1U))
                       .timestampMs <= target) {
            ++cursor.logicalIndex;
        }
        const auto& left = entry(cursor.logicalIndex);
        const auto& right = cursor.logicalIndex + 1U < count_
            ? entry(static_cast<uint16_t>(cursor.logicalIndex + 1U))
            : left;
        uint16_t alphaQ16 = 0U;
        if (target > left.timestampMs && right.timestampMs > left.timestampMs) {
            alphaQ16 = static_cast<uint16_t>(std::min<uint64_t>(
                65535U,
                (static_cast<uint64_t>(target - left.timestampMs) * 65535U) /
                    (right.timestampMs - left.timestampMs)
            ));
        }
        out = {
            .automationQ16 = lerp(left.baseQ16, right.baseQ16, alphaQ16),
            .baseQ16 = lerp(left.baseQ16, right.baseQ16, alphaQ16),
            .modulationQ15 = lerpSigned(
                left.modulationQ15,
                right.modulationQ15,
                alphaQ16
            ),
            .outQ16 = lerp(left.outQ16, right.outQ16, alphaQ16),
            .clippedLow = ((left.flags | right.flags) & 0x01U) != 0U,
            .clippedHigh = ((left.flags | right.flags) & 0x02U) != 0U,
            .discontinuityBefore = false,
        };
        return true;
    }

    [[nodiscard]] uint16_t count() const { return count_; }
    [[nodiscard]] uint32_t revision() const { return revision_; }

private:
    static uint16_t quantizeUnipolar(float value) {
        return static_cast<uint16_t>(std::lround(
            std::clamp(value, 0.0f, 1.0f) * 65535.0f
        ));
    }

    static int16_t quantizeBipolar(float value) {
        return static_cast<int16_t>(std::lround(
            std::clamp(value, -1.0f, 1.0f) * 32767.0f
        ));
    }

    static uint16_t quantizeMidiCc(float value) {
        const uint32_t cc = static_cast<uint32_t>(std::lround(
            std::clamp(value, 0.0f, 1.0f) * 127.0f
        ));
        return static_cast<uint16_t>((cc * 65535U + 63U) / 127U);
    }

    static uint16_t lerp(uint16_t from, uint16_t to, uint16_t alphaQ16) {
        return static_cast<uint16_t>(static_cast<int32_t>(from) +
            (static_cast<int64_t>(
                static_cast<int32_t>(to) - static_cast<int32_t>(from)
            ) * alphaQ16 + 32767) / 65535);
    }

    static int16_t lerpSigned(
        int16_t from,
        int16_t to,
        uint16_t alphaQ16
    ) {
        const int64_t scaled = static_cast<int64_t>(
            static_cast<int32_t>(to) - static_cast<int32_t>(from)
        ) * alphaQ16;
        const int64_t rounded = scaled >= 0
            ? scaled + 32767
            : scaled - 32767;
        return static_cast<int16_t>(
            static_cast<int32_t>(from) + rounded / 65535
        );
    }

    [[nodiscard]] uint16_t physicalIndex(uint16_t logicalIndex) const {
        return static_cast<uint16_t>(
            (static_cast<uint32_t>(head_) + logicalIndex) % entries_.size()
        );
    }

    [[nodiscard]] const MacroEditorLiveTraceEntry& entry(
        uint16_t logicalIndex
    ) const {
        return entries_[physicalIndex(logicalIndex)];
    }

    void bumpRevision() {
        ++revision_;
        if (revision_ == 0U) revision_ = 1U;
    }

    std::array<
        MacroEditorLiveTraceEntry,
        MACRO_EDITOR_LIVE_TRACE_CAPACITY
    > entries_{};
    uint32_t contextKey_ = 0U;
    uint32_t revision_ = 1U;
    uint16_t head_ = 0U;
    uint16_t count_ = 0U;
    bool hasContext_ = false;
    uint8_t reserved_[3]{};
};

static_assert(sizeof(MacroEditorLiveTraceEntry) == 12U);
static_assert(
    sizeof(MacroEditorLiveTrace) <= 4096U,
    "The single rolling Macro trace must remain a bounded PSRAM surface"
);

}  // namespace core::ui
