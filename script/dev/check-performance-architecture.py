#!/usr/bin/env python3

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = ROOT / "src"
PLATFORMIO = ROOT / "platformio.ini"
PRODUCT_LINKER = ROOT / "script" / "pio" / "imxrt1062_t41_product.ld"
UX_LINKER = ROOT / "script" / "pio" / "imxrt1062_t41_ux_recorder.ld"
DIAGNOSTICS_LINKER = ROOT / "script" / "pio" / "imxrt1062_t41_diagnostics.ld"
COLD_PLACEMENT = ROOT / "script" / "pio" / "imxrt1062_t41_cold_placement.ld"
MEMORY_GATE = ROOT / "script" / "pio" / "check_memory_budget.py"

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

    platformio = PLATFORMIO.read_text(encoding="utf-8")
    if "board_build.ldscript = script/pio/imxrt1062_t41_product.ld" not in platformio:
        errors.append("platformio.ini: Teensy base must use the product linker script")

    for linker_path in (PRODUCT_LINKER, UX_LINKER, DIAGNOSTICS_LINKER):
        if not linker_path.exists():
            errors.append(f"script/pio: missing linker script {linker_path.name}")
        elif "INCLUDE script/pio/imxrt1062_t41_cold_placement.ld" not in linker_path.read_text(encoding="utf-8"):
            errors.append(
                f"script/pio/{linker_path.name}: missing shared cold-placement fragment"
            )

    if not COLD_PLACEMENT.exists():
        errors.append("script/pio: missing cold-placement linker fragment")
    else:
        cold_placement = COLD_PLACEMENT.read_text(encoding="utf-8")
        for selector in (
            "*(.text.*_M_manager*)",
            "*(.text.*9subscribe*)",
            "*lv_binfont_loader.c.o(.text* .rodata*)",
            "*lv_draw_sw_box_shadow.c.o(.text* .rodata*)",
            "*lz4.c.o(.text* .rodata*)",
            "*(.text._ZN9ExFatFile4open*)",
            "*(.text._ZN9ExFatFile11openPrivate*)",
            "*(.text._ZN7FatFile4open*)",
            "*(.text._ZN7FatFile15openCachedEntry*)",
        ):
            if selector not in cold_placement:
                errors.append(
                    "script/pio/imxrt1062_t41_cold_placement.ld: "
                    f"missing selector {selector}"
                )
    diagnostics_env = re.search(
        r"\[env:dev_diagnostics\](.*?)(?=\n\[env:|\Z)",
        platformio,
        flags=re.DOTALL,
    )
    if diagnostics_env is None:
        errors.append("platformio.ini: missing dev_diagnostics environment")
    elif (
        "board_build.ldscript = script/pio/imxrt1062_t41_diagnostics.ld"
        not in diagnostics_env.group(1)
    ):
        errors.append(
            "platformio.ini: dev_diagnostics must use its dedicated linker script"
        )

    if not DIAGNOSTICS_LINKER.exists():
        errors.append("script/pio: missing diagnostics linker script")
    else:
        linker = DIAGNOSTICS_LINKER.read_text(encoding="utf-8")
        for object_name in (
            "PerformanceReporter.cpp.o(.text* .rodata*)",
            "MemoryFootprintReporter.cpp.o(.text* .rodata*)",
            "CoreStateDiagnostics.cpp.o(.text* .rodata*)",
        ):
            if object_name not in linker:
                errors.append(
                    "script/pio/imxrt1062_t41_diagnostics.ld: "
                    f"missing Flash placement for {object_name}"
                )

    memory_gate = MEMORY_GATE.read_text(encoding="utf-8")
    if (
        "elf_placement_violations" not in memory_gate
        or "product_placement_violations" not in memory_gate
        or "diagnostics_placement_violations" not in memory_gate
    ):
        errors.append(
            "script/pio/check_memory_budget.py: missing post-link placement gates"
        )

    reporter = (SOURCE_ROOT / "diagnostics" / "PerformanceReporter.cpp").read_text(
        encoding="utf-8"
    )
    if "DMAMEM uint8_t reporterStorage" not in reporter:
        errors.append(
            "diagnostics/PerformanceReporter.cpp: samples and counters must stay in RAM2"
        )

    state_diagnostics = (
        SOURCE_ROOT / "state" / "CoreStateDiagnostics.cpp"
    ).read_text(encoding="utf-8")
    if "#if OC_ENABLE_STATS" not in state_diagnostics:
        errors.append(
            "state/CoreStateDiagnostics.cpp: signal labels must remain diagnostics-only"
        )
    if "FLASHMEM void configureDebugLabels" not in state_diagnostics:
        errors.append(
            "state/CoreStateDiagnostics.cpp: label registration must execute from Flash"
        )

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
