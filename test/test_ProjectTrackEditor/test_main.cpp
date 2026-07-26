#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/project/ProjectTrackEditorOps.hpp"
#include "ui/project/ProjectTrackEditorViewModel.hpp"

namespace project = core::state::project;
namespace ui = core::ui::project;

namespace {

void testStateOpenCloseBoundsAndNoOps() {
    project::ProjectTrackEditorState editor{};
    constexpr uint16_t enabled = 0x8009U;

    assert(sizeof(editor) == 8U);
    assert(!editor.active);
    assert(editor.trackIndex == 0U);
    assert(editor.selectedProperty ==
           project::ProjectTrackEditorProperty::CHANNEL);

    assert(project::openProjectTrackEditor(editor, 16U, enabled).status ==
           project::ProjectTrackEditorMutationStatus::INVALID_TRACK);
    assert(project::openProjectTrackEditor(editor, 1U, enabled).status ==
           project::ProjectTrackEditorMutationStatus::TRACK_DISABLED);
    assert(editor.revision == 0U);
    assert(!editor.active);

    assert(project::openProjectTrackEditor(editor, 3U, enabled).changed());
    assert(editor.active);
    assert(editor.trackIndex == 3U);
    assert(editor.revision == 1U);
    assert(project::openProjectTrackEditor(editor, 3U, enabled).status ==
           project::ProjectTrackEditorMutationStatus::NO_CHANGE);
    assert(editor.revision == 1U);

    assert(project::closeProjectTrackEditor(editor).changed());
    assert(!editor.active);
    assert(editor.trackIndex == 3U);
    assert(editor.revision == 2U);
    assert(project::closeProjectTrackEditor(editor).status ==
           project::ProjectTrackEditorMutationStatus::NO_CHANGE);
    assert(editor.revision == 2U);

    editor.reset();
    assert(editor.revision == 0U);
    assert(editor.trackIndex == 0U);
    assert(!editor.active);
}

void testSparseEnabledTrackWrapAndEmptyMask() {
    constexpr uint16_t sparse = 0x8089U;  // Tracks 1, 4, 8 and 16.

    assert(project::nextEnabledProjectTrack(sparse, 0U, 1) == 3U);
    assert(project::nextEnabledProjectTrack(sparse, 3U, 1) == 7U);
    assert(project::nextEnabledProjectTrack(sparse, 7U, 1) == 15U);
    assert(project::nextEnabledProjectTrack(sparse, 15U, 1) == 0U);
    assert(project::nextEnabledProjectTrack(sparse, 0U, -1) == 15U);
    assert(project::nextEnabledProjectTrack(sparse, 7U, -1) == 3U);
    assert(project::nextEnabledProjectTrack(sparse, 5U, 1) == 7U);
    assert(project::nextEnabledProjectTrack(sparse, 5U, -1) == 3U);
    assert(project::nextEnabledProjectTrack(sparse, 16U, 1) == 0U);
    assert(project::nextEnabledProjectTrack(sparse, 16U, -1) == 15U);
    assert(project::nextEnabledProjectTrack(sparse, 7U, 0) == 7U);
    assert(project::nextEnabledProjectTrack(sparse, 6U, 0) ==
           project::PROJECT_TRACK_EDITOR_NO_TRACK);
    assert(project::nextEnabledProjectTrack(0U, 0U, 1) ==
           project::PROJECT_TRACK_EDITOR_NO_TRACK);

    project::ProjectTrackEditorState editor{};
    assert(project::openProjectTrackEditor(editor, 0U, sparse).changed());
    assert(project::moveProjectTrackEditorTrack(editor, sparse, -99).changed());
    assert(editor.trackIndex == 15U);
    assert(project::moveProjectTrackEditorTrack(editor, sparse, 99).changed());
    assert(editor.trackIndex == 0U);

    const uint32_t stableRevision = editor.revision;
    assert(project::moveProjectTrackEditorTrack(editor, 0U, 1).status ==
           project::ProjectTrackEditorMutationStatus::NO_ENABLED_TRACK);
    assert(editor.trackIndex == 0U);
    assert(editor.revision == stableRevision);

    constexpr uint16_t oneTrack = 0x0001U;
    assert(project::moveProjectTrackEditorTrack(editor, oneTrack, 1).status ==
           project::ProjectTrackEditorMutationStatus::NO_CHANGE);
    assert(editor.revision == stableRevision);
}

void testRetargetAndPropertySelectionAreAtomic() {
    project::ProjectTrackEditorState editor{};
    constexpr uint16_t enabled = 0x0081U;

    assert(project::retargetProjectTrackEditor(editor, 7U, enabled).status ==
           project::ProjectTrackEditorMutationStatus::INACTIVE);
    assert(editor.revision == 0U);
    assert(project::openProjectTrackEditor(editor, 0U, enabled).changed());

    const auto beforeInvalid = editor;
    assert(project::retargetProjectTrackEditor(editor, 4U, enabled).status ==
           project::ProjectTrackEditorMutationStatus::TRACK_DISABLED);
    assert(project::retargetProjectTrackEditor(editor, 16U, enabled).status ==
           project::ProjectTrackEditorMutationStatus::INVALID_TRACK);
    assert(editor == beforeInvalid);

    assert(project::retargetProjectTrackEditor(editor, 7U, enabled).changed());
    assert(editor.trackIndex == 7U);
    assert(editor.revision == 2U);
    assert(project::retargetProjectTrackEditor(editor, 7U, enabled).status ==
           project::ProjectTrackEditorMutationStatus::NO_CHANGE);
    assert(editor.revision == 2U);

    assert(project::selectProjectTrackEditorProperty(
        editor,
        project::ProjectTrackEditorProperty::DELAY
    ).changed());
    assert(editor.selectedProperty ==
           project::ProjectTrackEditorProperty::DELAY);
    assert(editor.revision == 3U);
    assert(project::selectProjectTrackEditorProperty(
        editor,
        project::ProjectTrackEditorProperty::DELAY
    ).status == project::ProjectTrackEditorMutationStatus::NO_CHANGE);
    assert(editor.revision == 3U);
    assert(project::moveProjectTrackEditorProperty(editor, 1).changed());
    assert(editor.selectedProperty ==
           project::ProjectTrackEditorProperty::CHANNEL);
    assert(project::moveProjectTrackEditorProperty(editor, -1).changed());
    assert(editor.selectedProperty ==
           project::ProjectTrackEditorProperty::DELAY);
    assert(project::moveProjectTrackEditorProperty(editor, 0).status ==
           project::ProjectTrackEditorMutationStatus::NO_CHANGE);

    const uint32_t stableRevision = editor.revision;
    assert(project::selectProjectTrackEditorProperty(
        editor,
        static_cast<project::ProjectTrackEditorProperty>(255U)
    ).status == project::ProjectTrackEditorMutationStatus::INVALID_PROPERTY);
    assert(editor.selectedProperty ==
           project::ProjectTrackEditorProperty::DELAY);
    assert(editor.revision == stableRevision);

    editor.revision = std::numeric_limits<uint32_t>::max();
    assert(project::moveProjectTrackEditorProperty(editor, 1).changed());
    assert(editor.revision == 1U);
}

void testViewModelIsTruthfulBoundedAndMinimal() {
    project::ProjectTrackState tracks{};
    project::ProjectTrackEditorState editor{};

    auto hidden = ui::buildProjectTrackEditorViewModel(editor, tracks, 0x8001U);
    assert(!hidden.visible);
    assert(std::strcmp(hidden.port.data(), "USB") == 0);
    assert(!hidden.portEditable);

    assert(project::setProjectTrackMidiChannel(tracks, 15U, 15U).status ==
           project::ProjectTrackMutationStatus::NO_CHANGE);
    assert(project::setProjectTrackDelayMs(tracks, 15U, -100).changed());
    assert(project::setProjectTrackMuted(tracks, 15U, true).changed());
    assert(project::setProjectTrackSoloed(tracks, 15U, true).changed());
    assert(project::openProjectTrackEditor(editor, 15U, 0x8001U).changed());
    assert(project::selectProjectTrackEditorProperty(
        editor,
        project::ProjectTrackEditorProperty::DELAY
    ).changed());

    const auto model = ui::buildProjectTrackEditorViewModel(
        editor,
        tracks,
        0x8001U
    );
    assert(sizeof(model) <= 32U);
    assert(model.visible);
    assert(model.trackEnabled);
    assert(model.canSwitchTrack);
    assert(model.trackIndex == 15U);
    assert(model.trackNumber == 16U);
    assert(std::strcmp(model.title.data(), "TRACK 16") == 0);
    assert(std::strcmp(model.port.data(), "USB") == 0);
    assert(!model.portEditable);
    assert(model.midiChannel == 16U);
    assert(model.delayMs == -100);
    assert(model.muted);
    assert(model.soloed);
    assert(model.selectedProperty ==
           project::ProjectTrackEditorProperty::DELAY);

    assert(project::setProjectTrackDelayMs(tracks, 0U, 100).changed());
    assert(project::retargetProjectTrackEditor(editor, 0U, 0x8001U).changed());
    const auto lowerRouteBoundary = ui::buildProjectTrackEditorViewModel(
        editor,
        tracks,
        0x8001U
    );
    assert(lowerRouteBoundary.midiChannel == 1U);
    assert(lowerRouteBoundary.delayMs == 100);

    const auto staleStructure = ui::buildProjectTrackEditorViewModel(
        editor,
        tracks,
        0x8000U
    );
    assert(staleStructure.visible);
    assert(!staleStructure.trackEnabled);
    assert(!staleStructure.canSwitchTrack);
}

void testRetargetProjectsOneCoherentDestination() {
    project::ProjectTrackState tracks{};
    project::ProjectTrackEditorState editor{};
    constexpr uint16_t enabled = 0x0081U;

    assert(project::setProjectTrackMidiChannel(tracks, 0U, 2U).changed());
    assert(project::setProjectTrackDelayMs(tracks, 0U, -25).changed());
    assert(project::setProjectTrackMuted(tracks, 0U, true).changed());
    assert(project::setProjectTrackMidiChannel(tracks, 7U, 11U).changed());
    assert(project::setProjectTrackDelayMs(tracks, 7U, 73).changed());
    assert(project::setProjectTrackSoloed(tracks, 7U, true).changed());

    assert(project::openProjectTrackEditor(editor, 0U, enabled).changed());
    auto model = ui::buildProjectTrackEditorViewModel(editor, tracks, enabled);
    assert(model.trackNumber == 1U);
    assert(model.midiChannel == 3U);
    assert(model.delayMs == -25);
    assert(model.muted);
    assert(!model.soloed);

    const uint32_t before = editor.revision;
    assert(project::retargetProjectTrackEditor(editor, 7U, enabled).changed());
    assert(editor.revision == before + 1U);
    model = ui::buildProjectTrackEditorViewModel(editor, tracks, enabled);
    assert(std::strcmp(model.title.data(), "TRACK 8") == 0);
    assert(model.trackIndex == 7U);
    assert(model.midiChannel == 12U);
    assert(model.delayMs == 73);
    assert(!model.muted);
    assert(model.soloed);

    const auto stableEditor = editor;
    const auto stableModel = model;
    assert(project::retargetProjectTrackEditor(editor, 6U, enabled).status ==
           project::ProjectTrackEditorMutationStatus::TRACK_DISABLED);
    assert(editor == stableEditor);
    const auto afterRejected = ui::buildProjectTrackEditorViewModel(
        editor,
        tracks,
        enabled
    );
    assert(afterRejected == stableModel);
}

}  // namespace

int main() {
    testStateOpenCloseBoundsAndNoOps();
    testSparseEnabledTrackWrapAndEmptyMask();
    testRetargetAndPropertySelectionAreAtomic();
    testViewModelIsTruthfulBoundedAndMinimal();
    testRetargetProjectsOneCoherentDestination();
    std::cout << "Project Track Editor tests passed\n";
    return 0;
}
