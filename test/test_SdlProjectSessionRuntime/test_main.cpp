#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <oc/impl/HostFileSystem.hpp>
#include <oc/time/Time.hpp>

#include "../../sdl/entry/SdlProjectSessionRuntime.hpp"
#include "../support/CoreStorages.hpp"

namespace {

uint32_t nowMs = 0;

uint32_t testTimeProvider() {
    return nowMs;
}

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
           "midi-studio-core-sdl-project-session-runtime-test";
}

void resetTestRoot() {
    std::error_code ec;
    std::filesystem::remove_all(testRoot(), ec);
}

core::state::CoreState makeCoreState(test_support::CoreStorages& storages) {
    return core::state::CoreState{
        storages.settings,
        storages.macroLibrary,
        storages.sequencerPatternLibrary,
        storages.sequencerSetLibrary,
    };
}

void configureProject(core::state::CoreState& state, uint8_t note) {
    state.project.metadata.id.fill('\0');
    std::strncpy(state.project.metadata.id.data(),
                 "sdl-parity",
                 state.project.metadata.id.size() - 1U);
    state.project.metadata.name.fill('\0');
    std::strncpy(state.project.metadata.name.data(),
                 "SDL parity",
                 state.project.metadata.name.size() - 1U);
    state.project.metadata.hasSavedIdentity = true;
    state.sequencer.pattern.setContentLength(8);
    state.sequencer.setStepDataAt(0, note, 100, 75);
    state.sequencer.pattern.toggle(0);
    state.markProjectMutated();
}

void updateUntilSaved(ms::entry::SdlProjectSessionRuntime& runtime,
                      core::state::CoreState& state) {
    for (uint16_t step = 0; step < 256 && state.hasPendingProjectSessionSave(); ++step) {
        runtime.update();
    }
    assert(!state.hasPendingProjectSessionSave());
}

void test_restore_and_firmware_ordered_autosave() {
    resetTestRoot();
    oc::time::setProvider(testTimeProvider);

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService productFiles(filesystem);
    assert(productFiles.init());

    {
        test_support::CoreStorages storages;
        auto state = makeCoreState(storages);
        ms::entry::SdlProjectSessionRuntime runtime(productFiles, state, 1);
        assert(runtime.restoreResult().status ==
               core::persistence::ProjectSessionRestoreService::Status::MISSING);

        nowMs = 10;
        configureProject(state, 67);
        assert(state.hasPendingProjectSessionSave());

        // Match firmware ownership gating: an external product-file transfer
        // blocks both CoreState persistence work and project autosave.
        assert(productFiles.beginWrite("tmp/external-write.tmp", 1));
        nowMs = 20;
        runtime.update();
        assert(state.hasPendingProjectSessionSave());
        assert(!std::filesystem::exists(
            testRoot() / "midi-studio" / "session" / "current.mspj"
        ));
        productFiles.abortWrite();

        updateUntilSaved(runtime, state);
        assert(std::filesystem::exists(
            testRoot() / "midi-studio" / "session" / "current.mspj"
        ));
    }

    {
        test_support::CoreStorages storages;
        auto state = makeCoreState(storages);
        ms::entry::SdlProjectSessionRuntime runtime(productFiles, state, 1);
        assert(runtime.restoreResult().restored());
        assert(std::strcmp(state.project.metadata.id.data(), "sdl-parity") == 0);
        assert(state.sequencer.pattern.length.get() == 8);
        assert(state.sequencer.pattern.isEnabled(0));
        assert(state.sequencer.pattern.note[0] == 67);
    }

    resetTestRoot();
    std::cout << "[PASS] test_restore_and_firmware_ordered_autosave\n";
}

}  // namespace

int main() {
    test_restore_and_firmware_ordered_autosave();
    return 0;
}
