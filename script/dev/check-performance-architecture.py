#!/usr/bin/env python3

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = ROOT / "src"

FORBIDDEN_LEGACY = (
    "PERF_LOG",
    "PerfWindowCounters",
    "SequencerPlaybackProfiler",
    "SequencerRuntimePerfReporter",
    "MacroButtonWidget",
    "BaseMacroWidget",
    "IMacroWidget",
)

FORBIDDEN_HEAP_REACTIVE_STORAGE = (
    "oc/state/SignalWatcher.hpp",
    "oc::state::SignalWatcher",
    "std::vector<oc::state::Subscription>",
)

RETAINED_VIEW_CONSTRUCTION = re.compile(
    r"makeExtmemUnique\s*<\s*core::ui::([A-Za-z0-9_]+View)\s*>"
)

HOT_UI_FLASHMEM = re.compile(
    r"FLASHMEM\s+[^\n]*\b(?:"
    r"CoalescedLvglRenderScheduler::(?:request|resumePending|onTimer|canDrain|drain)|"
    r"StandaloneUiAssembly::(?:scheduleGlobalTrackStripRender|requestGlobalTrackStripRender(?:Ready)?|"
    r"renderGlobalTrackStrip|drainGlobalTrackStripRender)|"
    r"[A-Za-z0-9_]+View::(?:request[A-Za-z0-9_]*|render[A-Za-z0-9_]*|"
    r"drainRender|canDrainRender|processRenderFlags|mark[A-Za-z0-9_]*Dirty)"
    r")\s*\("
)

HOT_RUNTIME_FLASHMEM = re.compile(
    r"FLASHMEM\s+[^\n]*\b(?:"
    r"StandaloneFeatureAssembly::(?:update|onMacroCC|onMacroNoteIn)|"
    r"ProjectSessionAutosaveService::(?:update|inProgress_|writeSessionActive)|"
    r"ProjectSessionStore::(?:saveCurrentInProgress|saveCurrentWriteSessionActive)|"
    r"ProjectSaveTransaction::(?:active|writeSessionActive)|"
    r"FileSystemRpc(?:Endpoint|Handler)::update"
    r")\s*\("
)

def source_files():
    for suffix in ("*.hpp", "*.cpp"):
        yield from SOURCE_ROOT.rglob(suffix)


def relative(path: Path) -> str:
    return path.relative_to(SOURCE_ROOT).as_posix()


def main() -> int:
    errors: list[str] = []

    for path in source_files():
        content = path.read_text(encoding="utf-8")
        rel = relative(path)

        for marker in FORBIDDEN_LEGACY:
            if marker in content:
                errors.append(f"{rel}: forbidden legacy marker {marker}")

        for marker in FORBIDDEN_HEAP_REACTIVE_STORAGE:
            if marker in content:
                errors.append(
                    f"{rel}: fixed UI signal topology must not allocate via {marker}"
                )

        if not rel.startswith("diagnostics/") and "[Perf]" in content:
            errors.append(f"{rel}: performance log formatting belongs in diagnostics/")

        if HOT_UI_FLASHMEM.search(content):
            errors.append(f"{rel}: hot retained-view rendering must stay in ITCM")
        if HOT_RUNTIME_FLASHMEM.search(content):
            errors.append(f"{rel}: main-loop and realtime wrappers must stay in ITCM")

    # Standalone assemblies own retained product views. Derive that set from
    # construction sites so newly added views inherit the contract while
    # short-lived views owned by other contexts (for example boot) do not.
    retained_view_names: set[str] = set()
    for assembly_path in (SOURCE_ROOT / "context" / "standalone").rglob("*.cpp"):
        assembly_content = assembly_path.read_text(encoding="utf-8")
        retained_view_names.update(RETAINED_VIEW_CONSTRUCTION.findall(assembly_content))

    for view_name in sorted(retained_view_names):
        path = SOURCE_ROOT / "ui" / "view" / f"{view_name}.cpp"
        if not path.exists():
            errors.append(
                f"context/standalone: retained view source missing for {view_name}"
            )
            continue
        content = path.read_text(encoding="utf-8")
        rel = relative(path)
        if "RetainedViewRenderPolicy" not in content:
            errors.append(f"{rel}: retained view bypasses RetainedViewRenderPolicy")
        if "CoalescedLvglRenderScheduler" not in content:
            errors.append(f"{rel}: retained view bypasses CoalescedLvglRenderScheduler")

    for rel in (
        "ui/view/PausableLvglTimer.hpp",
        "ui/common/PausableLvglTimer.hpp",
        "ui/common/StaticInvalidationBatch.hpp",
        "ui/common/StaticSurfaceInvalidation.hpp",
    ):
        if (SOURCE_ROOT / rel).exists():
            errors.append(f"{rel}: generic LVGL infrastructure belongs in oc-ui-lvgl")

    for path in source_files():
        rel = relative(path)
        content = path.read_text(encoding="utf-8")
        if "lv_timer_create(" in content:
            errors.append(f"{rel}: use oc::ui::lvgl::PausableTimer")
        if 'extern "C" lv_result_t lv_inv_area' in content:
            errors.append(f"{rel}: direct LVGL invalidation belongs in oc-ui-lvgl")

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    print("Performance architecture contract: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
