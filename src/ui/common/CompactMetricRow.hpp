#pragma once

#include <array>
#include <cstddef>
#include <lvgl.h>

namespace core::ui {

struct CompactMetricProps {
    const char* icon = "";
    const char* value = "";
};

/**
 * Compact icon/value metrics shared by retained controller headers.
 *
 * The row owns presentation only. Callers keep the semantic meaning and the
 * storage of the strings they expose through CompactMetricProps.
 */
class CompactMetricRow {
public:
    static constexpr size_t METRIC_COUNT = 2;

    bool create(lv_obj_t* parent);

    bool render(const std::array<CompactMetricProps, METRIC_COUNT>& props);

    [[nodiscard]] lv_obj_t* element() const { return container_; }

private:
    struct Widgets {
        lv_obj_t* group = nullptr;
        lv_obj_t* icon = nullptr;
        lv_obj_t* value = nullptr;
    };

    struct Cache {
        std::array<char, 8> icon{};
        std::array<char, 8> value{};
        bool visible = false;
    };

    lv_obj_t* container_ = nullptr;
    std::array<Widgets, METRIC_COUNT> widgets_{};
    std::array<Cache, METRIC_COUNT> cache_{};
    bool visible_ = false;
};

}  // namespace core::ui
