#include "validation/project/ProjectStoreSmoke.hpp"

#include <array>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/type/Result.hpp>

#include "persistence/ProjectFileStore.hpp"
#include "persistence/ProductFileService.hpp"
#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/project/ProjectSnapshot.hpp"

namespace core::validation::project {
namespace {

constexpr const char* PROJECT_STORE_SMOKE_ID = "smk001";
constexpr const char* PROJECT_STORE_SMOKE_NAME = "sd-smoke";

FLASHMEM void copySmokeText(char* target, size_t targetSize, const char* source) {
    if (targetSize == 0) return;
    std::strncpy(target, source, targetSize - 1U);
    target[targetSize - 1U] = '\0';
}

template <size_t N>
FLASHMEM void copySmokeText(std::array<char, N>& target, const char* source) {
    copySmokeText(target.data(), target.size(), source);
}

FLASHMEM void configureProjectStoreSmokeState(core::state::CoreState& state,
                                              uint32_t modifiedCounter) {
    copySmokeText(state.project.metadata.id, PROJECT_STORE_SMOKE_ID);
    copySmokeText(state.project.metadata.name, PROJECT_STORE_SMOKE_ID);
    state.project.metadata.modifiedCounter = modifiedCounter;
    state.project.metadata.hasSavedIdentity = true;
    state.project.metadata.dirty = false;

    const float tempo = 137.0f;
    state.statusBar.tempo.set(tempo);
    state.statusBar.tempoDisplay.set(tempo);
    state.projectNavigation.transportSwingPercent = 17;

    auto& page = state.pages.activePageData();
    copySmokeText(page.name, sizeof(page.name), PROJECT_STORE_SMOKE_NAME);
    page.cc[2] = 74;
    page.values[2] = 0.42f;
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.sequencer.pattern.setContentLength(12);
    state.sequencer.pattern.stepsPerBeat.set(4);
    state.sequencer.setStepDataAt(3, 65, 111, 72);
    if (!state.sequencer.pattern.isEnabled(3)) {
        state.sequencer.pattern.toggle(3);
    }
    state.sequencer.focusedStep.set(3);
}

FLASHMEM void mutateProjectStoreSmokeState(core::state::CoreState& state) {
    copySmokeText(state.project.metadata.id, PROJECT_STORE_SMOKE_ID);
    copySmokeText(state.project.metadata.name, PROJECT_STORE_SMOKE_ID);
    state.project.metadata.modifiedCounter = 999;

    state.statusBar.tempo.set(88.0f);
    state.statusBar.tempoDisplay.set(88.0f);
    state.projectNavigation.transportSwingPercent = 0;

    auto& page = state.pages.activePageData();
    copySmokeText(page.name, sizeof(page.name), "Mutated");
    page.cc[2] = 12;
    page.values[2] = 0.01f;
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.sequencer.setStepDataAt(3, 40, 1, 1);
    if (state.sequencer.pattern.isEnabled(3)) {
        state.sequencer.pattern.toggle(3);
    }
    state.sequencer.focusedStep.set(0);
}

FLASHMEM bool verifyProjectStoreSmokeState(const core::state::CoreState& state) {
    const auto& page = state.pages.activePageData();
    return std::strcmp(state.project.metadata.id.data(), PROJECT_STORE_SMOKE_ID) == 0 &&
           std::strcmp(state.project.metadata.name.data(), PROJECT_STORE_SMOKE_ID) == 0 &&
           state.project.metadata.modifiedCounter == 41U &&
           static_cast<int>(state.statusBar.tempo.get() + 0.5f) == 137 &&
           state.projectNavigation.transportSwingPercent == 17U &&
           std::strcmp(page.name, PROJECT_STORE_SMOKE_NAME) == 0 &&
           page.cc[2] == 74U &&
           state.sequencer.pattern.length.get() == 12U &&
           state.sequencer.pattern.note[3] == 65U &&
           state.sequencer.pattern.velocity[3] == 111U &&
           state.sequencer.pattern.gate[3] == 72U &&
           state.sequencer.pattern.isEnabled(3) &&
           state.sequencer.focusedStep.get() == 3U;
}

}  // namespace

FLASHMEM bool runProjectStoreSmoke(core::persistence::ProductFileService& productFiles,
                                   core::state::CoreState& state) {
    OC_LOG_INFO("[project-store-smoke] start");

    configureProjectStoreSmokeState(state, 41);

    auto savedSnapshot = core::state::project::captureProjectSnapshotOwned(state);
    if (!savedSnapshot) {
        OC_LOG_ERROR("[project-store-smoke] capture failed");
        return false;
    }

    core::persistence::ProjectFileStore store(productFiles);
    auto saved = store.save(*savedSnapshot);
    if (!saved) {
        OC_LOG_ERROR("[project-store-smoke] save failed: {} context={}",
                     oc::type::errorCodeToString(saved.error().code),
                     saved.error().context ? saved.error().context : "none");
        return false;
    }
    OC_LOG_INFO("[project-store-smoke] saved {} bytes to {}",
                saved.value().bytesWritten,
                saved.value().projectPath);

    mutateProjectStoreSmokeState(state);

    auto loadedSnapshot = core::state::project::makeProjectSnapshot();
    if (!loadedSnapshot) {
        OC_LOG_ERROR("[project-store-smoke] snapshot allocation failed");
        return false;
    }
    core::persistence::project_file::LoadReport report{};
    auto loaded = store.load(PROJECT_STORE_SMOKE_ID, *loadedSnapshot, &report);
    if (!loaded) {
        OC_LOG_ERROR("[project-store-smoke] load failed: {}",
                     oc::type::errorCodeToString(loaded.error().code));
        return false;
    }
    OC_LOG_INFO("[project-store-smoke] loaded {} bytes",
                loaded.value().bytesRead);

    if (!report.ok()) {
        OC_LOG_ERROR("[project-store-smoke] load report is not OK status={}",
                     static_cast<int>(report.status));
        return false;
    }

    if (!core::state::project::applyProjectSnapshot(state, *loadedSnapshot)) {
        OC_LOG_ERROR("[project-store-smoke] apply failed");
        return false;
    }

    if (!verifyProjectStoreSmokeState(state)) {
        OC_LOG_ERROR("[project-store-smoke] verification failed");
        return false;
    }

    OC_LOG_INFO("[project-store-smoke] OK");
    return true;
}

}  // namespace core::validation::project
