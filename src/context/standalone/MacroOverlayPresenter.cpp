#include "context/standalone/MacroOverlayPresenter.hpp"

#include <algorithm>
#include <array>
#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include <oc/type/TextFormat.hpp>

namespace core::context::standalone {

MacroOverlayPresenter::MacroOverlayPresenter(
    core::state::CoreState& state,
    ms::ui::VirtualListKeyValueOverlay& macroEditOverlay,
    ms::ui::VirtualListSelectorOverlay& macroEditSelectorOverlay,
    ms::ui::VirtualListSelectorOverlay& pageSelectorOverlay,
    ms::ui::VirtualListSelectorOverlay& macroTargetSelectorOverlay
)
    : state_(state)
    , macro_edit_overlay_(macroEditOverlay)
    , macro_edit_selector_overlay_(macroEditSelectorOverlay)
    , page_selector_overlay_(pageSelectorOverlay)
    , macro_target_selector_overlay_(macroTargetSelectorOverlay) {}

FLASHMEM void MacroOverlayPresenter::bind() {
    edit_watcher_.watchAll(
        [this]() { renderEdit(); },
        state_.macroEdit.visible,
        state_.macroEdit.editingIndex,
        state_.macroEdit.tempChannel,
        state_.macroEdit.tempCC,
        state_.macroEdit.focusedRow,
        state_.configRevision
    );

    edit_selector_watcher_.watchAll(
        [this]() { renderEditSelector(); },
        state_.macroEdit.selector.visible,
        state_.macroEdit.selector.editingRow,
        state_.macroEdit.selector.selectedIndex
    );

    page_selector_watcher_.watchAll(
        [this]() { renderPageSelector(); },
        state_.pages.selector.visible,
        state_.pages.selector.selectedIndex,
        state_.configRevision
    );

    macro_target_selector_watcher_.watchAll(
        [this]() { renderTargetSelector(); },
        state_.macroEdit.macroSelector.visible,
        state_.macroEdit.macroSelector.selectedIndex
    );
}

FLASHMEM void MacroOverlayPresenter::renderEdit() {
    const bool visible = state_.macroEdit.visible.get();
    if (!visible) {
        macro_edit_overlay_.render({.visible = false});
        return;
    }

    if (state_.macroEdit.pendingOpenReleaseDecision && state_.macroEdit.openedAtMs == 0) {
        state_.macroEdit.openedAtMs = oc::time::millis();
    }

    const uint8_t macroIndex = state_.macroEdit.editingIndex.get();
    const uint8_t channel0 = state_.macroEdit.tempChannel.get();
    const uint8_t cc = state_.macroEdit.tempCC.get();

    char title[16];
    size_t titlePos = oc::type::text::appendString(title, sizeof(title), 0, "MACRO ");
    titlePos = oc::type::text::appendUnsigned(title, sizeof(title), titlePos, static_cast<unsigned>(macroIndex) + 1U);
    oc::type::text::terminate(title, sizeof(title), titlePos);

    const unsigned page1 = static_cast<unsigned>(state_.pages.activePage) + 1U;

    char meta[16];
    size_t metaPos = oc::type::text::appendString(meta, sizeof(meta), 0, "PAGE ");
    metaPos = oc::type::text::appendUnsigned(meta, sizeof(meta), metaPos, page1);
    oc::type::text::terminate(meta, sizeof(meta), metaPos);

    char channelStr[8];
    oc::type::text::formatUnsigned(channelStr, sizeof(channelStr), static_cast<unsigned>(channel0) + 1U);

    char ccStr[8];
    oc::type::text::formatUnsigned(ccStr, sizeof(ccStr), static_cast<unsigned>(cc));

    const ms::ui::KeyValueRow rows[] = {
        {.key = "Channel", .value = channelStr},
        {.key = "CC", .value = ccStr},
    };

    const uint32_t dataRevision =
        (static_cast<uint32_t>(macroIndex) << 24) |
        (static_cast<uint32_t>(channel0) << 16) |
        (static_cast<uint32_t>(cc) << 8) |
        (static_cast<uint32_t>(state_.pages.activePage & 0x0F) << 4) |
        static_cast<uint32_t>(state_.macroEdit.focusedRow.get() & 0x0F);

    macro_edit_overlay_.render({
        .title = title,
        .meta = meta,
        .rows = rows,
        .rowCount = 2,
        .selectedIndex = state_.macroEdit.focusedRow.get(),
        .visible = true,
        .dataRevision = dataRevision,
    });
}

FLASHMEM void MacroOverlayPresenter::renderEditSelector() {
    const auto& selector = state_.macroEdit.selector;
    if (!selector.visible.get()) {
        macro_edit_selector_overlay_.render({.visible = false});
        return;
    }

    initializeStaticItems_();

    const uint8_t row = selector.editingRow.get();
    const bool isChannel = row == 0;
    const int itemCount = isChannel ? 16 : 128;
    const char* const* items = isChannel ? channel_items_.data() : cc_items_.data();
    const int selected = std::clamp(selector.selectedIndex.get(), 0, itemCount - 1);
    const char* meta = isChannel ? "CHANNEL" : "CC";

    macro_edit_selector_overlay_.render({
        .title = "VALUE",
        .meta = meta,
        .items = items,
        .itemCount = itemCount,
        .selectedIndex = selected,
        .showIndexColumn = false,
        .visible = true,
        .dataRevision = static_cast<uint32_t>(row + 1U),
    });
}

FLASHMEM void MacroOverlayPresenter::renderPageSelector() {
    if (!state_.pages.selector.visible.get()) {
        page_selector_overlay_.render({.visible = false});
        return;
    }

    std::array<const char*, core::state::macro::PAGE_COUNT> pageItems{};
    for (uint8_t i = 0; i < core::state::macro::PAGE_COUNT; ++i) {
        pageItems[i] = state_.pages.pageName(i);
    }

    const int selected = std::clamp(
        static_cast<int>(state_.pages.selector.selectedIndex.get()),
        0,
        static_cast<int>(core::state::macro::PAGE_COUNT) - 1
    );

    page_selector_overlay_.render({
        .title = "PAGE",
        .meta = "MACRO",
        .items = pageItems.data(),
        .itemCount = core::state::macro::PAGE_COUNT,
        .selectedIndex = selected,
        .showIndexColumn = false,
        .visible = true,
        .dataRevision = state_.configRevision.get(),
    });
}

FLASHMEM void MacroOverlayPresenter::renderTargetSelector() {
    if (!state_.macroEdit.macroSelector.visible.get()) {
        macro_target_selector_overlay_.render({.visible = false});
        return;
    }

    initializeStaticItems_();

    const int selected = std::clamp(
        state_.macroEdit.macroSelector.selectedIndex.get(),
        0,
        static_cast<int>(core::state::MACRO_COUNT) - 1
    );

    macro_target_selector_overlay_.render({
        .title = "MACRO",
        .meta = "TARGET",
        .items = macro_items_.data(),
        .itemCount = core::state::MACRO_COUNT,
        .selectedIndex = selected,
        .showIndexColumn = false,
        .visible = true,
        .dataRevision = 1,
    });
}

FLASHMEM void MacroOverlayPresenter::initializeStaticItems_() {
    if (static_items_initialized_) return;

    for (int i = 0; i < 16; ++i) {
        oc::type::text::formatUnsigned(channel_labels_[i].data(), channel_labels_[i].size(), i + 1);
        channel_items_[i] = channel_labels_[i].data();
    }

    for (int i = 0; i < 128; ++i) {
        oc::type::text::formatUnsigned(cc_labels_[i].data(), cc_labels_[i].size(), i);
        cc_items_[i] = cc_labels_[i].data();
    }

    for (uint8_t i = 0; i < core::state::MACRO_COUNT; ++i) {
        size_t pos = oc::type::text::appendString(macro_labels_[i].data(), macro_labels_[i].size(), 0, "Macro ");
        pos = oc::type::text::appendUnsigned(
            macro_labels_[i].data(),
            macro_labels_[i].size(),
            pos,
            static_cast<unsigned>(i) + 1U
        );
        oc::type::text::terminate(macro_labels_[i].data(), macro_labels_[i].size(), pos);
        macro_items_[i] = macro_labels_[i].data();
    }

    static_items_initialized_ = true;
}

}  // namespace core::context::standalone
