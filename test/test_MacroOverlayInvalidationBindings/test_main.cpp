#include <cassert>
#include <cstdint>
#include <iostream>

#include <oc/state/NotificationQueue.hpp>

#include "context/standalone/MacroOverlayInvalidationBindings.hpp"
#include "../support/NotificationTestUtils.hpp"

namespace {

namespace invalidation =
    core::context::standalone::macro_overlay_invalidation;

struct Probe {
    uint32_t flags = 0;
    uint32_t calls = 0;

    static void invalidate(void* context, uint32_t renderFlags) {
        auto* self = static_cast<Probe*>(context);
        assert(self != nullptr);
        self->flags |= renderFlags;
        ++self->calls;
    }

    void reset() {
        flags = 0;
        calls = 0;
    }
};

}  // namespace

int main() {
    test_support::drainNotifications();

    static core::state::MacroEditState macroEdit;
    static core::state::macro::MacroPagesState pages;
    static core::state::macro::MacroUiState macroUi;
    static core::state::StructureClipboardState clipboard;
    static oc::state::Signal<uint32_t> configRevision{0};

    invalidation::Bindings bindings;
    Probe probe;
    const invalidation::StateRefs stateRefs{
        macroEdit,
        pages,
        macroUi,
        configRevision,
        &clipboard,
    };

    assert(macroEdit.flowPhase.subscriberCount() == 0);
    assert(bindings.bind(stateRefs, &probe, &Probe::invalidate));
    assert(bindings.phaseSubscriptionCount() == 1);
    assert(macroEdit.flowPhase.subscriberCount() == 1);
    assert(bindings.subscriptionCount() == 34);
    assert(macroUi.runtimeProjectionRevision.subscriberCount() == 1);
    assert(macroEdit.modulatorNavigationFeedback.subscriberCount() == 1);
    assert(clipboard.revision.subscriberCount() == 1);
    assert(macroUi.recordedShapeCaptureRevision.subscriberCount() == 1);
    assert(macroUi.automationRecordingRevision.subscriberCount() == 0);
    assert(macroUi.automationEditRevision.subscriberCount() == 1);
    assert(macroUi.automationRecordingStatus.subscriberCount() == 2);

    // Rebinding replaces every RAII subscription instead of accumulating them.
    assert(bindings.bind(stateRefs, &probe, &Probe::invalidate));
    assert(bindings.phaseSubscriptionCount() == 1);
    assert(macroEdit.flowPhase.subscriberCount() == 1);
    assert(bindings.subscriptionCount() == 34);
    assert(macroUi.runtimeProjectionRevision.subscriberCount() == 1);
    assert(macroEdit.modulatorNavigationFeedback.subscriberCount() == 1);
    assert(clipboard.revision.subscriberCount() == 1);
    assert(macroUi.recordedShapeCaptureRevision.subscriberCount() == 1);
    assert(macroUi.automationRecordingRevision.subscriberCount() == 0);
    assert(macroUi.automationEditRevision.subscriberCount() == 1);
    assert(macroUi.automationRecordingStatus.subscriberCount() == 2);

    macroEdit.flowPhase.set(core::state::MacroEditFlowPhase::EDIT);
    test_support::drainNotifications();
    assert(probe.calls == 1);
    assert(probe.flags == invalidation::PHASE_RENDER_MASK);
    assert(probe.flags ==
           (invalidation::RENDER_EDIT |
            invalidation::RENDER_AUTOMATION |
            invalidation::RENDER_EDIT_SELECTOR));

    probe.reset();
    macroEdit.modulatorPickerIndex.set(1);
    test_support::drainNotifications();
    assert(probe.calls == 1);
    assert(probe.flags == invalidation::RENDER_AUTOMATION);

    probe.reset();
    clipboard.revision.set(clipboard.revision.get() + 1U);
    test_support::drainNotifications();
    assert(probe.calls == 1);
    assert(probe.flags ==
           (invalidation::RENDER_EDIT | invalidation::RENDER_AUTOMATION));

    probe.reset();
    macroUi.recordedShapeCaptureRevision.set(
        macroUi.recordedShapeCaptureRevision.get() + 1U
    );
    test_support::drainNotifications();
    assert(probe.calls == 1);
    assert(probe.flags ==
           (invalidation::RENDER_EDIT | invalidation::RENDER_AUTOMATION));

    // Per-sample Automation updates are read directly by the retained curve
    // marker. They must not schedule a full editor/curve render.
    probe.reset();
    macroUi.automationRecordingRevision.set(
        macroUi.automationRecordingRevision.get() + 1U
    );
    test_support::drainNotifications();
    assert(probe.calls == 0);

    probe.reset();
    macroUi.automationEditRevision.set(
        macroUi.automationEditRevision.get() + 1U
    );
    test_support::drainNotifications();
    assert(probe.calls == 1);
    assert(probe.flags ==
           (invalidation::RENDER_EDIT | invalidation::RENDER_AUTOMATION));

    probe.reset();
    macroUi.automationRecordingStatus.set(
        core::state::macro::MacroAutomationRecordingStatus::RECORDING
    );
    test_support::drainNotifications();
    assert(probe.calls == 2);
    assert(probe.flags ==
           (invalidation::RENDER_EDIT | invalidation::RENDER_AUTOMATION));

    probe.reset();
    macroEdit.contextSelectorActive.set(true);
    test_support::drainNotifications();
    assert(probe.calls == 1);
    assert(probe.flags == invalidation::RENDER_EDIT);

    probe.reset();
    macroUi.runtimeProjectionRevision.set(
        macroUi.runtimeProjectionRevision.get() + 1U
    );
    test_support::drainNotifications();
    assert(probe.calls == 1);
    assert(probe.flags == invalidation::RENDER_EDIT_LIVE);

    bindings.clear();
    assert(bindings.subscriptionCount() == 0);
    assert(macroEdit.flowPhase.subscriberCount() == 0);
    assert(macroEdit.modulatorNavigationFeedback.subscriberCount() == 0);
    assert(macroUi.runtimeProjectionRevision.subscriberCount() == 0);
    assert(macroUi.automationEditRevision.subscriberCount() == 0);
    assert(clipboard.revision.subscriberCount() == 0);
    assert(macroUi.recordedShapeCaptureRevision.subscriberCount() == 0);
    assert(macroUi.automationRecordingStatus.subscriberCount() == 0);

    std::cout << "Macro overlay invalidation binding tests passed\n";
    return 0;
}
