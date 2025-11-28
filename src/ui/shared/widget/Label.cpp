#include "Label.hpp"

Label::Label(lv_obj_t *parent)
{
    createWidgets(parent);
}

Label::~Label()
{
    stopScrollAnimation();
    destroyWidgets();
}

Label::Label(Label &&other) noexcept
    : container_(other.container_)
    , label_(other.label_)
    , auto_scroll_enabled_(other.auto_scroll_enabled_)
    , anim_running_(other.anim_running_)
    , overflow_amount_(other.overflow_amount_)
    , alignment_(other.alignment_)
    , scroll_duration_ms_(other.scroll_duration_ms_)
    , pause_duration_ms_(other.pause_duration_ms_)
{
    other.container_ = nullptr;
    other.label_ = nullptr;
    other.anim_running_ = false;
}

Label &Label::operator=(Label &&other) noexcept
{
    if (this != &other)
    {
        stopScrollAnimation();
        destroyWidgets();

        container_ = other.container_;
        label_ = other.label_;
        auto_scroll_enabled_ = other.auto_scroll_enabled_;
        anim_running_ = other.anim_running_;
        overflow_amount_ = other.overflow_amount_;
        alignment_ = other.alignment_;
        scroll_duration_ms_ = other.scroll_duration_ms_;
        pause_duration_ms_ = other.pause_duration_ms_;

        other.container_ = nullptr;
        other.label_ = nullptr;
        other.anim_running_ = false;
    }
    return *this;
}

void Label::createWidgets(lv_obj_t *parent)
{
    // Container that clips overflow - uses flex-grow to fill available space
    container_ = lv_obj_create(parent);
    lv_obj_set_height(container_, LV_SIZE_CONTENT);
    lv_obj_set_width(container_, 0);
    lv_obj_set_flex_grow(container_, 1);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(container_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    // The actual label
    label_ = lv_label_create(container_);
    lv_label_set_text(label_, "");
    lv_obj_set_style_pad_all(label_, 0, 0);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_CLIP);
}

void Label::destroyWidgets()
{
    if (container_)
    {
        lv_obj_delete(container_);
        container_ = nullptr;
        label_ = nullptr;
    }
}

void Label::setText(const std::string &text)
{
    setText(text.c_str());
}

void Label::setText(const char *text)
{
    if (!label_)
        return;

    stopScrollAnimation();
    lv_obj_set_pos(label_, 0, 0);
    lv_label_set_text(label_, text);

    // Defer overflow check to next frame when layout is ready
    if (auto_scroll_enabled_)
    {
        lv_timer_t *timer = lv_timer_create(
            [](lv_timer_t *t)
            {
                auto *self = static_cast<Label *>(lv_timer_get_user_data(t));
                if (self)
                {
                    self->checkOverflowAndScroll();
                }
            },
            50, this);
        lv_timer_set_repeat_count(timer, 1);
    }
}

void Label::setColor(lv_color_t color)
{
    if (label_)
    {
        lv_obj_set_style_text_color(label_, color, 0);
    }
}

void Label::setFont(const lv_font_t *font)
{
    if (label_)
    {
        lv_obj_set_style_text_font(label_, font, 0);
    }
}

void Label::setFlexGrow(bool enabled)
{
    if (container_)
    {
        if (enabled)
        {
            lv_obj_set_width(container_, 0);
            lv_obj_set_flex_grow(container_, 1);
        }
        else
        {
            lv_obj_set_flex_grow(container_, 0);
            lv_obj_set_width(container_, LV_SIZE_CONTENT);
        }
    }
}

void Label::checkOverflowAndScroll()
{
    if (!label_ || !container_)
        return;

    // Measure text width
    lv_label_set_long_mode(label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_, LV_SIZE_CONTENT);
    lv_obj_update_layout(label_);
    lv_coord_t text_width = lv_obj_get_width(label_);

    // Restore clip mode
    lv_label_set_long_mode(label_, LV_LABEL_LONG_CLIP);

    lv_coord_t container_width = lv_obj_get_width(container_);
    overflow_amount_ = text_width - container_width;

    if (overflow_amount_ > 0)
    {
        // Text overflows: align left and scroll
        lv_obj_set_x(label_, 0);
        if (auto_scroll_enabled_)
        {
            startScrollAnimation();
        }
    }
    else
    {
        // Text fits: apply alignment
        lv_coord_t offset = 0;
        switch (alignment_)
        {
        case LV_TEXT_ALIGN_CENTER:
            offset = (container_width - text_width) / 2;
            break;
        case LV_TEXT_ALIGN_RIGHT:
            offset = container_width - text_width;
            break;
        default:
            offset = 0;
            break;
        }
        lv_obj_set_x(label_, offset);
    }
}

void Label::startScrollAnimation()
{
    if (!label_ || anim_running_ || overflow_amount_ <= 0)
        return;

    lv_anim_init(&scroll_anim_);
    lv_anim_set_var(&scroll_anim_, this);
    lv_anim_set_exec_cb(&scroll_anim_, scrollAnimCallback);
    lv_anim_set_values(&scroll_anim_, 0, -overflow_amount_);
    lv_anim_set_duration(&scroll_anim_, scroll_duration_ms_);
    lv_anim_set_delay(&scroll_anim_, 500);
    lv_anim_set_path_cb(&scroll_anim_, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&scroll_anim_, [](lv_anim_t *a)
                             {
        auto* self = static_cast<Label*>(a->var);
        lv_timer_t* timer = lv_timer_create(pauseTimerCallback, self->pause_duration_ms_, self);
        lv_timer_set_repeat_count(timer, 1); });

    lv_anim_start(&scroll_anim_);
    anim_running_ = true;
}

void Label::stopScrollAnimation()
{
    if (anim_running_)
    {
        lv_anim_delete(this, nullptr);
        anim_running_ = false;
    }
}

void Label::scrollAnimCallback(void *var, int32_t value)
{
    auto *self = static_cast<Label *>(var);
    if (self->label_)
    {
        lv_obj_set_x(self->label_, value);
    }
}

void Label::scrollBackAnimCallback(void *var, int32_t value)
{
    auto *self = static_cast<Label *>(var);
    if (self->label_)
    {
        lv_obj_set_x(self->label_, value);
    }
}

void Label::pauseTimerCallback(lv_timer_t *timer)
{
    auto *self = static_cast<Label *>(lv_timer_get_user_data(timer));
    if (!self || !self->label_)
        return;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, self);
    lv_anim_set_exec_cb(&anim, scrollBackAnimCallback);
    lv_anim_set_values(&anim, -self->overflow_amount_, 0);
    lv_anim_set_duration(&anim, self->scroll_duration_ms_);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&anim, [](lv_anim_t *a)
                             {
        auto* self = static_cast<Label*>(a->var);
        self->anim_running_ = false; });

    lv_anim_start(&anim);
}
