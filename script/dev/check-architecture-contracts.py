#!/usr/bin/env python3

import argparse
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = ROOT / "src"
PLATFORMIO = ROOT / "platformio.ini"
PRODUCT_LINKER = ROOT / "script" / "pio" / "imxrt1062_t41_product.ld"
UX_LINKER = ROOT / "script" / "pio" / "imxrt1062_t41_ux_recorder.ld"
DIAGNOSTICS_LINKER = ROOT / "script" / "pio" / "imxrt1062_t41_diagnostics.ld"
COLD_PLACEMENT = ROOT / "script" / "pio" / "imxrt1062_t41_cold_placement.ld"
MEMORY_GATE = ROOT / "script" / "pio" / "check_memory_budget.py"
ATTENTION_LINE_THRESHOLD = 800
ATTENTION_SUFFIXES = frozenset((".c", ".cc", ".cpp", ".h", ".hpp", ".ux"))

FORBIDDEN_LEGACY = (
    "PERF_LOG",
    "PerfWindowCounters",
    "SequencerPlaybackProfiler",
    "SequencerRuntimePerfReporter",
    "MacroButtonWidget",
    "BaseMacroWidget",
    "IMacroWidget",
    "DataManager",
    "Data Manager",
    "MacroPersistence",
    "PersistenceSlotFileStore",
)

FORBIDDEN_PERSISTENCE_PATHS = (
    "/macros.bin",
    "./macros.bin",
    "/patterns.bin",
    "./patterns.bin",
    "/sets.bin",
    "./sets.bin",
    "macro-workspace",
    "sequencer-workspace",
)

FORBIDDEN_MUTATION_SYMBOLS = (
    "eraseCurrentStructure",
    "removeCurrentStructure",
    "applyBottomLeftTapCurrentStructure",
    "eraseTrack",
    "erasePage",
    "removeMacroAutomation",
    "removeSlot",
    "removePage",
)

ALLOWED_LOW_LEVEL_ERASE_SYMBOLS = frozenset(
    (
        "eraseCurveRecord",
        "eraseDense",
        "eraseIdentity",
        "eraseWithBarrier",
    )
)

CORE_STATE_PERSISTENCE_COMPOSITION_INCLUDES = frozenset(
    (
        "persistence/DeviceSettingsStore.hpp",
        "persistence/PersistenceStatus.hpp",
    )
)

FORBIDDEN_HEAP_REACTIVE_STORAGE = (
    "oc/state/SignalWatcher.hpp",
    "oc::state::SignalWatcher",
    "std::vector<oc::state::Subscription>",
)

DIRECT_EXTMEM_OWNERS = (
    "app/ExtmemAllocator.hpp",
    "state/CoreState.cpp",
)

RETAINED_VIEW_CONSTRUCTION = re.compile(
    r"makeExtmemUnique\s*<\s*core::ui::([A-Za-z0-9_]+View)\s*>"
)

DIRECT_EXTMEM_CALL = re.compile(
    r"\bextmem_(?:malloc|calloc|realloc|free)\s*\("
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

BOTTOM_CENTER_BINDING = re.compile(
    r"\.button\s*\(\s*(?:Config::)?ButtonID::BOTTOM_CENTER\s*\)"
)

GLOBAL_PASS_THROUGH = re.compile(r"\.globalPassThrough\s*\(\s*\)")

INCLUDE_DIRECTIVE = re.compile(
    r'^\s*#\s*include\s*[<"]([^">]+)[">]',
    flags=re.MULTILINE,
)

DOMAIN_ERASE_SYMBOL = re.compile(r"\berase[A-Z][A-Za-z0-9_]*\b")

MARKDOWN_LINK = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")


def source_files():
    for suffix in ("*.h", "*.hpp", "*.c", "*.cc", "*.cpp"):
        yield from SOURCE_ROOT.rglob(suffix)


def product_implementation_files():
    yield ROOT / "main.cpp"
    for suffix in ("*.hpp", "*.cpp", "*.ux"):
        yield from (ROOT / "sdl").rglob(suffix)
    yield from source_files()


def relative(path: Path) -> str:
    return path.relative_to(SOURCE_ROOT).as_posix()


def layer_dependency_error(rel: str, include: str) -> str | None:
    forbidden_targets: tuple[str, ...] = ()
    if rel.startswith("handler/"):
        forbidden_targets = ("ui/", "context/")
    elif rel.startswith("persistence/"):
        forbidden_targets = ("handler/", "ui/", "context/")
    elif rel.startswith("sequencer/"):
        forbidden_targets = ("handler/", "ui/", "context/", "persistence/")
    elif rel.startswith("ui/"):
        forbidden_targets = ("handler/", "context/", "persistence/")
    elif rel.startswith("state/"):
        forbidden_targets = ("handler/", "ui/", "context/")
        if include.startswith("persistence/") and not (
            rel == "state/CoreState.hpp"
            and include in CORE_STATE_PERSISTENCE_COMPOSITION_INCLUDES
        ):
            return (
                f"{rel}: State must not depend on Persistence "
                f"({include}); CoreState.hpp has only the explicit "
                "Device Settings composition includes"
            )

    if include.startswith(forbidden_targets):
        target = include.split("/", maxsplit=1)[0]
        owner = rel.split("/", maxsplit=1)[0]
        return f"{rel}: {owner} must not depend on {target} ({include})"

    if (
        rel.startswith("state/CoreState")
        and include.startswith("sequencer/")
    ):
        return (
            f"{rel}: ambiguous Core State include {include}; "
            "use state/sequencer/... explicitly"
        )
    return None


def mutation_contract_errors(rel: str, content: str) -> list[str]:
    if not rel.startswith(("state/", "handler/", "persistence/")):
        return []

    errors: list[str] = []
    for symbol in sorted(set(DOMAIN_ERASE_SYMBOL.findall(content))):
        if symbol not in ALLOWED_LOW_LEVEL_ERASE_SYMBOLS:
            errors.append(
                f"{rel}: product-domain symbol {symbol} violates the "
                "ADR-0065 erase contract"
            )
    for symbol in FORBIDDEN_MUTATION_SYMBOLS:
        if re.search(rf"\b{re.escape(symbol)}\b", content):
            errors.append(f"{rel}: obsolete mutation symbol {symbol}")
    return errors


def attention_category(rel: str) -> str | None:
    if rel.startswith("test/"):
        return "tests"
    if rel.startswith("sdl/"):
        return "sdl"
    if rel.startswith(
        ("src/validation/", "src/context/standalone/ux/")
    ):
        return "validation"
    if rel.startswith("src/"):
        return "product"
    return None


def version_control_source_candidates() -> list[Path]:
    result = subprocess.run(
        (
            "git",
            "ls-files",
            "--cached",
            "--others",
            "--exclude-standard",
            "-z",
            "--",
            "src",
            "test",
            "sdl",
        ),
        cwd=ROOT,
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"git ls-files failed: {detail}")

    paths: list[Path] = []
    for raw_path in result.stdout.split(b"\0"):
        if not raw_path:
            continue
        rel = raw_path.decode("utf-8", errors="strict").replace("\\", "/")
        path = ROOT / rel
        if path.is_file() and path.suffix.lower() in ATTENTION_SUFFIXES:
            paths.append(path)
    return paths


def attention_inventory(paths: list[Path]) -> list[tuple[str, int, str]]:
    inventory: list[tuple[str, int, str]] = []
    for path in paths:
        rel = path.relative_to(ROOT).as_posix()
        category = attention_category(rel)
        if category is None:
            continue
        line_count = len(path.read_text(encoding="utf-8").splitlines())
        if line_count > ATTENTION_LINE_THRESHOLD:
            inventory.append((category, line_count, rel))
    return sorted(inventory, key=lambda row: (-row[1], row[2]))


def local_markdown_target(raw_target: str) -> str | None:
    target = raw_target.strip().strip("<>")
    if not target or target.startswith(
        ("#", "http://", "https://", "mailto:")
    ):
        return None
    return target.split("#", maxsplit=1)[0]


def documentation_contract_errors() -> list[str]:
    required_docs = (
        ROOT / "README.md",
        ROOT / "docs" / "README.md",
        ROOT / "docs" / "DEVELOPER_ONBOARDING.md",
        ROOT / "docs" / "CORE_ARCHITECTURE.md",
        ROOT / "docs" / "INPUT_BINDINGS.md",
        ROOT / "docs" / "ARCHITECTURE_REVIEW_RULES.md",
    )
    errors: list[str] = []
    for path in required_docs:
        if not path.is_file():
            errors.append(
                f"{path.relative_to(ROOT).as_posix()}: "
                "required developer entrypoint is missing"
            )
            continue
        content = path.read_text(encoding="utf-8")
        rel = path.relative_to(ROOT).as_posix()
        if re.search(r"\b(?:pio|platformio)\s+test\b", content, re.IGNORECASE):
            errors.append(
                f"{rel}: native tests must use the workspace ms test command"
            )
        for raw_target in MARKDOWN_LINK.findall(content):
            target = local_markdown_target(raw_target)
            if target is None:
                continue
            resolved = path.parent / target
            if not resolved.exists():
                errors.append(f"{rel}: broken local link {target}")
    return errors


def self_test() -> int:
    checks = (
        (
            layer_dependency_error(
                "handler/macro/Example.cpp",
                "ui/view/MacroView.hpp",
            )
            is not None,
            "Handler-to-UI dependency is rejected",
        ),
        (
            layer_dependency_error(
                "handler/macro/Example.cpp",
                "state/macro/MacroState.hpp",
            )
            is None,
            "Handler-to-State dependency is accepted",
        ),
        (
            layer_dependency_error(
                "state/CoreState.hpp",
                "persistence/DeviceSettingsStore.hpp",
            )
            is None,
            "CoreState composition exception is accepted",
        ),
        (
            layer_dependency_error(
                "state/CoreState.hpp",
                "persistence/UnrelatedStore.hpp",
            )
            is not None,
            "CoreState composition exception is exact, not directory-wide",
        ),
        (
            layer_dependency_error(
                "state/sequencer/Example.cpp",
                "persistence/ExampleCodec.hpp",
            )
            is not None,
            "general State-to-Persistence dependency is rejected",
        ),
        (
            layer_dependency_error(
                "state/CoreState.cpp",
                "sequencer/SequencerState.hpp",
            )
            is not None,
            "ambiguous CoreState Sequencer include is rejected",
        ),
        (
            bool(
                mutation_contract_errors(
                    "handler/macro/Example.cpp",
                    "void erasePage();",
                )
            ),
            "product-domain erase verb is rejected",
        ),
        (
            not mutation_contract_errors(
                "persistence/Example.cpp",
                "void eraseWithBarrier();",
            ),
            "low-level erase primitive is accepted",
        ),
        (
            attention_category(
                "src/context/standalone/ux/StandaloneMacroUxSurfaces.cpp"
            )
            == "validation",
            "semantic UX source is classified as validation",
        ),
        (
            attention_category("src/handler/macro/MacroEditHandler.cpp")
            == "product",
            "product source is classified as product",
        ),
        (
            attention_category("test/test_MacroHistory/test_main.cpp")
            == "tests",
            "native test is classified as tests",
        ),
        (
            local_markdown_target(
                "CORE_ARCHITECTURE.md#layer-ownership"
            )
            == "CORE_ARCHITECTURE.md",
            "local documentation target strips its anchor",
        ),
        (
            local_markdown_target("https://example.com/doc") is None,
            "external documentation target is ignored",
        ),
    )
    failures = [description for ok, description in checks if not ok]
    if failures:
        for failure in failures:
            print(f"SELF-TEST ERROR: {failure}")
        return 1
    print(f"Architecture contract self-tests: OK ({len(checks)}/{len(checks)})")
    return 0


def main(show_inventory: bool = False) -> int:
    errors: list[str] = []

    errors.extend(documentation_contract_errors())

    platformio = PLATFORMIO.read_text(encoding="utf-8")
    if "board_build.ldscript = script/pio/imxrt1062_t41_product.ld" not in platformio:
        errors.append("platformio.ini: Teensy base must use the product linker script")
    if platformio.count("-D OC_MAX_BUTTONS=48") < 2:
        errors.append(
            "platformio.ini: product/native builds must retain the measured "
            "48-slot physical-button bound"
        )
    if not re.search(
        r"^\s*post:script/pio/check_memory_budget\.py\s*$",
        platformio,
        flags=re.MULTILINE,
    ):
        errors.append(
            "platformio.ini: Teensy builds must execute the post-link memory "
            "and placement gate"
        )

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
        or "normal_build_diagnostics_violations" not in memory_gate
    ):
        errors.append(
            "script/pio/check_memory_budget.py: missing post-link placement gates"
        )

    app_config = (SOURCE_ROOT / "config" / "App.hpp").read_text(encoding="utf-8")
    for marker in (
        "ReleaseRoutingPolicy::OwnerOnly",
        "GestureRoutingPolicy::PressScoped",
        "BindingAmbiguityPolicy::FailClosed",
        "GlobalRoutingPolicy::ExplicitPassThroughOnly",
    ):
        if marker not in app_config:
            errors.append(
                "config/App.hpp: strict physical-button routing must keep "
                f"{marker}"
            )

    reporter = (SOURCE_ROOT / "diagnostics" / "PerformanceReporter.cpp").read_text(
        encoding="utf-8"
    )
    if "DMAMEM uint8_t reporterStorage" not in reporter:
        errors.append(
            "diagnostics/PerformanceReporter.cpp: samples and counters must stay in RAM2"
        )
    memory_reporter = (
        SOURCE_ROOT / "diagnostics" / "MemoryFootprintReporter.cpp"
    ).read_text(encoding="utf-8")
    if "DMAMEM uint8_t memoryHighWaterStorage" not in memory_reporter:
        errors.append(
            "diagnostics/MemoryFootprintReporter.cpp: memory counters must stay in RAM2"
        )

    extmem_allocator = (
        SOURCE_ROOT / "app" / "ExtmemAllocator.hpp"
    ).read_text(encoding="utf-8")
    for marker in (
        "trackExtmemAllocation",
        "trackExtmemFree",
    ):
        if marker not in extmem_allocator:
            errors.append(
                "app/ExtmemAllocator.hpp: diagnostics must track every "
                f"product EXTMEM lifetime via {marker}"
            )

    core_state = (SOURCE_ROOT / "state" / "CoreState.cpp").read_text(
        encoding="utf-8"
    )
    for marker in (
        "trackExtmemAllocation",
        "trackExtmemFree",
    ):
        if marker not in core_state:
            errors.append(
                "state/CoreState.cpp: custom PendingApply EXTMEM lifetime "
                f"must call {marker}"
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

        for include in INCLUDE_DIRECTIVE.findall(content):
            dependency_error = layer_dependency_error(rel, include)
            if dependency_error is not None:
                errors.append(dependency_error)

        errors.extend(mutation_contract_errors(rel, content))

        for marker in FORBIDDEN_LEGACY:
            if marker in content:
                errors.append(f"{rel}: forbidden legacy marker {marker}")

        for marker in FORBIDDEN_HEAP_REACTIVE_STORAGE:
            if marker in content:
                errors.append(
                    f"{rel}: fixed UI signal topology must not allocate via {marker}"
                )

        if DIRECT_EXTMEM_CALL.search(content) and rel not in DIRECT_EXTMEM_OWNERS:
            errors.append(
                f"{rel}: direct EXTMEM allocation bypasses the tracked owners"
            )

        if not rel.startswith("diagnostics/") and "[Perf]" in content:
            errors.append(f"{rel}: performance log formatting belongs in diagnostics/")

        if HOT_UI_FLASHMEM.search(content):
            errors.append(f"{rel}: hot retained-view rendering must stay in ITCM")
        if HOT_RUNTIME_FLASHMEM.search(content):
            errors.append(f"{rel}: main-loop and realtime wrappers must stay in ITCM")

        if BOTTOM_CENTER_BINDING.search(content) and rel != (
            "handler/transport/TransportHandler.cpp"
        ):
            errors.append(
                f"{rel}: BOTTOM_CENTER is reserved for invariant Transport"
            )
        if GLOBAL_PASS_THROUGH.search(content) and rel != (
            "handler/transport/TransportHandler.cpp"
        ):
            errors.append(
                f"{rel}: global pass-through is reserved for invariant Transport"
            )

    for path in product_implementation_files():
        content = path.read_text(encoding="utf-8")
        rel = path.relative_to(ROOT).as_posix()
        for marker in FORBIDDEN_PERSISTENCE_PATHS:
            if marker in content:
                errors.append(f"{rel}: retired fixed-slot persistence path {marker}")

    for retired_path in (
        SOURCE_ROOT / "persistence" / "MacroPersistence.hpp",
        SOURCE_ROOT / "persistence" / "MacroPersistence.cpp",
        SOURCE_ROOT / "persistence" / "PersistenceSlotFileStore.hpp",
        SOURCE_ROOT / "persistence" / "PersistenceSlotFileStore.cpp",
        SOURCE_ROOT / "persistence" / "SequencerPersistence.hpp",
        SOURCE_ROOT / "persistence" / "SequencerPersistence.cpp",
        SOURCE_ROOT / "state" / "DataManagerState.hpp",
        SOURCE_ROOT / "handler" / "settings" / "DataManagerHandler.hpp",
        SOURCE_ROOT / "state" / "CoreSettings.hpp",
        SOURCE_ROOT / "state" / "CoreSettings.cpp",
        SOURCE_ROOT / "state" / "CoreSettingsCodec.hpp",
        SOURCE_ROOT / "state" / "CoreSettingsCodec.cpp",
        SOURCE_ROOT / "state" / "CoreSettingsLayout.hpp",
        SOURCE_ROOT / "state" / "sequencer" / "SequencerGraphAssetCodec.hpp",
        SOURCE_ROOT / "state" / "sequencer" / "SequencerGraphAssetCodec.cpp",
        SOURCE_ROOT / "state" / "sequencer" / "SequencerGraphAssetRecords.hpp",
        SOURCE_ROOT / "handler" / "common" / "MidiCcGlobalFrameCoordinator.hpp",
        SOURCE_ROOT / "handler" / "common" / "MidiCcGlobalFrameCoordinator.cpp",
    ):
        if retired_path.exists():
            errors.append(
                f"{retired_path.relative_to(ROOT).as_posix()}: retired product path "
                "must not be restored"
            )

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

    try:
        inventory = attention_inventory(version_control_source_candidates())
    except RuntimeError as error:
        errors.append(f"attention inventory unavailable: {error}")
        inventory = []

    category_order = ("product", "validation", "tests", "sdl")
    category_counts = {
        category: sum(1 for row in inventory if row[0] == category)
        for category in category_order
    }

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    print(
        "Attention inventory "
        f"(>{ATTENTION_LINE_THRESHOLD} physical lines; advisory): "
        + ", ".join(
            f"{category}={category_counts[category]}"
            for category in category_order
        )
        + f", total={len(inventory)}"
    )
    if show_inventory:
        for category, line_count, rel in inventory:
            print(f"{category:10} {line_count:5} {rel}")
    print("Core architecture contracts: OK")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Check executable Core architecture contracts."
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run deterministic fixture checks for the gate itself",
    )
    parser.add_argument(
        "--inventory",
        action="store_true",
        help="print the full advisory >800-line inventory",
    )
    args = parser.parse_args()
    sys.exit(self_test() if args.self_test else main(args.inventory))
