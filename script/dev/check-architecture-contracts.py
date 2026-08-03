#!/usr/bin/env python3

import argparse
import functools
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

COLD_PLACEMENT_CONTRACT_SELECTORS = (
    "*SequencerPreparedPageStructureMutationPlan.cpp.o(.text* .rodata*)",
    "*SequencerPreparedPageStructureTransaction.cpp.o(.text* .rodata*)",
    "*SequencerDirectTrackStructureTransaction.cpp.o(.text* .rodata*)",
    "*MacroDirectTrackStructureTransaction.cpp.o(.text* .rodata*)",
    "*SequencerPreparedTrackStructurePlanValidation.cpp.o(.text* .rodata*)",
    "*SequencerPreparedTrackStructureTransaction.cpp.o(.text* .rodata*)",
    "*SharedTrackDomainServices.cpp.o(.text* .rodata*)",
    "*SequencerHistory.cpp.o(.text* .rodata*)",
    "*SequencerStructureHistory.cpp.o(.text* .rodata*)",
    "*SequencerTrackBankOps.cpp.o(.text* .rodata*)",
    "*(.text._ZN4core11persistence20ProjectFileWorkspace7prepareEv*)",
    "*SequencerSnapshotOps.cpp.o(.text* .rodata*)",
    "*(.text._ZNK2oc4note9sequencer18StepSequencerGraph8sequenceEt*)",
    "*(.text._ZNK2oc4note9sequencer18StepSequencerGraph8cycleSetEt*)",
)

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
)

EXTMEM_ALLOCATOR_SOURCE = "src/app/ExtmemAllocator.hpp"
CORE_STATE_SOURCE = "src/state/CoreState.cpp"
CORE_STATE_HEADER = "src/state/CoreState.hpp"
STRICT_EXTMEM_OWNER_SOURCES = frozenset((
    EXTMEM_ALLOCATOR_SOURCE,
    CORE_STATE_SOURCE,
))

STRICT_EXTMEM_CALL = re.compile(
    r"\b(?:allocate|free)ExtmemStrict\s*\("
)

RETAINED_VIEW_CONSTRUCTION = re.compile(
    r"makeExtmemUnique\s*<\s*core::ui::([A-Za-z0-9_]+View)\s*>"
)

DIRECT_EXTMEM_CALL = re.compile(
    r"\bextmem_(?:malloc|calloc|realloc|free)\s*\("
)

DIRECT_SMALLOC_MUTATION_CALL = re.compile(
    r"\bsm_(?:malloc|calloc|realloc|free)_pool\s*\("
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

STEP_DRAFT_LABELS = "src/ui/sequencer/SequencerStepContentDraftTransitionLabels.hpp"
STEP_DRAFT_LABELS_SOURCE = "src/ui/sequencer/SequencerStepContentDraftTransitionLabels.cpp"
STEP_DRAFT_HISTORY = "src/state/sequencer/SequencerHistory.cpp"
PAGE_STRUCTURE_TRANSACTION = (
    "src/handler/sequencer/SequencerPreparedPageStructureTransaction.cpp"
)
PAGE_STRUCTURE_TRANSACTION_HEADER = (
    "src/handler/sequencer/SequencerPreparedPageStructureTransaction.hpp"
)
PAGE_STRUCTURE_MUTATION_PLAN = (
    "src/handler/sequencer/SequencerPreparedPageStructureMutationPlan.cpp"
)
PAGE_STRUCTURE_EDIT_WORKFLOW = (
    "src/handler/sequencer/SequencerStructureEditWorkflow.cpp"
)
PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER = (
    "src/handler/sequencer/SequencerStructureEditWorkflow.hpp"
)
PAGE_STRUCTURE_SELECTION_WORKFLOW = (
    "src/handler/sequencer/SequencerStructureSelectionWorkflow.cpp"
)
PAGE_STRUCTURE_NAVIGATION_WORKFLOW = (
    "src/handler/sequencer/SequencerStructureNavigationWorkflow.cpp"
)
PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER = (
    "src/handler/sequencer/SequencerStructureNavigationWorkflow.hpp"
)
CONTEXT_SELECTOR_WORKFLOW = (
    "src/handler/sequencer/SequencerContextSelectorWorkflow.cpp"
)
CONTEXT_SELECTOR_WORKFLOW_HEADER = (
    "src/handler/sequencer/SequencerContextSelectorWorkflow.hpp"
)
PRESS_HOLD_TURN_RELEASE_GESTURE_HEADER = (
    "src/handler/common/PressHoldTurnReleaseGesture.hpp"
)
MACRO_STRUCTURE_WORKFLOW = "src/handler/macro/MacroStructureWorkflow.cpp"
MACRO_STRUCTURE_WORKFLOW_HEADER = "src/handler/macro/MacroStructureWorkflow.hpp"
MACRO_STRUCTURE_DOMAIN_SERVICES = (
    "src/handler/macro/MacroStructureDomainServices.cpp"
)
MACRO_STRUCTURE_DOMAIN_SERVICES_HEADER = (
    "src/handler/macro/MacroStructureDomainServices.hpp"
)
MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION = (
    "src/handler/macro/MacroDirectTrackStructureTransaction.cpp"
)
MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION_HEADER = (
    "src/handler/macro/MacroDirectTrackStructureTransaction.hpp"
)
MACRO_PERFORMANCE_HANDLER = "src/handler/macro/MacroPerformanceHandler.cpp"
MACRO_VIEW = "src/ui/view/MacroView.cpp"
STRUCTURE_NAVIGATION_STATE = "src/state/StructureNavigationState.cpp"
STRUCTURE_NAVIGATION_STATE_HEADER = "src/state/StructureNavigationState.hpp"
DIRECT_TRACK_STRUCTURE_TRANSACTION = (
    "src/handler/sequencer/SequencerDirectTrackStructureTransaction.cpp"
)
DIRECT_TRACK_STRUCTURE_TRANSACTION_HEADER = (
    "src/handler/sequencer/SequencerDirectTrackStructureTransaction.hpp"
)
SHARED_TRACK_DOMAIN_SERVICES_HEADER = (
    "src/handler/common/SharedTrackDomainServices.hpp"
)
SHARED_TRACK_DOMAIN_SERVICES = (
    "src/handler/common/SharedTrackDomainServices.cpp"
)
CORE_SEQUENCER_HISTORY_TRAVERSAL = (
    "src/state/CoreStateSequencerHistoryTraversal.cpp"
)
CORE_SEQUENCER_HISTORY_RECORDING = (
    "src/state/CoreStateSequencerHistoryRecording.cpp"
)
SEQUENCER_STEP_HANDLER = "src/handler/sequencer/SequencerStepHandler.cpp"
SEQUENCER_STEP_HANDLER_HEADER = "src/handler/sequencer/SequencerStepHandler.hpp"
SEQUENCER_VIEW = "src/ui/view/SequencerView.cpp"
SEQUENCER_VIEW_HEADER = "src/ui/view/SequencerView.hpp"
SEQUENCER_OVERLAY_PRESENTER = (
    "src/context/standalone/SequencerOverlayPresenter.cpp"
)
BUTTON_RELEASE_LATCH_HEADER = "src/handler/common/ButtonReleaseLatch.hpp"
RETIRED_RAW_PAGE_SYMBOLS = (
    "pastePageClipboard",
    "pastePageSelectionClipboard",
    "createSequencerStructurePage",
    "ensurePageExists",
    "capturePageHistoryBefore",
    "recordPageHistoryAfter",
    "captureSequencerPageStructureHistoryBefore",
    "recordSequencerPageStructureHistoryChange",
    "commitStructureStepPastePlan",
    "resizeActiveContentForStepPaste",
    "writeRootStepFromClipboardEntry",
    "writeChildStepFromClipboardEntry",
    "resetActiveContentStep",
    "resetSelectedActiveContentSteps",
    "resetSelectedActiveContentPages",
    "deleteSelectedRootPages",
    "clearCurrentSequencerStructurePage",
    "deleteCurrentSequencerStructurePage",
    "SequencerPageStructureHistoryChangePtr",
    "sequencerHistoryPageCount",
    "makeSequencerPageStructureHistoryDescriptor",
)
RETIRED_RAW_TRACK_SYMBOLS = (
    "createSequencerStructureTrack",
    "rollbackTrackCreation",
)
PAGE_STRUCTURE_ACTIONS = (
    "Invalid",
    "PageSelectionPaste",
    "PageClear",
    "PageDelete",
    "PagePaste",
    "StepPaste",
    "FocusedStepReset",
    "StepSelectionReset",
    "PageSelectionReset",
    "PageSelectionDeleteOrDeepReset",
)
LEASED_TEST_VERSIONED_PAGE_WRITERS = (
    "clearStepRange",
    "appendPage",
    "insertPage",
    "deletePage",
)
LEASED_TEST_VERSIONED_PAGE_WRITER_OWNER_FILES = frozenset(
    (
        "src/state/sequencer/SequencerSnapshotOps.cpp",
        "src/state/sequencer/SequencerSnapshotOps.hpp",
    )
)
PAGE_STRUCTURE_GRAPH_RESULT_FUNCTIONS = (
    "initializeSequencerGraphRootUnversioned",
    "extendMicroSequencePreservingLogicalContentUnversioned",
    "extendCycleStateSetPreservingLogicalContentUnversioned",
    "resetStepNodePayloadUnversioned",
    "resetStepNodePayloadPreservingChildrenUnversioned",
    "copyStepNodePayloadFromGraphUnversioned",
    "resizeSequencerRootContentUnversioned",
    "deleteSequencerRootPagesUnversioned",
)

STEP_DRAFT_HISTORY_GUARD = re.compile(
    r"\bstepContentDraft\.rejectTransitionIfActive\s*\(\s*"
    r"(?:[A-Za-z_][A-Za-z0-9_]*::)*"
    r"SequencerStepContentDraftBlockedTransition::HISTORY\s*\)",
    flags=re.DOTALL,
)

STEP_DRAFT_STRUCTURE_GUARD = re.compile(
    r"\bstepContentDraft\.rejectTransitionIfActive\s*\(\s*"
    r"(?:[A-Za-z_][A-Za-z0-9_]*::)*"
    r"SequencerStepContentDraftBlockedTransition::STRUCTURE_EDIT\s*\)",
    flags=re.DOTALL,
)


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


def cold_placement_contract_errors(cold_placement: str) -> list[str]:
    errors: list[str] = []
    for selector in COLD_PLACEMENT_CONTRACT_SELECTORS:
        if selector not in cold_placement:
            errors.append(
                "script/pio/imxrt1062_t41_cold_placement.ld: "
                f"missing contracted cold-placement selector {selector}"
            )
    return errors


@functools.lru_cache(maxsize=512)
def cpp_code_mask(content: str) -> str:
    """Blank comments and quoted literals while preserving source offsets."""
    masked = list(content)
    state = "code"
    quote = ""
    index = 0
    while index < len(content):
        char = content[index]
        following = content[index + 1] if index + 1 < len(content) else ""

        if state == "code":
            if char == "/" and following == "/":
                masked[index] = masked[index + 1] = " "
                state = "line-comment"
                index += 2
                continue
            if char == "/" and following == "*":
                masked[index] = masked[index + 1] = " "
                state = "block-comment"
                index += 2
                continue
            if char in ('"', "'"):
                masked[index] = " "
                quote = char
                state = "quoted"
                index += 1
                continue
        elif state == "line-comment":
            if char == "\n":
                state = "code"
            else:
                masked[index] = " "
            index += 1
            continue
        elif state == "block-comment":
            masked[index] = " "
            if char == "*" and following == "/":
                masked[index + 1] = " "
                state = "code"
                index += 2
                continue
            index += 1
            continue
        else:
            masked[index] = " "
            if char == "\\" and following:
                masked[index + 1] = " "
                index += 2
                continue
            if char == quote:
                state = "code"
            index += 1
            continue

        index += 1
    return "".join(masked)


@functools.lru_cache(maxsize=65536)
def regex_count_dotall(pattern: str, content: str) -> int:
    return len(re.findall(pattern, content, flags=re.DOTALL))


def matching_cpp_delimiter(
    masked_content: str,
    opening_index: int,
    opening: str,
    closing: str,
) -> int | None:
    depth = 0
    for index in range(opening_index, len(masked_content)):
        char = masked_content[index]
        if char == opening:
            depth += 1
        elif char == closing:
            depth -= 1
            if depth == 0:
                return index
    return None


@functools.lru_cache(maxsize=8192)
def cpp_function_bodies(content: str, qualified_name: str) -> list[str]:
    """Return balanced, comment/literal-masked bodies for qualified definitions."""
    masked = cpp_code_mask(content)
    bodies: list[str] = []
    signature = re.compile(rf"\b{re.escape(qualified_name)}\s*\(")
    for match in signature.finditer(masked):
        opening_parenthesis = masked.find("(", match.start())
        closing_parenthesis = matching_cpp_delimiter(
            masked, opening_parenthesis, "(", ")"
        )
        if closing_parenthesis is None:
            continue

        terminator = re.search(r"[;{]", masked[closing_parenthesis + 1 :])
        if terminator is None:
            continue
        terminator_index = closing_parenthesis + 1 + terminator.start()
        if masked[terminator_index] == ";":
            continue

        closing_brace = matching_cpp_delimiter(masked, terminator_index, "{", "}")
        if closing_brace is None:
            continue
        bodies.append(masked[terminator_index + 1 : closing_brace])
    return bodies


@functools.lru_cache(maxsize=2048)
def cpp_type_bodies(content: str, type_name: str) -> list[str]:
    """Return balanced, comment/literal-masked class or struct bodies."""
    masked = cpp_code_mask(content)
    bodies: list[str] = []
    signature = re.compile(
        rf"\b(?:class|struct)\s+{re.escape(type_name)}\b"
    )
    for match in signature.finditer(masked):
        opening = masked.find("{", match.end())
        declaration_end = masked.find(";", match.end())
        if opening < 0 or (0 <= declaration_end < opening):
            continue
        closing = matching_cpp_delimiter(masked, opening, "{", "}")
        if closing is not None:
            bodies.append(masked[opening + 1 : closing])
    return bodies


def extmem_lifetime_contract_errors(files: dict[str, str]) -> list[str]:
    """Prove strict PSRAM allocation, ownership, and matching release paths."""
    errors: list[str] = []
    allocator = files.get(EXTMEM_ALLOCATOR_SOURCE, "")
    core_state = files.get(CORE_STATE_SOURCE, "")
    core_header_code = cpp_code_mask(files.get(CORE_STATE_HEADER, ""))
    allocator_code = cpp_code_mask(allocator)
    core_state_code = cpp_code_mask(core_state)

    def single_function_body(
        content: str,
        rel: str,
        qualified_name: str,
    ) -> str | None:
        bodies = cpp_function_bodies(content, qualified_name)
        if len(bodies) != 1:
            errors.append(
                f"{rel}: {qualified_name} must have exactly one balanced "
                f"definition (found {len(bodies)})"
            )
            return None
        return bodies[0]

    def require_once(
        body: str | None,
        rel: str,
        qualified_name: str,
        pattern: str,
        description: str,
    ) -> None:
        if body is None:
            return
        found = len(re.findall(pattern, body, flags=re.DOTALL))
        if found != 1:
            errors.append(
                f"{rel}: {description} in {qualified_name} "
                f"(expected 1, found {found})"
            )

    strict_allocate = single_function_body(
        allocator,
        EXTMEM_ALLOCATOR_SOURCE,
        "allocateExtmemStrict",
    )
    strict_free = single_function_body(
        allocator,
        EXTMEM_ALLOCATOR_SOURCE,
        "freeExtmemStrict",
    )

    require_once(
        strict_allocate,
        EXTMEM_ALLOCATOR_SOURCE,
        "allocateExtmemStrict",
        r"\breturn\s+sm_malloc_pool\s*\(\s*&\s*extmem_smalloc_pool\s*,"
        r"\s*bytes\s*\)\s*;",
        "strict allocation must return the canonical PSRAM pool sink",
    )
    require_once(
        strict_free,
        EXTMEM_ALLOCATOR_SOURCE,
        "freeExtmemStrict",
        r"\bsm_free_pool\s*\(\s*&\s*extmem_smalloc_pool\s*,"
        r"\s*ptr\s*\)\s*;",
        "strict free must use the matching canonical PSRAM pool sink",
    )

    for name, body in (
        ("allocateExtmemStrict", strict_allocate),
        ("freeExtmemStrict", strict_free),
    ):
        if body is None:
            continue
        found = len(DIRECT_SMALLOC_MUTATION_CALL.findall(body))
        if found != 1:
            errors.append(
                f"{EXTMEM_ALLOCATOR_SOURCE}: {name} must contain exactly "
                f"one smalloc pool mutation (found {found})"
            )

    if strict_allocate is not None and re.search(
        r"\b(?:extmem_(?:malloc|calloc|realloc)|malloc|calloc|realloc)\s*\("
        r"|\b(?:operator\s+)?new\b",
        strict_allocate,
    ):
        errors.append(
            f"{EXTMEM_ALLOCATOR_SOURCE}: allocateExtmemStrict must not "
            "contain an internal-RAM fallback"
        )

    if strict_free is not None and re.search(
        r"\b(?:extmem_free|free)\s*\(|\bdelete\b",
        strict_free,
    ):
        errors.append(
            f"{EXTMEM_ALLOCATOR_SOURCE}: freeExtmemStrict must not "
            "contain a non-PSRAM fallback"
        )

    pool_mutation_count = len(
        DIRECT_SMALLOC_MUTATION_CALL.findall(allocator_code)
    )
    if pool_mutation_count != 2:
        errors.append(
            f"{EXTMEM_ALLOCATOR_SOURCE}: expected only the canonical "
            "smalloc allocation/free pair "
            f"(found {pool_mutation_count} pool mutations)"
        )

    if DIRECT_EXTMEM_CALL.search(allocator_code):
        errors.append(
            f"{EXTMEM_ALLOCATOR_SOURCE}: strict owners must not use "
            "Teensy's fallback-capable extmem allocator"
        )

    for helper in (
        "makeExtmemUnique",
        "makeExtmemUniqueCopy",
        "makeExtmemUniqueForOverwrite",
        "makeExtmemUniqueArrayForOverwrite",
    ):
        body = single_function_body(
            allocator,
            EXTMEM_ALLOCATOR_SOURCE,
            helper,
        )
        require_once(
            body,
            EXTMEM_ALLOCATOR_SOURCE,
            helper,
            r"\ballocateExtmemStrict\s*\(",
            "EXTMEM helper must allocate through allocateExtmemStrict",
        )
        require_once(
            body,
            EXTMEM_ALLOCATOR_SOURCE,
            helper,
            r"\bcore::diagnostics::trackExtmemAllocation\s*\(\s*memory\s*\)",
            "EXTMEM helper must track its product allocation",
        )

    for deleter_name in ("ExtmemDeleter", "ExtmemArrayDeleter"):
        type_bodies = cpp_type_bodies(allocator, deleter_name)
        if len(type_bodies) != 1:
            errors.append(
                f"{EXTMEM_ALLOCATOR_SOURCE}: {deleter_name} must have "
                f"exactly one balanced definition (found {len(type_bodies)})"
            )
            continue

        operator_bodies = cpp_function_bodies(
            type_bodies[0],
            "operator()",
        )
        if len(operator_bodies) != 1:
            errors.append(
                f"{EXTMEM_ALLOCATOR_SOURCE}: {deleter_name}::operator() "
                "must have exactly one balanced definition "
                f"(found {len(operator_bodies)})"
            )
            continue

        body = operator_bodies[0]
        require_once(
            body,
            EXTMEM_ALLOCATOR_SOURCE,
            f"{deleter_name}::operator()",
            r"\bfreeExtmemStrict\s*\(\s*ptr\s*\)",
            "EXTMEM deleter must pair allocation with freeExtmemStrict",
        )
        require_once(
            body,
            EXTMEM_ALLOCATOR_SOURCE,
            f"{deleter_name}::operator()",
            r"\bcore::diagnostics::trackExtmemFree\s*\(\s*ptr\s*\)",
            "EXTMEM deleter must track its product free",
        )

        if deleter_name == "ExtmemDeleter":
            require_once(
                body,
                EXTMEM_ALLOCATOR_SOURCE,
                f"{deleter_name}::operator()",
                r"\bptr\s*->\s*~T\s*\(\s*\)",
                "object deleter must destroy before releasing PSRAM",
            )

    alias_contracts = (
        (
            r"\busing\s+ExtmemUniquePtr\s*=\s*std::unique_ptr\s*<\s*T\s*,"
            r"\s*ExtmemDeleter\s*<\s*T\s*>\s*>\s*;",
            "ExtmemUniquePtr must retain ExtmemDeleter",
        ),
        (
            r"\busing\s+ExtmemUniqueArray\s*=\s*std::unique_ptr\s*<\s*T"
            r"\s*\[\s*\]\s*,\s*ExtmemArrayDeleter\s*<\s*T\s*>\s*>\s*;",
            "ExtmemUniqueArray must retain ExtmemArrayDeleter",
        ),
    )
    for pattern, description in alias_contracts:
        found = len(re.findall(pattern, allocator_code, flags=re.DOTALL))
        if found != 1:
            errors.append(
                f"{EXTMEM_ALLOCATOR_SOURCE}: {description} "
                f"(expected 1, found {found})"
            )

    for symbol, expected in (
        ("allocateExtmemStrict", 5),
        ("freeExtmemStrict", 3),
    ):
        found = len(re.findall(
            rf"\b{symbol}\s*\(",
            allocator_code,
        ))
        if found != expected:
            errors.append(
                f"{EXTMEM_ALLOCATOR_SOURCE}: canonical {symbol} inventory "
                f"changed (expected {expected}, found {found})"
            )

    create_pending = single_function_body(
        core_state,
        CORE_STATE_SOURCE,
        "createPendingApply",
    )
    pending_deleter = single_function_body(
        core_state,
        CORE_STATE_SOURCE,
        "SequencerDomainState::PendingApplyDeleter::operator()",
    )
    core_constructor = single_function_body(
        core_state,
        CORE_STATE_SOURCE,
        "CoreState::CoreState",
    )

    require_once(
        create_pending,
        CORE_STATE_SOURCE,
        "createPendingApply",
        r"\bcore::app::allocateExtmemStrict\s*\(\s*sizeof\s*\(\s*"
        r"SequencerDomainState::PendingApply\s*\)\s*\)",
        "PendingApply must use strict PSRAM allocation",
    )
    require_once(
        create_pending,
        CORE_STATE_SOURCE,
        "createPendingApply",
        r"\bif\s*\(\s*!\s*memory\s*\)\s*return\s+nullptr\s*;",
        "PendingApply must fail closed on PSRAM exhaustion",
    )
    require_once(
        create_pending,
        CORE_STATE_SOURCE,
        "createPendingApply",
        r"\bcore::diagnostics::trackExtmemAllocation\s*\(\s*memory\s*\)",
        "PendingApply must track its allocation",
    )
    require_once(
        create_pending,
        CORE_STATE_SOURCE,
        "createPendingApply",
        r"\bnew\s*\(\s*memory\s*\)\s*"
        r"SequencerDomainState::PendingApply\s*\(\s*\)",
        "PendingApply must be constructed in the strict allocation",
    )

    require_once(
        pending_deleter,
        CORE_STATE_SOURCE,
        "SequencerDomainState::PendingApplyDeleter::operator()",
        r"\bptr\s*->\s*~PendingApply\s*\(\s*\)",
        "PendingApply must be destroyed before release",
    )
    require_once(
        pending_deleter,
        CORE_STATE_SOURCE,
        "SequencerDomainState::PendingApplyDeleter::operator()",
        r"\bcore::diagnostics::trackExtmemFree\s*\(\s*ptr\s*\)",
        "PendingApply must track its free",
    )
    require_once(
        pending_deleter,
        CORE_STATE_SOURCE,
        "SequencerDomainState::PendingApplyDeleter::operator()",
        r"\bcore::app::freeExtmemStrict\s*\(\s*ptr\s*\)",
        "PendingApply must use strict PSRAM release",
    )
    require_once(
        core_constructor,
        CORE_STATE_SOURCE,
        "CoreState::CoreState",
        r"\bsequencerDomain_\.pendingApply\.reset\s*\(\s*"
        r"createPendingApply\s*\(\s*\)\s*\)",
        "CoreState must adopt PendingApply into its custom-deleter owner",
    )

    pending_header_contracts = (
        (
            r"\busing\s+PendingApplyPtr\s*=\s*std::unique_ptr\s*<\s*"
            r"PendingApply\s*,\s*PendingApplyDeleter\s*>\s*;",
            "PendingApplyPtr must retain PendingApplyDeleter",
        ),
        (
            r"\bPendingApplyPtr\s+pendingApply\s*;",
            "pendingApply must retain its custom owner type",
        ),
    )
    for pattern, description in pending_header_contracts:
        found = len(re.findall(pattern, core_header_code, flags=re.DOTALL))
        if found != 1:
            errors.append(
                f"{CORE_STATE_HEADER}: {description} "
                f"(expected 1, found {found})"
            )

    for symbol, expected in (
        ("allocateExtmemStrict", 1),
        ("freeExtmemStrict", 1),
    ):
        found = len(re.findall(
            rf"\bcore::app::{symbol}\s*\(",
            core_state_code,
        ))
        if found != expected:
            errors.append(
                f"{CORE_STATE_SOURCE}: PendingApply {symbol} inventory "
                f"changed (expected {expected}, found {found})"
            )

    for rel, content in files.items():
        if rel in STRICT_EXTMEM_OWNER_SOURCES:
            continue
        if STRICT_EXTMEM_CALL.search(cpp_code_mask(content)):
            errors.append(
                f"{rel}: strict EXTMEM primitive escaped its canonical owners"
            )

    return errors


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


def step_draft_transition_contract_errors(files: dict[str, str]) -> list[str]:
    errors: list[str] = []
    code_mask_cache: dict[str, str] = {}

    def masked_file(rel: str) -> str:
        if rel not in code_mask_cache:
            code_mask_cache[rel] = cpp_code_mask(files.get(rel, ""))
        return code_mask_cache[rel]

    def require(rel: str, pattern: str, description: str, count: int = 1) -> None:
        found = regex_count_dotall(pattern, files.get(rel, ""))
        if found != count:
            errors.append(f"{rel}: {description} (expected {count}, found {found})")

    def require_across_files(
        pattern: str, description: str, count: int
    ) -> None:
        found = sum(
            regex_count_dotall(pattern, masked_file(rel))
            for rel in files
        )
        if found != count:
            errors.append(
                f"source set: {description} (expected {count}, found {found})"
            )

    function_body_cache: dict[tuple[str, str], list[str]] = {}

    def function_bodies(rel: str, qualified_name: str) -> list[str]:
        key = (rel, qualified_name)
        if key not in function_body_cache:
            function_body_cache[key] = cpp_function_bodies(
                files.get(rel, ""), qualified_name
            )
        return function_body_cache[key]

    def require_in_function(
        rel: str,
        qualified_name: str,
        pattern: str,
        description: str,
        count: int = 1,
    ) -> None:
        bodies = function_bodies(rel, qualified_name)
        if len(bodies) != 1:
            errors.append(
                f"{rel}: {qualified_name} must have exactly one balanced definition "
                f"(found {len(bodies)})"
            )
            return
        found = regex_count_dotall(pattern, bodies[0])
        if found != count:
            errors.append(
                f"{rel}: {description} in {qualified_name} "
                f"(expected {count}, found {found})"
            )

    type_body_cache: dict[tuple[str, str], list[str]] = {}

    def type_bodies(rel: str, type_name: str) -> list[str]:
        key = (rel, type_name)
        if key not in type_body_cache:
            type_body_cache[key] = cpp_type_bodies(
                files.get(rel, ""), type_name
            )
        return type_body_cache[key]

    def require_in_type(
        rel: str,
        type_name: str,
        pattern: str,
        description: str,
        count: int = 1,
    ) -> None:
        bodies = type_bodies(rel, type_name)
        if len(bodies) != 1:
            errors.append(
                f"{rel}: {type_name} must have exactly one balanced definition "
                f"(found {len(bodies)})"
            )
            return
        found = regex_count_dotall(pattern, bodies[0])
        if found != count:
            errors.append(
                f"{rel}: {description} in {type_name} "
                f"(expected {count}, found {found})"
            )

    session = "src/state/sequencer/SequencerStepContentDraftSession.hpp"
    require(
        session,
        r"enum\s+class\s+SequencerStepContentDraftBlockedTransition\s*:\s*"
        r"uint8_t\s*\{[^}]*\bRESET\s*,\s*STRUCTURE_EDIT\s*,\s*HISTORY\s*,",
        "STRUCTURE_EDIT and HISTORY must remain appended after RESET",
    )

    frozen_labels = (
        ("STANDALONE_NONE_LABEL", "APPLY OR DISCARD DRAFT"),
        ("STANDALONE_TRACK_LABEL", "APPLY BEFORE CHANGING TRACK"),
        ("STANDALONE_VIEW_LABEL", "APPLY BEFORE CHANGING VIEW"),
        ("STANDALONE_PROJECT_LOAD_LABEL", "APPLY BEFORE LOADING"),
        ("STANDALONE_RESET_LABEL", "APPLY BEFORE RESET"),
        ("STANDALONE_STRUCTURE_EDIT_LABEL", "APPLY BEFORE STRUCTURE EDIT"),
        ("STANDALONE_HISTORY_LABEL", "APPLY BEFORE UNDO/REDO"),
        ("PROPERTY_NONE_LABEL", "Apply or discard"),
        ("PROPERTY_TRACK_LABEL", "Apply before track"),
        ("PROPERTY_VIEW_LABEL", "Apply before view"),
        ("PROPERTY_PROJECT_LOAD_LABEL", "Apply before load"),
        ("PROPERTY_RESET_LABEL", "Apply before reset"),
        ("PROPERTY_STRUCTURE_EDIT_LABEL", "Apply before structure edit"),
        ("PROPERTY_HISTORY_LABEL", "Apply before undo/redo"),
    )
    for symbol, label in frozen_labels:
        require(
            STEP_DRAFT_LABELS_SOURCE,
            rf"\bconst\s+char\s+{symbol}\s*\[\s*\]\s*"
            rf"PROGMEM\s*=\s*\"{re.escape(label)}\"\s*;",
            f"{symbol} must retain cold label {label!r}",
        )

    table_contracts = (
        (
            "STANDALONE_LABELS",
            tuple(symbol for symbol, _ in frozen_labels[:7]),
            "standaloneStepContentDraftTransitionLabel",
        ),
        (
            "PROPERTY_LABELS",
            tuple(symbol for symbol, _ in frozen_labels[7:]),
            "propertyOverlayStepContentDraftTransitionLabel",
        ),
    )
    for table, symbols, helper in table_contracts:
        ordered_symbols = r"\s*,\s*".join(map(re.escape, symbols))
        require(
            STEP_DRAFT_LABELS_SOURCE,
            rf"\b{table}\s+PROGMEM\s*\{{\s*{ordered_symbols}\s*,?\s*\}}\s*;",
            f"{table} must retain its exact cold transition order",
        )
        require(
            STEP_DRAFT_LABELS_SOURCE,
            rf"\b{helper}\s*\([^)]*\)\s*\{{\s*return\s+"
            rf"transitionLabel\s*\(\s*{table}\s*,\s*transition\s*\)\s*;\s*\}}",
            f"{helper} must delegate to the single {table} Flash table",
        )

    shared_include = re.escape(
        '#include "ui/sequencer/SequencerStepContentDraftTransitionLabels.hpp"'
    )
    formatter_wiring = (
        (
            "src/context/standalone/SequencerOverlayPresenterFormatters.cpp",
            "draftFailureLabel",
            "standaloneStepContentDraftTransitionLabel",
        ),
        (
            "src/ui/sequencer/SequencerPropertyOverlayViewModelBuilder.cpp",
            "stepContentDraftFailureValue",
            "propertyOverlayStepContentDraftTransitionLabel",
        ),
    )
    for rel, formatter, helper in formatter_wiring:
        require(rel, shared_include, "shared Step-draft label include must remain")
        require(
            rel,
            rf"\b{formatter}\s*\([^)]*\)\s*\{{.*?"
            r"\bcase\s+Failure::TRANSITION_BLOCKED\s*:\s*return\s+"
            rf"(?:core::ui::sequencer::)?{helper}\s*\(\s*"
            r"draft\.blockedTransition\s*\)\s*;",
            f"{formatter} must delegate TRANSITION_BLOCKED to {helper}",
        )

    history_guards = (
        ("src/state/CoreStateProjectHistory.cpp", "CoreState::undoProjectHistory",
         "sequencer"),
        ("src/state/CoreStateProjectHistory.cpp", "CoreState::redoProjectHistory",
         "sequencer"),
        ("src/state/CoreStateSequencerHistoryTraversal.cpp",
         "CoreState::traverseSequencerHistory_", "sequencer"),
        (STEP_DRAFT_HISTORY, "applyEntrySnapshot", "active"),
    )
    for rel, function, receiver in history_guards:
        require(
            rel,
            rf"\b{re.escape(function)}\s*\([^)]*\)\s*\{{\s*if\s*\(\s*"
            rf"{receiver}\.stepContentDraft\.rejectTransitionIfActive\s*\(\s*"
            r"(?:[A-Za-z_][A-Za-z0-9_]*::)*"
            r"SequencerStepContentDraftBlockedTransition::HISTORY\s*\)\s*\)\s*"
            r"\{\s*return\s+false\s*;\s*\}",
            f"{function} must start with its HISTORY guard",
        )

    history_count = sum(
        len(STEP_DRAFT_HISTORY_GUARD.findall(content))
        for rel, content in files.items()
        if rel.startswith("src/")
    )
    if history_count != 4:
        errors.append(
            f"src: expected exactly four production HISTORY guards, found {history_count}"
        )

    require(
        PAGE_STRUCTURE_TRANSACTION,
        r"\bSequencerPreparedPageStructureTransaction::openBoundary\s*\(\s*\)\s*"
        r"\{.*?stepContentDraft\.rejectTransitionIfActive\s*\(\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*"
        r"SequencerStepContentDraftBlockedTransition::STRUCTURE_EDIT\s*\)\s*\)\s*"
        r"\{\s*return\s+false\s*;\s*\}.*?"
        r"history_\.commitCoalescedPatternEditOutcome\s*\(\s*\)",
        "openBoundary must reject STRUCTURE_EDIT before its Pattern boundary",
    )
    structure_count = sum(
        len(STEP_DRAFT_STRUCTURE_GUARD.findall(content))
        for rel, content in files.items()
        if rel.startswith("src/")
    )
    if structure_count != 1:
        errors.append(
            "src: expected exactly one production STRUCTURE_EDIT guard, "
            f"found {structure_count}"
        )
    require(
        PAGE_STRUCTURE_TRANSACTION,
        r"\bhistory_\.commitCoalescedPatternEditOutcome\s*\(\s*\)",
        "Page transaction must own exactly one Pattern boundary",
    )
    require(
        PAGE_STRUCTURE_TRANSACTION,
        r"\bSequencerPreparedPageStructureTransaction::execute\s*\([^)]*\)\s*"
        r"\{.*?\bbegin\s*\(\s*execution\s*\).*?"
        r"execution\.revalidate\s*\(.*?"
        r"history_\.preparedPatternEditReady\s*\(.*?"
        r"execution\.mutate\s*\(.*?"
        r"sealAndCommit\s*\(",
        "Page execute must own begin -> plan revalidation -> Core readiness -> mutation -> seal",
    )
    page_source = files.get(PAGE_STRUCTURE_TRANSACTION, "")
    for call, expected in (
        ("execution.revalidate", 1),
        ("execution.mutate", 1),
        ("history_.preparedPatternEditReady", 1),
    ):
        observed = len(re.findall(re.escape(call) + r"\s*\(", page_source))
        if observed != expected:
            errors.append(
                f"{PAGE_STRUCTURE_TRANSACTION}: expected {expected} {call} expression, "
                f"found {observed}"
            )

    page_header = files.get(PAGE_STRUCTURE_TRANSACTION_HEADER, "")
    first_action, *remaining_actions = PAGE_STRUCTURE_ACTIONS
    exact_action_members = (
        rf"{re.escape(first_action)}\s*=\s*0\s*,\s*"
        + r"\s*,\s*".join(map(re.escape, remaining_actions))
    )
    require(
        PAGE_STRUCTURE_TRANSACTION_HEADER,
        r"\benum\s+class\s+SequencerPreparedPageStructureAction\s*:\s*"
        rf"uint8_t\s*\{{\s*{exact_action_members}\s*,?\s*\}}\s*;",
        "Page Structure action enum must contain one invalid sentinel and exactly nine frozen actions",
    )
    for retired_symbol in (
        "PageCreate",
        "buildSequencerPageCreateMutationPlan",
        "createPreviewedPageAfterBoundary",
        "createPreviewedStructure",
    ):
        observed = sum(
            len(re.findall(rf"\b{re.escape(retired_symbol)}\b", masked_file(rel)))
            for rel in files
            if rel.startswith("src/handler/sequencer/")
        )
        if observed != 0:
            errors.append(
                "src/handler/sequencer: retired Navigation Page-create symbol "
                f"{retired_symbol} restored (found {observed})"
            )
    sequencer_add_slot_signal_count = sum(
        len(re.findall(r"\bpreviewAddPageSlot\b", masked_file(rel)))
        for rel in files
        if rel.startswith("src/") and "sequencer" in rel.lower()
    )
    if sequencer_add_slot_signal_count != 0:
        errors.append(
            "src: retired Sequencer previewAddPageSlot signal restored "
            f"(found {sequencer_add_slot_signal_count})"
        )
    for retired_selector_symbol in (
        "SequencerContextSelectorFeedback",
        "feedbackUntilMs",
        "UNAVAILABLE_FEEDBACK_MS",
        "EDITOR_UNAVAILABLE",
    ):
        observed = sum(
            len(re.findall(
                rf"\b{re.escape(retired_selector_symbol)}\b",
                masked_file(rel),
            ))
            for rel in files
            if rel.startswith("src/") and "sequencer" in rel.lower()
        )
        if observed != 0:
            errors.append(
                "src: retired unproducible Sequencer selector feedback symbol "
                f"{retired_selector_symbol} restored (found {observed})"
            )
    require(
        CONTEXT_SELECTOR_WORKFLOW_HEADER,
        r"\brotated\s*\(",
        "context selector must not restore its unused rotated accessor",
        count=0,
    )
    require(
        PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
        r"\btrackPasteDetailsVisible\s*\(",
        "Edit workflow must not restore its unused Track-paste detail accessor",
        count=0,
    )
    public_block = re.search(
        r"\bclass\s+SequencerPreparedPageStructureTransaction\b.*?"
        r"\bpublic\s*:(.*?)\bprivate\s*:",
        page_header,
        flags=re.DOTALL,
    )
    if public_block is None:
        errors.append(
            f"{PAGE_STRUCTURE_TRANSACTION_HEADER}: transaction public/private boundary missing"
        )
    elif re.search(r"\b(?:begin|abort|sealAndCommit)\s*\(", public_block.group(1)):
        errors.append(
            f"{PAGE_STRUCTURE_TRANSACTION_HEADER}: begin/abort/seal must remain private"
        )

    require(
        PAGE_STRUCTURE_TRANSACTION,
        r"\bfailPageStructureTransactionInvariant\s*\(\s*\)\s*\{.*?"
        r"\b__builtin_trap\s*\(\s*\)\s*;.*?\bstd::abort\s*\(\s*\)\s*;.*?\}",
        "Page invariant failure must remain release fail-stop without ARM runtime state",
    )
    require(
        PAGE_STRUCTURE_TRANSACTION,
        r"\bSequencerPreparedPageStructureTransaction::abortOwned\s*\(\s*\)\s*\{"
        r".*?\babortPreparedPatternEdit\s*\(.*?\)\s*;.*?\bif\s*\(\s*outcome\s*!="
        r"\s*seq::SequencerPreparedPatternEditAbortOutcome::Aborted\s*\)\s*\{\s*"
        r"failPageStructureTransactionInvariant\s*\(\s*\)\s*;\s*\}",
        "an armed abort must fail-stop unless Core proves Aborted",
    )
    require(
        PAGE_STRUCTURE_TRANSACTION,
        r"\bcase\s+SealOutcome::FailedClosed\s*:\s*phase_\s*=\s*Phase::Closed\s*;"
        r"\s*return\s+Result::Failed\s*;.*?"
        r"\bcase\s+SealOutcome::Failed\s*:\s*abortOwned\s*\(\s*\)\s*;"
        r"\s*return\s+Result::Failed\s*;.*?"
        r"\bdefault\s*:\s*failPageStructureTransactionInvariant\s*\(\s*\)\s*;",
        "seal settlement must distinguish closed failure and fail-stop invalid outcomes",
    )
    require(
        PAGE_STRUCTURE_TRANSACTION,
        r"\bcase\s+CommitOutcome::Failed\s*:\s*abortOwned\s*\(\s*\)\s*;"
        r"\s*return\s+Result::Failed\s*;\s*"
        r"\bcase\s+CommitOutcome::NoPending\s*:\s*"
        r"failPageStructureTransactionInvariant\s*\(\s*\)\s*;",
        "commit failure must abort while NoPending remains fatal",
    )

    for symbol in RETIRED_RAW_PAGE_SYMBOLS:
        observed = sum(
            len(re.findall(rf"\b{re.escape(symbol)}\b", content))
            for rel, content in files.items()
            if rel.startswith("src/")
        )
        if observed != 0:
            errors.append(
                f"src: retired raw Page mutation {symbol} must remain absent "
                f"(found {observed})"
            )

    for symbol in RETIRED_RAW_TRACK_SYMBOLS:
        observed = sum(
            len(re.findall(rf"\b{re.escape(symbol)}\b", content))
            for rel, content in files.items()
            if rel.startswith("src/")
        )
        if observed != 0:
            errors.append(
                f"src: retired raw Track Structure symbol {symbol} "
                f"must remain absent (found {observed})"
            )

    for symbol in LEASED_TEST_VERSIONED_PAGE_WRITERS:
        callers = [
            rel
            for rel, content in files.items()
            if rel.startswith("src/")
            and rel not in LEASED_TEST_VERSIONED_PAGE_WRITER_OWNER_FILES
            and (
                symbol != "deletePage"
                or rel.startswith("src/state/sequencer/")
                or rel.startswith("src/handler/sequencer/")
                or '"state/sequencer/SequencerSnapshotOps.hpp"' in content
            )
            and symbol in content
            and re.search(
                rf"(?<![.>])\b{re.escape(symbol)}\s*\(",
                masked_file(rel),
            )
        ]
        if callers:
            errors.append(
                f"src: test-leased versioned Page writer {symbol} escaped SnapshotOps "
                f"into {', '.join(sorted(callers))}"
            )

    require(
        PAGE_STRUCTURE_MUTATION_PLAN,
        r"\bcompactSequencerGraph\s*\(",
        "prepared Page mutation must not call allocating compactSequencerGraph",
        count=0,
    )
    page_plan_code = masked_file(PAGE_STRUCTURE_MUTATION_PLAN)
    ignored_graph_result = re.compile(
        r"(?m)^[ \t]*(?:\(\s*void\s*\)\s*|"
        r"static_cast\s*<\s*void\s*>\s*\(\s*)?"
        r"(?:(?:[A-Za-z_][A-Za-z0-9_]*)::)*"
        rf"(?:{'|'.join(map(re.escape, PAGE_STRUCTURE_GRAPH_RESULT_FUNCTIONS))})"
        r"\s*\("
    )
    ignored_graph_calls = ignored_graph_result.findall(page_plan_code)
    if ignored_graph_calls:
        errors.append(
            f"{PAGE_STRUCTURE_MUTATION_PLAN}: prepared Graph mutation result "
            f"must remain checked (found {len(ignored_graph_calls)} ignored call(s))"
        )

    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::pasteStructureSelection",
        r"\bAction::PageSelectionPaste\b.*?"
        r"SequencerPreparedPageStructureTransaction\s+transaction\s*"
        r"\([^;]*\baction\s*\)\s*;.*?"
        r"transaction\.openBoundary\s*\(\s*\).*?"
        r"pastePageSelectionAfterBoundary\s*"
        r"\(\s*transaction(?:\s*,\s*target)?\s*\)",
        "Page-selection paste must route boundary ownership to PageSelectionPaste",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::pastePageSelectionAfterBoundary",
        r"\bbuildSequencerPageSelectionPasteMutationPlan\s*\(",
        "PageSelectionPaste must use its prepared builder after the boundary",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::pasteCurrentStructure",
        r"\bAction::PagePaste\b.*?"
        r"SequencerPreparedPageStructureTransaction\s+transaction\s*"
        r"\([^;]*\baction\s*\)\s*;.*?"
        r"transaction\.openBoundary\s*\(\s*\).*?"
        r"pasteCurrentPageAfterBoundary\s*"
        r"\(\s*transaction(?:\s*,\s*target)?\s*\)",
        "current Page paste must route boundary ownership to PagePaste",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::pasteCurrentPageAfterBoundary",
        r"\bbuildSequencerPagePasteMutationPlan\s*\(",
        "PagePaste must use its prepared builder after the boundary",
    )

    prepared_structure_routes = (
        (
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "SequencerStructureEditWorkflow::applyCurrentStructureShortPress",
            "PageClear",
            "clearCurrentPageAfterBoundary",
        ),
        (
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "SequencerStructureEditWorkflow::applyCurrentStructureLongPress",
            "PageDelete",
            "deleteCurrentPageAfterBoundary",
        ),
        (
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "SequencerStructureEditWorkflow::pasteStepClipboardAt",
            "StepPaste",
            "pasteStepClipboardAfterBoundary",
        ),
        (
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "SequencerStructureEditWorkflow::resetFocusedStep",
            "FocusedStepReset",
            "resetFocusedStepAfterBoundary",
        ),
        (
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "SequencerStructureEditWorkflow::resetStepSelection",
            "StepSelectionReset",
            "resetStepSelectionAfterBoundary",
        ),
        (
            PAGE_STRUCTURE_SELECTION_WORKFLOW,
            "SequencerStructureEditWorkflow::applySelectionBottomLeftTap",
            "PageSelectionReset",
            "resetPageSelectionAfterBoundary",
        ),
        (
            PAGE_STRUCTURE_SELECTION_WORKFLOW,
            "SequencerStructureEditWorkflow::applySelectionBottomLeftHold",
            "PageSelectionDeleteOrDeepReset",
            "deleteOrResetPageSelectionAfterBoundary",
        ),
    )
    for rel, function, action, helper in prepared_structure_routes:
        require_in_function(
            rel,
            function,
            rf"\bAction::{action}\b.*?"
            r"SequencerPreparedPageStructureTransaction\s+transaction\s*"
            r"\([^;]*\baction\s*\)\s*;.*?"
            r"transaction\.openBoundary\s*\(\s*\).*?"
            rf"\b{helper}\s*\(\s*transaction(?:\s*,[^)]*)?\)",
            f"{action} must route one boundary to {helper}",
        )

    prepared_structure_helpers = (
        (
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "SequencerStructureEditWorkflow::clearCurrentPageAfterBoundary",
            "buildSequencerPageClearMutationPlan",
        ),
        (
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "SequencerStructureEditWorkflow::deleteCurrentPageAfterBoundary",
            "buildSequencerPageDeleteMutationPlan",
        ),
        (
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "SequencerStructureEditWorkflow::pasteStepClipboardAfterBoundary",
            "buildSequencerStepPasteMutationPlan",
        ),
        (
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "SequencerStructureEditWorkflow::resetFocusedStepAfterBoundary",
            "buildSequencerFocusedStepResetMutationPlan",
        ),
        (
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "SequencerStructureEditWorkflow::resetStepSelectionAfterBoundary",
            "buildSequencerStepSelectionResetMutationPlan",
        ),
        (
            PAGE_STRUCTURE_SELECTION_WORKFLOW,
            "SequencerStructureEditWorkflow::resetPageSelectionAfterBoundary",
            "buildSequencerPageSelectionResetMutationPlan",
        ),
        (
            PAGE_STRUCTURE_SELECTION_WORKFLOW,
            "SequencerStructureEditWorkflow::deleteOrResetPageSelectionAfterBoundary",
            "buildSequencerPageSelectionDeleteOrDeepResetMutationPlan",
        ),
    )
    for rel, function, builder in prepared_structure_helpers:
        require_in_function(
            rel,
            function,
            rf"\b{builder}\s*\(",
            f"{function} must use {builder}",
        )
        require_in_function(
            rel,
            function,
            r"\bexecuteSequencerPreparedPageStructureMutationPlan\s*\(",
            f"{function} must execute exactly one prepared plan",
        )

    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "executeDirect",
        r"\bexecuteSequencerTrackStructureTransaction\s*\(",
        "direct Track adapter must own exactly one common kernel call",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "executeSequencerCreateTrackStructure",
        r"\bexecuteDirect\s*\(\s*state\s*,\s*Action::SequencerCreate\s*,\s*"
        r"TrackBank::TRACK_COUNT\s*\)",
        "Create wrapper must route the frozen SequencerCreate action",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "executeSequencerRemoveCurrentTrackStructure",
        r"\bexecuteDirect\s*\(\s*state\s*,\s*"
        r"Action::SequencerRemoveCurrent\s*,\s*latchedTargetTrack\s*\)",
        "RemoveCurrent wrapper must route the frozen SequencerRemoveCurrent action",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "executeSequencerRemoveSelectionTrackStructure",
        r"\bexecuteDirect\s*\(\s*state\s*,\s*"
        r"Action::SequencerRemoveSelection\s*,\s*latchedActiveTrack\s*\)",
        "SelectionRemove wrapper must route the frozen SequencerRemoveSelection action",
    )
    require(
        DIRECT_TRACK_STRUCTURE_TRANSACTION_HEADER,
        r"\[\[nodiscard\]\]\s*SequencerPreparedTrackStructureResult\s*"
        r"executeSequencerRemoveSelectionTrackStructure\s*\(\s*"
        r"SequencerDirectTrackStructureStateRefs\s+state\s*,\s*"
        r"uint8_t\s+latchedActiveTrack\s*\)",
        "SelectionRemove direct adapter must remain a typed public cold route",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "executeDirect",
        r"^\s*if\s*\(\s*"
        r"state\.sequencer\.stepContentDraft\.rejectTransitionIfActive\s*\(\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::\s*)*"
        r"SequencerStepContentDraftBlockedTransition::TRACK\s*\)\s*\)",
        "direct Track adapter must give Draft rejection first executable priority",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "reconcileCommitted",
        r"\breconcilePreparedSequencerActiveTrackPresentation\s*\(\s*\)",
        "committed Track tail must reconcile Macro presentation exactly once",
    )
    require_in_function(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        "CoreState::publishSequencerHistoryTraversal_",
        r"else\s+if\s*\(\s*result\.descriptor\.kind\s*==\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*"
        r"SequencerHistoryActionKind::TrackStructure\s*&&\s*"
        r"activeTrackBefore\s*!=\s*"
        r"sequencerTracks\.activeTrackIndex\s*\(\s*\)\s*\)\s*\{\s*"
        r"reconcilePreparedSequencerActiveTrackPresentation\s*\(\s*\)",
        "pure Track Structure replay must reconcile presentation only on active-Track change",
    )
    require_in_function(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        "CoreState::traversePreparedSequencerStructureHistory_",
        r"const\s+uint8_t\s+activeTrackBefore\s*=\s*"
        r"sequencerTracks\.activeTrackIndex\s*\(\s*\)\s*;.*?"
        r"sequencerHistory\.commitPreparedStructureHistoryReplay\s*\(",
        "prepared Track replay must capture the active Track before its no-fail tail",
    )
    require_in_function(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        "CoreState::traverseSequencerHistory_",
        r"prepareStructureHistoryReplay\s*\(.*?"
        r"traversePreparedSequencerStructureHistory_\s*\(",
        "coupled Structure replay must prepare before dispatching its no-fail path",
    )
    require_in_function(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        "CoreState::armPreparedSequencerHistoryActivation_",
        r"planHistoryTransition\s*\(.*?"
        r"tryArmPlannedHistoryTransition\s*\(",
        "coupled activation must pure-plan before its atomic arm",
    )
    require_in_function(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        "CoreState::traversePreparedSequencerStructureHistory_",
        r"armPreparedSequencerHistoryActivation_\s*\(.*?"
        r"commitPreparedStructureHistoryReplay\s*\(.*?"
        r"commitHistoryTransition\s*\(.*?"
        r"publishSequencerHistoryTraversal_\s*\(",
        "coupled Structure replay must arm, commit and publish in order",
    )
    require(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        r"\bapplyMacroTrackStructureHistory\s*\(",
        "Core traversal must not use the legacy fallible Macro replay wrapper",
        count=0,
    )
    require_in_function(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        "CoreState::traverseGenericSequencerHistory_",
        r"sequencerHistory\.undoWithResult\s*\(",
        "generic Undo fallback must occur exactly once without compensation",
    )
    require_in_function(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        "CoreState::traverseGenericSequencerHistory_",
        r"sequencerHistory\.redoWithResult\s*\(",
        "generic Redo fallback must occur exactly once without compensation",
    )
    require_in_function(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        "CoreState::undoSequencerHistory",
        r"return\s+traverseSequencerHistory_\s*\(\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*SequencerHistoryDirection::Undo\s*\)",
        "Undo wrapper must delegate only to the shared direction coordinator",
    )
    require_in_function(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        "CoreState::redoSequencerHistory",
        r"return\s+traverseSequencerHistory_\s*\(\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*SequencerHistoryDirection::Redo\s*\)",
        "Redo wrapper must delegate only to the shared direction coordinator",
    )
    require_in_function(
        "src/state/sequencer/SequencerHistory.cpp",
        "SequencerHistoryService::prepareStructureHistoryReplay",
        r"validateMacroTrackStructureHistoryReplay\s*\(.*?"
        r"prepareHistoryStructureReplayOwners\s*\(",
        "Macro replay validation must precede every Structure owner allocation",
    )
    require_in_function(
        "src/state/sequencer/SequencerHistory.cpp",
        "SequencerHistoryService::commitPreparedStructureHistoryReplay",
        r"commitPreparedHistoryStructureReplayState\s*\(.*?"
        r"commitMacroTrackStructureHistoryReplay\s*\(.*?"
        r"popBack\s*\(.*?notifyApplied\s*\(",
        "no-fail tail must commit Sequencer, Macro and chronology in order",
    )
    require(
        "src/state/sequencer/SequencerHistory.cpp",
        r"\bapplyEntrySnapshot\s*\([^)]*\)\s*\{.*?"
        r"if\s*\(\s*entry\.scope\s*==\s*"
        r"SequencerHistoryScope::Structure\s*\)\s*return\s+false\s*;",
        "generic History application must reject Structure replay",
    )
    require(
        "src/state/sequencer/SequencerStructureHistory.hpp",
        r"static_assert\s*\(\s*sizeof\s*\(\s*"
        r"SequencerPreparedStructureHistoryReplay\s*\)\s*<=\s*256U",
        "prepared Structure replay handle must retain its 256-byte ARM lock",
    )
    require(
        "src/state/sequencer/SequencerStructureHistory.cpp",
        r"\bprepareHistoryStructureReplayOwners\s*\([^)]*\)\s*\{.*?"
        r"for\s*\([^)]*\)\s*\{.*?"
        r"cloneSnapshotGraph\s*\(\s*snapshot\.tracks\[i\]\s*,\s*"
        r"out\.bankGraphs\[i\]\s*\).*?"
        r"cloneSequencerCcLaneBank\s*\(\s*out\.bankCcLanes\[i\].*?"
        r"cloneSnapshotGraph\s*\(\s*snapshot\.tracks\[targetActive\]\s*,\s*"
        r"out\.editorGraph\s*\).*?"
        r"cloneSequencerCcLaneBank\s*\(\s*out\.editorCcLanes",
        "Structure replay allocation order must remain bank G,C ascending then editor G,C",
    )
    require_in_function(
        "src/state/sequencer/SequencerHistory.cpp",
        "SequencerHistoryService::commitPreparedStructureHistoryReplay",
        r"\b(?:makeExtmem|clone(?:SnapshotGraph|SequencerCcLaneBank)|"
        r"prepareHistoryStructureReplayOwners)\s*\(",
        "prepared Structure replay History tail must allocate zero owners",
        count=0,
    )

    selection_intent_fields = (
        ("clipboardRevision", r"uint32_t", r"selection\.clipboardRevision\.get\s*\(\s*\)"),
        ("selectedMask", r"uint16_t", r"selection\.selectedMask\.get\s*\(\s*\)"),
        ("destinationMask", r"uint16_t", r"selection\.destinationMask\.get\s*\(\s*\)"),
        ("overwriteMask", r"uint16_t", r"selection\.overwriteMask\.get\s*\(\s*\)"),
        ("cursor", r"uint8_t", r"selection\.cursorIndex\.get\s*\(\s*\)"),
    )
    for field, field_type, source in selection_intent_fields:
        require_in_type(
            DIRECT_TRACK_STRUCTURE_TRANSACTION,
            "SelectionIntentToken",
            rf"\b{field_type}\s+{field}\b",
            f"selection intent must retain {field}",
        )
        require_in_type(
            DIRECT_TRACK_STRUCTURE_TRANSACTION,
            "SelectionIntentToken",
            rf"\b{field}\s*==\s*other\.{field}\b",
            f"selection intent equality must include {field}",
        )
        require_in_function(
            DIRECT_TRACK_STRUCTURE_TRANSACTION,
            "captureSelectionIntent",
            rf"\.{field}\s*=\s*{source}",
            f"selection intent capture must include {field}",
        )

    step_selection_intent_fields = (
        (
            "selectedMask",
            r"oc::note::sequencer::StepBitMask128",
            r"selection\.selectedMask\.get\s*\(\s*\)",
        ),
        (
            "clipboardRevision",
            r"uint32_t",
            r"selection\.clipboardRevision\.get\s*\(\s*\)",
        ),
        (
            "cursor",
            r"uint8_t",
            r"selection\.cursorStep\.get\s*\(\s*\)",
        ),
    )
    for field, field_type, source in step_selection_intent_fields:
        require_in_type(
            DIRECT_TRACK_STRUCTURE_TRANSACTION,
            "StepSelectionIntentToken",
            rf"\b{field_type}\s+{field}\b",
            f"Step selection intent must retain {field}",
        )
        require_in_type(
            DIRECT_TRACK_STRUCTURE_TRANSACTION,
            "StepSelectionIntentToken",
            rf"\b{field}\s*==\s*other\.{field}\b",
            f"Step selection intent equality must include {field}",
        )
        require_in_function(
            DIRECT_TRACK_STRUCTURE_TRANSACTION,
            "captureStepSelectionIntent",
            rf"\.{field}\s*=\s*{source}",
            f"Step selection intent capture must include {field}",
        )

    clipboard_owner_fields = (
        ("macroAutomation", "macroAutomationSet"),
        ("macroModulationAssignment", "macroModulationAssignment"),
        ("sequencerGraph", "sequencerGraph"),
        ("sequencerCcLanes", "sequencerCcLanes"),
        ("sequencerTrackSelection", "sequencerTrackSelection"),
        ("macroPageSelection", "macroPageSelection"),
    )
    for field, source_field in clipboard_owner_fields:
        require_in_type(
            DIRECT_TRACK_STRUCTURE_TRANSACTION,
            "ClipboardIntentToken",
            rf"\bconst\s+void\s*\*\s*{field}\b",
            f"clipboard intent must retain {field} owner identity",
        )
        require_in_type(
            DIRECT_TRACK_STRUCTURE_TRANSACTION,
            "ClipboardIntentToken",
            rf"\b{field}\s*==\s*other\.{field}\b",
            f"clipboard intent equality must include {field}",
        )
        require_in_function(
            DIRECT_TRACK_STRUCTURE_TRANSACTION,
            "captureClipboardIntent",
            rf"\.{field}\s*=\s*clipboard\.{source_field}\s*\.get\s*\(\s*\)",
            f"clipboard intent capture must include {field}",
        )

    track_paste_intent_fields = (
        ("revision", r"uint32_t", r"paste\.revision\.get\s*\(\s*\)"),
        ("clipboardRevision", r"uint32_t", r"paste\.clipboardRevision"),
        ("interactionGeneration", r"uint32_t", r"paste\.interactionGeneration"),
        ("operationGeneration", r"uint32_t", r"paste\.operationGeneration"),
        ("activationGeneration", r"uint32_t", r"paste\.activationGeneration"),
        ("gestureActive", r"bool", r"paste\.gestureActive\s*\(\s*\)"),
        ("detailVisible", r"bool", r"paste\.detailVisible"),
        ("buttonOwned", r"bool", r"paste\.buttonOwned"),
        ("commitConsumed", r"bool", r"paste\.commitConsumed"),
    )
    for field, field_type, source in track_paste_intent_fields:
        require_in_type(
            DIRECT_TRACK_STRUCTURE_TRANSACTION,
            "TrackPasteIntentToken",
            rf"\b{field_type}\s+{field}\b",
            f"Track-paste intent must retain {field}",
        )
        require_in_type(
            DIRECT_TRACK_STRUCTURE_TRANSACTION,
            "TrackPasteIntentToken",
            rf"\b{field}\s*==\s*other\.{field}\b",
            f"Track-paste intent equality must include {field}",
        )
        require_in_function(
            DIRECT_TRACK_STRUCTURE_TRANSACTION,
            "captureTrackPasteIntent",
            rf"\.{field}\s*=\s*{source}",
            f"Track-paste intent capture must include {field}",
        )

    require_in_type(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "IntentToken",
        r"\buint8_t\s+activeTrack\b",
        "direct intent must retain the observed live active Track",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "captureIntent",
        r"token\.activeTrack\s*=\s*state\.sharedTracks\.activeTrack\s*\(\s*\)",
        "direct intent must capture the observed live active Track",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "sameIntent",
        r"lhs\.activeTrack\s*==\s*rhs\.activeTrack",
        "direct intent equality must include the observed live active Track",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "captureIntent",
        r"token\.targetTrack\s*=\s*action\s*==\s*Action::SequencerCreate\s*"
        r"\?\s*token\.previewTrack\s*:\s*latchedTargetTrack",
        "Remove intent must use the immutable dispatch target",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "FLASHMEM bool validIntent",
        r"token\.activeTrack\s*==\s*token\.targetTrack\s*&&\s*"
        r"token\.previewTrack\s*==\s*token\.targetTrack",
        "Remove intent must reject live active/preview drift from its latch",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "FLASHMEM bool validIntent",
        r"action\s*==\s*Action::SequencerRemoveSelection.*?"
        r"token\.trackSelection\.active\s*&&\s*"
        r"token\.trackSelection\.scope\s*==\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*StructureSelectionScope::TRACK\s*&&\s*"
        r"!\s*token\.trackSelection\.placing\s*&&\s*"
        r"!\s*token\.previewAddTrack\s*&&\s*"
        r"token\.activeTrack\s*==\s*token\.targetTrack",
        "SelectionRemove intent must retain Track scope, placement and active latch",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "FLASHMEM bool validIntent",
        r"\bcommitConsumed\b",
        "terminal consumed-paste state must not invalidate an unrelated Track action",
        count=0,
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "FLASHMEM InitialTopologyOutcome validateInitialTopology",
        r"action\s*==\s*Action::SequencerCreate.*?"
        r"enabledMask\s*&\s*targetBit\s*\)\s*==\s*0U\s*"
        r"\?\s*InitialTopologyOutcome::Ready\s*"
        r":\s*InitialTopologyOutcome::Invalid",
        "Create preflight must reject an already-enabled target",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "FLASHMEM InitialTopologyOutcome validateInitialTopology",
        r"action\s*==\s*Action::SequencerRemoveCurrent.*?"
        r"targetTrack\s*!=\s*activeTrack.*?"
        r"countEnabled\s*\(\s*enabledMask\s*,\s*TrackBank::TRACK_COUNT\s*\)"
        r"\s*>\s*1U\s*\?\s*InitialTopologyOutcome::Ready\s*"
        r":\s*InitialTopologyOutcome::Invalid",
        "Remove preflight must reject stale target and last-Track no-op",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "FLASHMEM InitialTopologyOutcome validateInitialTopology",
        r"action\s*==\s*Action::SequencerRemoveSelection.*?"
        r"targetTrack\s*!=\s*activeTrack.*?"
        r"activeTrackSelectionMask\s*\(\s*"
        r"context\.token\.trackSelection\.selectedMask\s*,\s*enabledMask\s*\).*?"
        r"deleteSelectedStructureTracks\s*\(\s*"
        r"enabledMask\s*,\s*selectedMask\s*,\s*activeTrack\s*\)\.changed\s*"
        r"\?\s*InitialTopologyOutcome::Ready\s*"
        r":\s*InitialTopologyOutcome::Invalid",
        "SelectionRemove preflight must sanitize selection and reject empty/all deletion",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "FLASHMEM PlanOutcome buildPlan",
        r"action\s*==\s*Action::SequencerRemoveSelection.*?"
        r"plan\.targetTrack\s*=\s*TrackBank::TRACK_COUNT\s*;.*?"
        r"plan\.afterEnabledMask\s*=\s*mutation\.nextMask\s*;.*?"
        r"plan\.afterActiveTrack\s*=\s*mutation\.nextActive\s*;.*?"
        r"plan\.affectedTrackMask\s*=\s*selectedMask\s*;.*?"
        r"plan\.capturedTrackMask\s*=\s*static_cast<uint16_t>\s*\(\s*"
        r"oldActiveBit\s*\|\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*slotBit\s*\(\s*mutation\.nextActive\s*\)\s*"
        r"\)\s*;.*?"
        r"SequencerActiveTrackIncomingOwnerPolicy::Preserve",
        "SelectionRemove plan must affect S while capturing only the old/new active pair",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "FLASHMEM PlanOutcome buildPlan",
        r"action\s*==\s*Action::SequencerRemoveSelection.*?"
        r"const\s+uint8_t\s+incomingLength\s*=\s*"
        r"mutation\.nextActive\s*==\s*beforeActive\s*\?\s*"
        r"context\.state\.sequencer\.pattern\.length\.get\s*\(\s*\)\s*:\s*"
        r"context\.state\.tracks\.track\s*\(\s*mutation\.nextActive\s*\)"
        r"\.length\.get\s*\(\s*\)\s*;.*?"
        r"fillActiveChangeFocus\s*\(\s*context\s*,\s*incomingLength\s*,\s*plan\s*\)",
        "SelectionRemove focus must use the active editor when the active Track survives",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "FLASHMEM PlanOutcome buildPlan",
        r"\bcanonicalResetTrackMask\s*=",
        "only Track Create may request canonical owner reset",
        count=1,
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "executeDirect",
        r"canReconcilePreparedSequencerActiveTrackPresentation\s*\(\s*\).*?"
        r"Status::PublicationUnavailable.*?DirectContext\s+context\s*\{.*?"
        r"executeSequencerTrackStructureTransaction\s*\(",
        "presentation capability must fail before intent capture, chronology and allocation",
    )
    require_in_type(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "SelectionIntentToken",
        r"static_assert",
        "selection intent compactness assertion must remain outside the token body",
        count=0,
    )
    require(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        r"static_assert\s*\(\s*sizeof\s*\(\s*SelectionIntentToken\s*\)\s*"
        r"==\s*16U",
        "direct selection intent must retain its 16-byte ABI lock",
    )
    require_in_type(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "DirectContext",
        r"^\s*const\s+SequencerDirectTrackStructureStateRefs\s*&\s*state\s*;\s*"
        r"IntentToken\s+token\s*;\s*$",
        "direct context must reference the synchronous adapter state instead of copying it",
    )
    require(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        r"sizeof\s*\(\s*void\s*\*\s*\)\s*!=\s*4U\s*\|\|\s*"
        r"sizeof\s*\(\s*DirectContext\s*\)\s*<=\s*160U",
        "direct context must retain its ARM stack ceiling",
    )
    require(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        r"sizeof\s*\(\s*void\s*\*\s*\)\s*!=\s*8U\s*\|\|\s*"
        r"sizeof\s*\(\s*DirectContext\s*\)\s*<=\s*168U",
        "direct context must retain its native stack ceiling",
    )
    require(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        r"FLASHMEM\s+Result\s+executeDirect\s*\(\s*const\s+"
        r"SequencerDirectTrackStructureStateRefs\s*&\s*state",
        "private direct execution must avoid a second StateRefs copy",
    )
    require(
        DIRECT_TRACK_STRUCTURE_TRANSACTION_HEADER,
        r"sizeof\s*\(\s*SequencerDirectTrackStructureStateRefs\s*\)\s*"
        r"==\s*64U",
        "direct Track StateRefs must retain their ARM stack ABI lock",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "executeDirect",
        r"validIntent\s*\(\s*context\s*,\s*action\s*\).*?"
        r"validateInitialTopology\s*\(\s*context\s*,\s*action\s*\).*?"
        r"executeSequencerTrackStructureTransaction\s*\(",
        "obvious intent/topology rejection must precede chronology",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "settleSuccessful",
        r"\bpreviewAddSlot\.set\s*\(\s*false\s*\)",
        "successful Track settlement must close the add preview exactly once",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "settleSuccessful",
        r"\bsyncPreviewTrack\s*\(\s*plan\.afterActiveTrack\s*\)",
        "successful Track settlement must publish the committed Track preview",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "settleSuccessful",
        r"\bsyncSequencerPagePreviewToVisible\s*\(",
        "successful Track settlement must reconcile the visible Page preview",
    )
    require_in_function(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "settleSuccessful",
        r"if\s*\(\s*plan\.action\s*==\s*Action::SequencerRemoveSelection\s*\)\s*\{\s*"
        r"context\.state\.trackNavigation\.selection\.reset\s*\(\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*StructureSelectionScope::TRACK\s*,\s*"
        r"plan\.afterActiveTrack\s*\)\s*;\s*"
        r"context\.state\.navigationFocus\.set\s*\(\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*StructureNavigationFocus::TRACK\s*\)\s*;\s*\}",
        "SelectionRemove UI reset must remain success-only inside direct settlement",
    )
    require(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        r"\b(?:recordStructure|recordPreparedStructure|captureTrackHistoryBefore|"
        r"recordTrackHistoryAfter|applyTrackState)\s*\(",
        "direct Track adapter must not restore raw capture/record/apply paths",
        count=0,
    )

    require_in_function(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "executePrepared",
        r"\bexecuteSequencerTrackStructureTransaction\s*\(",
        "direct Macro Track adapter must own exactly one common kernel call",
    )
    require(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        r"__attribute__\s*\(\(\s*noinline\s*\)\).*?"
        r"__declspec\s*\(\s*noinline\s*\).*?"
        r"FLASHMEM\s+Result\s+executePrepared\s*\(",
        "Macro kernel dispatch must retain its portable ARM stack split",
    )
    require_in_function(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "executeDirect",
        r"\bexecutePrepared\s*\(\s*context\s*\)",
        "direct Macro Track preflight must dispatch exactly once",
    )
    macro_direct_routes = (
        (
            "executeMacroDeleteTrackStructure",
            r"\bexecuteDirect\s*\(\s*state\s*,\s*Action::MacroDelete\s*,\s*"
            r"state\.sharedTrackActive\.get\s*\(\s*\)",
            "MacroDelete",
        ),
        (
            "executeMacroResetTrackStructure",
            r"\bexecuteDirect\s*\(\s*state\s*,\s*Action::MacroReset\s*,\s*"
            r"targetTrack",
            "MacroReset",
        ),
        (
            "executeMacroPasteTrackStructure",
            r"\bexecuteDirect\s*\(\s*state\s*,\s*Action::MacroPaste\s*,\s*"
            r"targetTrack\s*,\s*&track\s*,\s*automation",
            "MacroPaste",
        ),
        (
            "executeMacroCreateTrackStructure",
            r"\bexecuteDirect\s*\(\s*state\s*,\s*Action::MacroCreate\s*,\s*"
            r"targetTrack",
            "MacroCreate",
        ),
    )
    for function, route, action_name in macro_direct_routes:
        require_in_function(
            MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
            function,
            route,
            f"{function} must route the frozen {action_name} action",
        )
        require(
            MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION_HEADER,
            rf"\[\[nodiscard\]\]\s*SequencerPreparedTrackStructureResult\s*"
            rf"{re.escape(function)}\s*\(",
            f"{function} must remain a typed public cold route",
        )

    macro_service_routes = (
        (
            "MacroStructureDomainServices::deleteActiveTrack",
            "executeMacroDeleteTrackStructure",
        ),
        (
            "MacroStructureDomainServices::resetTrackContent",
            "executeMacroResetTrackStructure",
        ),
        (
            "MacroStructureDomainServices::pasteTrack",
            "executeMacroPasteTrackStructure",
        ),
        (
            "MacroStructureDomainServices::createTrack",
            "executeMacroCreateTrackStructure",
        ),
    )
    for function, adapter in macro_service_routes:
        require_in_function(
            MACRO_STRUCTURE_DOMAIN_SERVICES,
            function,
            rf"\b{re.escape(adapter)}\s*\(",
            f"{function} must delegate exactly once to {adapter}",
        )
        require_in_function(
            MACRO_STRUCTURE_DOMAIN_SERVICES,
            function,
            r"\bresult\.settled\s*\(\s*\)",
            f"{function} must consume the typed transaction result",
        )
        require_in_function(
            MACRO_STRUCTURE_DOMAIN_SERVICES,
            function,
            r"\b(?:prepareMacroTrackStructureHistory|"
            r"rollbackMacroTrackStructureHistory|"
            r"commitMacroTrackStructureHistory|applyTrackStructureMutation|"
            r"applyTrackStructureState|recordPreparedStructure)\s*\(",
            f"{function} must not retain raw Track mutation or History paths",
            count=0,
        )

    retired_macro_track_helpers = (
        "prepareMacroTrackStructureHistory",
        "rollbackMacroTrackStructureHistory",
        "commitMacroTrackStructureHistory",
        "applyTrackStructureMutation",
        "applyTrackStructureState",
        "setSharedTrackStateFromCoreState",
    )
    for retired_helper in retired_macro_track_helpers:
        require(
            MACRO_STRUCTURE_DOMAIN_SERVICES,
            rf"\b{re.escape(retired_helper)}\b",
            f"retired Macro Track helper {retired_helper} must remain absent",
            count=0,
        )
        require(
            MACRO_STRUCTURE_DOMAIN_SERVICES_HEADER,
            rf"\b{re.escape(retired_helper)}\b",
            f"retired Macro Track declaration {retired_helper} must remain absent",
            count=0,
        )

    require_in_function(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "executeDirect",
        r"^\s*if\s*\(\s*"
        r"state\.sequencer\.stepContentDraft\.rejectTransitionIfActive\s*\(\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::\s*)*"
        r"SequencerStepContentDraftBlockedTransition::TRACK\s*\)\s*\)",
        "direct Macro Track adapter must give Draft rejection first priority",
    )
    require_in_function(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "executeDirect",
        r"validIntent\s*\(\s*context\s*\).*?"
        r"pasteSourcesMatch\s*\(\s*context\s*\).*?"
        r"validateInitialTopology\s*\(\s*context\s*\).*?"
        r"executePrepared\s*\(\s*context\s*\)",
        "Macro intent, clipboard and topology checks must precede chronology",
    )
    require(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        r"\btrackClipboardValid\s*\(\s*context\.pasteAutomation\s*\)",
        "Macro Track clipboard must be validated before transaction allocation",
    )
    require_in_function(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "prepareMacroAfter",
        r"\bclearTracksInDomain\s*\(",
        "Delete/Reset/Create must mutate only the detached control domain",
        count=2,
    )
    require_in_function(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "prepareMacroAfter",
        r"\breplaceTrackFromClipboardInDomain\s*\(",
        "Paste must mutate only the detached control domain",
    )
    require_in_function(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "prepareMacroAfter",
        r"\b(?:clearTracks|replaceTrackFromClipboard)\s*\(",
        "Macro preparation must not mutate the live control domain",
        count=0,
    )
    require(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        r"sizeof\s*\(\s*void\s*\*\s*\)\s*!=\s*4U\s*\|\|\s*"
        r"sizeof\s*\(\s*DirectContext\s*\)\s*<=\s*96U",
        "Macro direct context must retain its ARM stack ceiling",
    )
    require(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        r"sizeof\s*\(\s*void\s*\*\s*\)\s*!=\s*8U\s*\|\|\s*"
        r"sizeof\s*\(\s*DirectContext\s*\)\s*<=\s*120U",
        "Macro direct context must retain its native stack ceiling",
    )
    for intent_field in (
        "clipboardRevision",
        "holdAcquisition",
        "contextRevision",
        "previewTrack",
        "previewAddTrack",
        "canonicalClipboardSource",
        "clipboardAutomation",
        "trackSelection",
    ):
        require_in_function(
            MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
            "captureIntent",
            rf"\.{re.escape(intent_field)}\s*=",
            f"Macro direct intent must capture {intent_field}",
        )
        require_in_function(
            MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
            "sameIntent",
            rf"lhs\.{re.escape(intent_field)}\s*==\s*"
            rf"rhs\.{re.escape(intent_field)}",
            f"Macro direct intent must revalidate {intent_field}",
        )
    require(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        r"\b(?:recordStructure|recordPreparedStructure|"
        r"prepareMacroTrackStructureHistory|rollbackMacroTrackStructureHistory|"
        r"commitMacroTrackStructureHistory|applyTrackStructureMutation|"
        r"applyTrackStructureState)\s*\(",
        "direct Macro adapter must not restore raw mutation or History routes",
        count=0,
    )
    require_in_function(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "reconcileCommitted",
        r"\bclearManualAndMaybeSync\s*\(.*?\bnextMacroConfigRevision\s*\(",
        "committed Macro reconciliation must clear UI state and revise config once",
    )
    require_in_function(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "settleNoChange",
        r"^\s*auto&\s+context\s*=.*?"
        r"clearManualAndMaybeSync\s*\(\s*context\s*,\s*plan\s*\)\s*;\s*$",
        "Macro NoChange settlement must remain publication-free",
    )
    for no_fail_function in (
        "clearManualAndMaybeSync",
        "reconcileCommitted",
        "settleNoChange",
    ):
        require_in_function(
            MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
            no_fail_function,
            r"\b(?:makeExtmem\w*|record\w*|rollback\w*|"
            r"markProjectMutated)\s*\(|\bnew\b|\breturn\s+false\b",
            f"{no_fail_function} must remain allocation- and failure-free",
            count=0,
        )

    require_in_function(
        CORE_SEQUENCER_HISTORY_RECORDING,
        "CoreState::commitAdmittedSequencerStructureHistory",
        r"const\s+bool\s+directMacroTrackAction\s*=\s*"
        r"change\s*&&\s*change->macroStructure\s*&&.*?"
        r"affectedTrackIndex\s*!=.*?INVALID_AFFECTED_TRACK\s*;.*?"
        r"commitAdmittedStructure\s*\(\s*std::move\s*\(\s*change\s*\)\s*\)\s*;.*?"
        r"publishPreparedSequencerMutation\s*\(\s*!directMacroTrackAction\s*\)",
        "direct Macro Track commits must suppress Project-navigation publication",
    )
    require_in_function(
        CORE_SEQUENCER_HISTORY_RECORDING,
        "CoreState::publishPreparedSequencerMutation",
        r"if\s*\(\s*notifyProjectNavigation\s*\)\s*\{\s*"
        r"markProjectMutated\s*\(\s*\)\s*;\s*\}\s*else\s*\{\s*"
        r"markProjectDurableMutation_\s*\(\s*\)\s*;\s*\}",
        "prepared publication must separate durable state from Project navigation",
    )
    require_in_function(
        MACRO_STRUCTURE_WORKFLOW,
        "MacroStructureWorkflow::pasteCurrentStructure",
        r"if\s*\(\s*services_\.pasteTrack\s*\(.*?\)\s*\)\s*\{\s*"
        r"syncPreviewToCurrentContext\s*\(\s*\)\s*;\s*"
        r"track_ui_\.previewAddSlot\.set\s*\(\s*false\s*\)\s*;\s*\}",
        "Macro Track Paste must settle its add preview only on success",
    )
    require_in_function(
        MACRO_STRUCTURE_WORKFLOW,
        "MacroStructureWorkflow::pasteCurrentStructure",
        r"\btrack_ui_\.previewAddSlot\.set\s*\(",
        "Macro Track Paste must have exactly one success-owned preview settlement",
    )
    require_in_function(
        MACRO_STRUCTURE_WORKFLOW,
        "MacroStructureWorkflow::createPreviewedStructure",
        r"case\s+(?:[A-Za-z_][A-Za-z0-9_]*::)*"
        r"StructureNavigationFocus::TRACK\s*:\s*"
        r"if\s*\(\s*services_\.createTrack\s*\(.*?\)\s*\)\s*\{.*?"
        r"track_ui_\.previewAddSlot\.set\s*\(\s*false\s*\)\s*;.*?"
        r"macro_ui_\.previewAddPageSlot\.set\s*\(\s*false\s*\)\s*;\s*"
        r"\}\s*return\s*;",
        "Macro Track Create must settle both previews only on success",
    )

    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::createPreviewedTrackStructure",
        r"\bexecuteSequencerCreateTrackStructure\s*\(",
        "Track Create edit workflow must delegate exactly once to the direct adapter",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::createPreviewedTrackStructure",
        r"\b(?:captureSequencerTrackStructureHistoryBefore|"
        r"captureSequencerTrackStructureHistoryAfter|recordPreparedStructure|"
        r"createSequencerStructureTrack|rollbackTrackCreation)\s*\(",
        "Track Create edit workflow must not retain raw mutation or rollback",
        count=0,
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::createPreviewedTrackStructure",
        r"\btrack_ui_\.previewAddSlot\.set\s*\(",
        "Track Create edit workflow must leave preview settlement to typed success",
        count=0,
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::applyCurrentStructureLongPress",
        r"\bexecuteSequencerRemoveCurrentTrackStructure\s*\(",
        "RemoveCurrent workflow must delegate exactly once to the direct adapter",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::applyCurrentStructureLongPress",
        r"\b(?:captureTrackHistoryBefore|recordTrackHistoryAfter|applyTrackState)\s*\(",
        "RemoveCurrent workflow must not retain raw capture/record/apply",
        count=0,
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::applyCurrentStructureLongPress",
        r"\btrack_ui_\.(?:previewAddSlot\.set|syncPreviewTrack)\s*\(",
        "RemoveCurrent workflow must leave preview settlement to typed success",
        count=0,
    )
    require_in_function(
        PAGE_STRUCTURE_SELECTION_WORKFLOW,
        "SequencerStructureEditWorkflow::applySelectionBottomLeftHold",
        r"if\s*\(\s*track_ui_\.selection\.active\.get\s*\(\s*\)\s*\)\s*\{.*?"
        r"track_activations_\s*==\s*nullptr.*?"
        r"const\s+auto\s+result\s*=\s*"
        r"executeSequencerRemoveSelectionTrackStructure\s*\(\s*\{.*?"
        r"\}\s*,\s*currentActiveTrack\s*\(\s*\)\s*\)\s*;\s*"
        r"if\s*\(\s*!\s*result\.settled\s*\(\s*\)\s*\)\s*return\s*;\s*"
        r"return\s*;\s*\}",
        "SelectionRemove workflow must delegate once and consume the typed result",
    )
    require_in_function(
        PAGE_STRUCTURE_SELECTION_WORKFLOW,
        "SequencerStructureEditWorkflow::applySelectionBottomLeftHold",
        r"\b(?:captureTrackHistoryBefore|recordTrackHistoryAfter|applyTrackState|"
        r"recordStructure|recordPreparedStructure)\s*\(",
        "SelectionRemove workflow must not retain raw capture, mutation or recording",
        count=0,
    )
    require_in_function(
        PAGE_STRUCTURE_SELECTION_WORKFLOW,
        "SequencerStructureEditWorkflow::applySelectionBottomLeftHold",
        r"\btrack_ui_\.selection\.reset\s*\(",
        "SelectionRemove workflow must leave Track selection settlement to typed success",
        count=0,
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::applyLatchedTrackSelectionLongPress",
        r"\b(?:commitPatternHistoryBarrier|commitCoalescedPatternEditOutcome)\s*\(",
        "SelectionRemove caller must not own a duplicate Pattern boundary",
        count=0,
    )
    for retired_selection_helper in (
        "captureTrackHistoryBefore",
        "recordTrackHistoryAfter",
        "applyTrackState",
    ):
        require(
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            rf"\b{retired_selection_helper}\b",
            f"retired SelectionRemove helper {retired_selection_helper} must remain absent",
            count=0,
        )
        require(
            PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
            rf"\b{retired_selection_helper}\b",
            f"retired SelectionRemove declaration {retired_selection_helper} must remain absent",
            count=0,
        )
    require(
        PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
        r"enum\s+class\s+TrackHoldIntent\s*:\s*uint8_t\s*\{\s*"
        r"None\s*=\s*0U\s*,\s*CurrentRemove\s*,\s*SelectionRemove\s*,\s*\}"
        r".*?TrackHoldIntent\s+track_hold_intent_\s*=\s*"
        r"TrackHoldIntent::None\s*;.*?"
        r"\buint8_t\s+track_hold_target_\s*=\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*SequencerTrackBankState::TRACK_COUNT\s*;.*?"
        r"\buint32_t\s+track_hold_acquisition_id_\s*=\s*0U\s*;",
        "Remove hold must retain distinct intent, target and shared-hold generation",
    )
    require_in_type(
        PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
        "TrackSelectionHoldToken",
        r"^\s*uint32_t\s+clipboardRevision\s*=\s*0U\s*;\s*"
        r"uint16_t\s+selectedMask\s*=\s*0U\s*;\s*"
        r"uint16_t\s+enabledMask\s*=\s*0U\s*;\s*"
        r"uint16_t\s+destinationMask\s*=\s*0U\s*;\s*"
        r"uint16_t\s+overwriteMask\s*=\s*0U\s*;\s*"
        r"uint8_t\s+cursor\s*=\s*0U\s*;\s*"
        r"uint8_t\s+previewTrack\s*=\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*SequencerTrackBankState::TRACK_COUNT\s*;\s*"
        r"uint8_t\s+flags\s*=\s*0U\s*;\s*$",
        "SelectionRemove token must retain the complete immutable selection intent",
    )
    require(
        PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
        r"static_assert\s*\(\s*sizeof\s*\(\s*TrackSelectionHoldToken\s*\)\s*"
        r"==\s*16U",
        "SelectionRemove token must retain its compact ARM ABI lock",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "packTrackSelectionHoldFlags",
        r"scope\s*==\s*(?:[A-Za-z_][A-Za-z0-9_]*::)*"
        r"StructureSelectionScope::TRACK.*?placing.*?pasteBlocked.*?previewAddTrack",
        "packed SelectionRemove flags must retain scope, placement, block and add-preview",
    )
    for source, flag in (
        (
            r"scope\s*==\s*(?:[A-Za-z_][A-Za-z0-9_]*::)*"
            r"StructureSelectionScope::TRACK",
            "TRACK_SELECTION_HOLD_SCOPE_TRACK",
        ),
        (r"placing", "TRACK_SELECTION_HOLD_PLACING"),
        (r"pasteBlocked", "TRACK_SELECTION_HOLD_PASTE_BLOCKED"),
        (r"previewAddTrack", "TRACK_SELECTION_HOLD_PREVIEW_ADD"),
    ):
        require_in_function(
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "packTrackSelectionHoldFlags",
            rf"{source}.*?\?\s*{flag}\s*:\s*0U",
            f"packed SelectionRemove flags must encode {flag}",
        )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::beginHoldAction",
        r"action\s*!=\s*(?:[A-Za-z_][A-Za-z0-9_]*::)*"
        r"StructureHoldAction::REMOVE\s*\|\|\s*"
        r"track_ui_\.selection\.active\.get\s*\(\s*\).*?"
        r"track_hold_intent_\s*=\s*TrackHoldIntent::CurrentRemove\s*;\s*"
        r"track_hold_target_\s*=\s*currentActiveTrack\s*\(\s*\)\s*;.*?"
        r"track_ui_\.hold\.begin\s*\(.*?"
        r"track_hold_acquisition_id_\s*=\s*"
        r"track_ui_\.hold\.acquisitionId\s*\(\s*\)",
        "current Remove hold must reject Selection ownership and capture provenance",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::beginSelectionHoldAction",
        r"action\s*!=\s*(?:[A-Za-z_][A-Za-z0-9_]*::)*"
        r"StructureHoldAction::REMOVE\s*\|\|\s*!\s*"
        r"track_ui_\.selection\.active\.get\s*\(\s*\)\s*\|\|\s*"
        r"track_ui_\.selection\.scope\.get\s*\(\s*\)\s*!=\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*StructureSelectionScope::TRACK\s*\|\|\s*"
        r"track_ui_\.previewAddSlot\.get\s*\(\s*\).*?"
        r"track_hold_intent_\s*=\s*TrackHoldIntent::SelectionRemove\s*;\s*"
        r"track_hold_target_\s*=\s*currentActiveTrack\s*\(\s*\)\s*;.*?"
        r"track_selection_hold_token_\s*=\s*\{\s*"
        r"\.clipboardRevision\s*=\s*track_ui_\.selection\.clipboardRevision\.get\s*\(\s*\)\s*,\s*"
        r"\.selectedMask\s*=\s*track_ui_\.selection\.selectedMask\.get\s*\(\s*\)\s*,\s*"
        r"\.enabledMask\s*=\s*currentTrackEnabledMask\s*\(\s*\)\s*,\s*"
        r"\.destinationMask\s*=\s*track_ui_\.selection\.destinationMask\.get\s*\(\s*\)\s*,\s*"
        r"\.overwriteMask\s*=\s*track_ui_\.selection\.overwriteMask\.get\s*\(\s*\)\s*,\s*"
        r"\.cursor\s*=\s*track_ui_\.selection\.cursorIndex\.get\s*\(\s*\)\s*,\s*"
        r"\.previewTrack\s*=\s*track_ui_\.previewTrackIndex\.get\s*\(\s*\)\s*,\s*"
        r"\.flags\s*=\s*packTrackSelectionHoldFlags\s*\(\s*"
        r"track_ui_\.selection\.scope\.get\s*\(\s*\)\s*,\s*"
        r"track_ui_\.selection\.placing\.get\s*\(\s*\)\s*,\s*"
        r"track_ui_\.selection\.pasteBlocked\.get\s*\(\s*\)\s*,\s*"
        r"track_ui_\.previewAddSlot\.get\s*\(\s*\)\s*\)\s*,\s*"
        r"\}\s*;.*?"
        r"track_ui_\.hold\.begin\s*\(.*?"
        r"track_hold_acquisition_id_\s*=\s*"
        r"track_ui_\.hold\.acquisitionId\s*\(\s*\)",
        "Selection Remove hold must capture its complete immutable provenance",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::applyCurrentStructureLongPress",
        r"^\s*if\s*\(\s*trackRemoveHoldPending\s*\(\s*\)\s*\)\s*\{.*?"
        r"const\s+bool\s+holdStillMatches\s*=\s*"
        r"currentTrackRemoveHoldStillMatches\s*\(\s*\)\s*;\s*"
        r"const\s+uint8_t\s+latchedTarget\s*=\s*track_hold_target_\s*;.*?"
        r"if\s*\(\s*trackRemoveHoldOwnsSharedState\s*\(\s*\)\s*\)\s*"
        r"track_ui_\.hold\.clear\s*\(\s*\)\s*;.*?"
        r"!\s*holdStillMatches.*?"
        r"const\s+auto\s+result\s*=\s*"
        r"executeSequencerRemoveCurrentTrackStructure\s*\(\s*\{.*?\}\s*,\s*"
        r"latchedTarget\s*\)\s*;.*?!\s*result\.settled\s*\(\s*\).*?"
        r"return\s*;\s*\}\s*if\s*\(\s*navigation_focus_\.get",
        "Track provenance must dispatch and consume its typed result before live-focus branches",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::clearHoldAction",
        r"^\s*clearTrackRemoveHoldIntent\s*\(\s*\)\s*;\s*"
        r"sequencer_\.structureUi\.pageHold\.clear\s*\(\s*\)\s*;\s*$",
        "explicit hold cancellation must clear both owned hold channels",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::invalidateTrackRemoveHoldIntent",
        r"track_hold_intent_\s*=\s*TrackHoldIntent::None\s*;.*?"
        r"track_hold_target_\s*=\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*SequencerTrackBankState::TRACK_COUNT\s*;.*?"
        r"track_hold_acquisition_id_\s*=\s*0U\s*;.*?"
        r"track_selection_hold_token_\s*=\s*\{\s*\}\s*;",
        "private Remove invalidation must retire every captured provenance field",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::clearTrackRemoveHoldIntent",
        r"^\s*track_ui_\.hold\.clear\s*\(\s*\)\s*;\s*"
        r"invalidateTrackRemoveHoldIntent\s*\(\s*\)\s*;\s*$",
        "owned Track Remove cancellation must clear shared state then private provenance",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::trackRemoveHoldOwnsSharedState",
        r"^\s*return\s+trackRemoveHoldPending\s*\(\s*\)\s*&&\s*"
        r"track_ui_\.hold\.action\.get\s*\(\s*\)\s*==\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*StructureHoldAction::REMOVE\s*&&\s*"
        r"track_ui_\.hold\.acquisitionId\s*\(\s*\)\s*==\s*"
        r"track_hold_acquisition_id_\s*;\s*$",
        "shared Track hold ownership must match action and exact acquisition ID",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::settleConsumedBottomLeftRelease",
        r"!\s*trackRemoveHoldPending\s*\(\s*\).*?clearHoldAction\s*\(\s*\).*?"
        r"if\s*\(\s*trackRemoveHoldOwnsSharedState\s*\(\s*\)\s*\)\s*"
        r"track_ui_\.hold\.clear\s*\(\s*\)\s*;\s*"
        r"invalidateTrackRemoveHoldIntent\s*\(\s*\)\s*;",
        "physical release must preserve a foreign replacement hold",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::trackPasteNavigationBlocked",
        r"\bhold\b",
        "Track Remove hold must not broaden paste-owned release blocking",
        count=0,
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::trackRemoveNavigationBlocked",
        r"^\s*return\s+trackRemoveHoldPending\s*\(\s*\)\s*;\s*$",
        "Track Remove NAV blocker must follow immutable provenance",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::currentTrackRemoveHoldPending",
        r"^\s*return\s+track_hold_intent_\s*==\s*"
        r"TrackHoldIntent::CurrentRemove\s*;\s*$",
        "current Track Remove provenance must remain distinct",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::currentTrackRemoveHoldStillMatches",
        r"^\s*return\s+currentTrackRemoveHoldPending\s*\(\s*\)\s*&&\s*"
        r"trackRemoveHoldOwnsSharedState\s*\(\s*\)\s*&&\s*"
        r"currentTrackRemoveIntentMatches\s*\(\s*track_hold_target_\s*\)\s*;\s*$",
        "CurrentRemove must delegate exact matching with its latched owner",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::currentTrackRemoveIntentMatches",
        r"^\s*return\s+navigation_focus_\.get\s*\(\s*\)\s*==\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*StructureNavigationFocus::TRACK\s*&&\s*"
        r"!\s*track_ui_\.selection\.active\.get\s*\(\s*\)\s*&&\s*"
        r"!\s*track_ui_\.previewAddSlot\.get\s*\(\s*\)\s*&&\s*"
        r"track_ui_\.previewTrackIndex\.get\s*\(\s*\)\s*==\s*"
        r"targetTrack\s*&&\s*currentActiveTrack\s*\(\s*\)\s*==\s*"
        r"targetTrack\s*;\s*$",
        "CurrentRemove must revalidate focus, mode, preview and latched owner exactly",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::selectionTrackRemoveHoldPending",
        r"^\s*return\s+track_hold_intent_\s*==\s*"
        r"TrackHoldIntent::SelectionRemove\s*;\s*$",
        "selection Track Remove provenance must remain distinct",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::selectionTrackRemoveHoldStillMatches",
        r"^\s*return\s+selectionTrackRemoveHoldPending\s*\(\s*\)\s*&&\s*"
        r"trackRemoveHoldOwnsSharedState\s*\(\s*\)\s*&&\s*"
        r"selectionTrackRemoveIntentMatches\s*\(\s*"
        r"track_selection_hold_token_\s*,\s*track_hold_target_\s*\)\s*;\s*$",
        "SelectionRemove must delegate exact matching with its immutable token",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::selectionTrackRemoveIntentMatches",
        r"^\s*return\s+navigation_focus_\.get\s*\(\s*\)\s*==\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*StructureNavigationFocus::TRACK\s*&&\s*"
        r"track_ui_\.selection\.active\.get\s*\(\s*\)\s*&&\s*"
        r"track_ui_\.selection\.clipboardRevision\.get\s*\(\s*\)\s*==\s*"
        r"token\.clipboardRevision\s*&&\s*"
        r"track_ui_\.selection\.selectedMask\.get\s*\(\s*\)\s*==\s*"
        r"token\.selectedMask\s*&&\s*"
        r"track_ui_\.selection\.destinationMask\.get\s*\(\s*\)\s*==\s*"
        r"token\.destinationMask\s*&&\s*"
        r"track_ui_\.selection\.overwriteMask\.get\s*\(\s*\)\s*==\s*"
        r"token\.overwriteMask\s*&&\s*"
        r"track_ui_\.selection\.cursorIndex\.get\s*\(\s*\)\s*==\s*"
        r"token\.cursor\s*&&\s*"
        r"track_ui_\.previewTrackIndex\.get\s*\(\s*\)\s*==\s*"
        r"token\.previewTrack\s*&&\s*"
        r"packTrackSelectionHoldFlags\s*\(\s*"
        r"track_ui_\.selection\.scope\.get\s*\(\s*\)\s*,\s*"
        r"track_ui_\.selection\.placing\.get\s*\(\s*\)\s*,\s*"
        r"track_ui_\.selection\.pasteBlocked\.get\s*\(\s*\)\s*,\s*"
        r"track_ui_\.previewAddSlot\.get\s*\(\s*\)\s*\)\s*==\s*"
        r"token\.flags\s*&&\s*"
        r"currentTrackEnabledMask\s*\(\s*\)\s*==\s*"
        r"token\.enabledMask\s*&&\s*"
        r"currentActiveTrack\s*\(\s*\)\s*==\s*targetTrack\s*;\s*$",
        "SelectionRemove must revalidate its complete token, topology and owner exactly",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::settleRejectedSelectionTrackRemoveLongPress",
        r"^\s*if\s*\(\s*trackRemoveHoldOwnsSharedState\s*\(\s*\)\s*\)\s*"
        r"track_ui_\.hold\.clear\s*\(\s*\)\s*;\s*$",
        "rejected Selection hold must clear only its owned shared state",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::applyLatchedCurrentTrackShortPress",
        r"^\s*if\s*\(\s*!\s*currentTrackRemoveHoldStillMatches\s*"
        r"\(\s*\)\s*\).*?if\s*\(\s*trackRemoveHoldOwnsSharedState\s*"
        r"\(\s*\)\s*\)\s*track_ui_\.hold\.clear\s*\(\s*\)\s*;\s*"
        r"invalidateTrackRemoveHoldIntent\s*\(\s*\)\s*;\s*return\s*;.*?"
        r"const\s+uint8_t\s+targetTrack\s*=\s*track_hold_target_\s*;\s*"
        r"clearTrackRemoveHoldIntent\s*\(\s*\)\s*;.*?"
        r"commitCoalescedPatternEditOutcome\s*\(\s*\).*?"
        r"track_ui_\.hold\.active\s*\(\s*\)\s*\|\|\s*"
        r"!\s*currentTrackRemoveIntentMatches\s*\(\s*targetTrack\s*\).*?"
        r"toggleSequencerStructureTrackMute\s*\(.*?targetTrack\s*\)\s*;\s*$",
        "Current Track tap must settle only its owned hold and revalidate its target",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::applyLatchedTrackSelectionShortPress",
        r"if\s*\(\s*!\s*selectionTrackRemoveHoldStillMatches\s*"
        r"\(\s*\)\s*\).*?if\s*\(\s*trackRemoveHoldOwnsSharedState\s*"
        r"\(\s*\)\s*\)\s*track_ui_\.hold\.clear\s*\(\s*\)\s*;\s*"
        r"invalidateTrackRemoveHoldIntent\s*\(\s*\).*?"
        r"track_selection_hold_token_\s*;\s*"
        r"const\s+uint8_t\s+targetTrack\s*=\s*track_hold_target_\s*;\s*"
        r"clearTrackRemoveHoldIntent\s*\(\s*\)\s*;.*?"
        r"commitCoalescedPatternEditOutcome\s*\(\s*\).*?"
        r"track_ui_\.hold\.active\s*\(\s*\)\s*\|\|\s*"
        r"!\s*selectionTrackRemoveIntentMatches\s*"
        r"\(\s*token\s*,\s*targetTrack\s*\).*?"
        r"applySelectionBottomLeftTap\s*\(\s*\)\s*;\s*$",
        "Track Selection tap must settle only its owned hold and revalidate after its boundary",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::applyLatchedTrackSelectionLongPress",
        r"track_selection_hold_token_\s*;\s*"
        r"const\s+uint8_t\s+targetTrack\s*=\s*track_hold_target_\s*;\s*"
        r"if\s*\(\s*trackRemoveHoldOwnsSharedState\s*\(\s*\)\s*\)\s*"
        r"track_ui_\.hold\.clear\s*\(\s*\)\s*;.*?"
        r"track_ui_\.hold\.active\s*\(\s*\)\s*\|\|\s*"
        r"!\s*selectionTrackRemoveIntentMatches\s*"
        r"\(\s*token\s*,\s*targetTrack\s*\).*?"
        r"applySelectionBottomLeftHold\s*\(\s*\)\s*;\s*$",
        "Track Selection hold must preserve provenance through physical release",
    )
    require_in_function(
        SEQUENCER_STEP_HANDLER,
        "SequencerStepHandler::setupBindings",
        r"\bbeginSelectionHoldAction\s*\(\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*StructureHoldAction::REMOVE\s*\)",
        "Selection press must arm SelectionRemove rather than CurrentRemove",
    )
    require(
        BUTTON_RELEASE_LATCH_HEADER,
        r"template\s*<\s*typename\s+ButtonIdT\s*>\s*"
        r"bool\s+isArmed\s*\(\s*ButtonIdT\s+button\s*\)\s+const\s*\{\s*"
        r"return\s+isArmedId\s*\(\s*static_cast\s*<\s*"
        r"oc::type::ButtonID\s*>\s*\(\s*button\s*\)\s*\)\s*;\s*\}",
        "release latch must expose a storage-neutral const armed query",
    )
    require_in_function(
        SEQUENCER_STEP_HANDLER,
        "SequencerStepHandler::setupBindings",
        r"bottom_action_release_latch_\.isArmed\s*\(\s*"
        r"Config::ButtonID::BOTTOM_LEFT\s*\)",
        "both BottomLeft release routes must stay eligible to consume an armed latch",
        count=2,
    )
    require_in_function(
        SEQUENCER_STEP_HANDLER,
        "SequencerStepHandler::setupBindings",
        r"\.button\s*\(\s*Config::ButtonID::NAV\s*\)\s*"
        r"\.press\s*\(\s*\).*?"
        r"!\s*edit_workflow_\.trackPasteNavigationBlocked\s*\(\s*\)\s*&&\s*"
        r"!\s*edit_workflow_\.trackRemoveNavigationBlocked\s*\(\s*\).*?"
        r"context_selector_workflow_\.press\s*\(",
        "new NAV selector acquisition must be rejected during Track Remove",
    )
    require_in_function(
        SEQUENCER_STEP_HANDLER,
        "SequencerStepHandler::setupBindings",
        r"\btrackRemoveNavigationBlocked\s*\(\s*\)",
        "Track Remove must block one NAV press and the three NAV turn routes",
        count=4,
    )
    require_in_function(
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW,
        "SequencerStructureNavigationWorkflow::bindStateSync",
        r"tracks_\.activeTrackSignal\s*\(\s*\)\.subscribe\s*\(\s*"
        r"\[this\]\s*\(\s*uint8_t\s+activeTrack\s*\)\s*\{\s*"
        r"syncTrackPreviewFromActive\s*\(\s*activeTrack\s*\)\s*;\s*\}",
        "active-Track subscription thunk must delegate to the cold sync helper",
    )
    require_in_function(
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW,
        "SequencerStructureNavigationWorkflow::syncTrackPreviewFromActive",
        r"^\s*if\s*\(\s*track_ui_\.hold\.active\s*\(\s*\)\s*\)\s*return\s*;\s*"
        r"track_ui_\.syncPreviewTrack\s*\(\s*activeTrack\s*\)",
        "cold active-Track sync must not rewrite preview during a hold",
    )
    require_in_function(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::createPreviewedTrackStructure",
        r"track_activations_\s*==\s*nullptr.*?"
        r"executeSequencerCreateTrackStructure\s*\(\s*\{\s*tracks_\s*,\s*"
        r"sequencer_\s*,\s*navigation_focus_\s*,\s*track_ui_\s*,\s*"
        r"structure_clipboard_\s*,\s*macro_pages_\s*,\s*\*track_activations_\s*,\s*"
        r"shared_tracks_\s*,\s*history_\s*,\s*\}\s*\)",
        "Track Create edit workflow must validate activation ownership and pass every intent source",
    )
    require_in_function(
        SEQUENCER_STEP_HANDLER,
        "SequencerStepHandler::handleContextSelectorRelease",
        r"case\s+SequencerContextSelectorAction::OPEN_TRACK_EDITOR\s*:"
        r".*?outcome\.focus\s*!=\s*"
        r"core::state::StructureNavigationFocus::TRACK.*?"
        r"track_ui_\.previewTrackIndex\.get\s*\(\s*\)\s*!=\s*"
        r"outcome\.previewTarget.*?"
        r"if\s*\(\s*outcome\.previewAddSlot\s*\).*?"
        r"track_ui_\.previewAddSlot\.get\s*\(\s*\).*?"
        r"edit_workflow_\.createPreviewedTrackStructure\s*\(\s*\)"
        r"\.settled\s*\(\s*\)",
        "Track Create binding must delegate without a duplicate Pattern barrier",
    )
    require_in_function(
        SEQUENCER_STEP_HANDLER,
        "SequencerStepHandler::handleContextSelectorRelease",
        r"\bcommitPatternHistoryBarrier\s*\(",
        "context release must retain exactly one boundary for context changes only",
        count=1,
    )
    require_in_function(
        SEQUENCER_STEP_HANDLER,
        "SequencerStepHandler::handleContextSelectorRelease",
        r"case\s+SequencerContextSelectorAction::OPEN_STEP_EDITOR\s*:"
        r".*?outcome\.focus\s*!=\s*"
        r"core::state::StructureNavigationFocus::STEP.*?"
        r"navigation_focus_\.get\s*\(\s*\)\s*!=\s*"
        r"core::state::StructureNavigationFocus::STEP.*?"
        r"sequencer_\.focusedStep\.get\s*\(\s*\)\s*!=\s*"
        r"outcome\.previewTarget.*?"
        r"step_edit_handler_->openFocusedStepAtRow\s*\(",
        "Step Editor release must revalidate its complete press target",
    )
    require_in_function(
        SEQUENCER_STEP_HANDLER,
        "SequencerStepHandler::handleContextSelectorRelease",
        r"case\s+SequencerContextSelectorAction::OPEN_PATTERN_EDITOR\s*:"
        r".*?outcome\.focus\s*!=\s*"
        r"core::state::StructureNavigationFocus::PAGE.*?"
        r"navigation_focus_\.get\s*\(\s*\)\s*!=\s*"
        r"core::state::StructureNavigationFocus::PAGE.*?"
        r"sequencer_\.structureUi\.previewPageIndex\.get\s*\(\s*\)\s*!=\s*"
        r"outcome\.previewTarget\s*\|\|\s*outcome\.previewAddSlot.*?"
        r"pattern_editor_handler_->openFromCurrentPage\s*\(\s*\)",
        "Pattern Editor release must revalidate Page focus/target and reject add provenance",
    )
    require_in_function(
        SEQUENCER_STEP_HANDLER,
        "SequencerStepHandler::setupBindings",
        r"const\s+bool\s+previewAddSlot\s*=\s*"
        r"focus\s*==\s*core::state::StructureNavigationFocus::TRACK\s*&&\s*"
        r"track_ui_\.previewAddSlot\.get\s*\(\s*\)\s*;",
        "NAV press and hold must derive add provenance from Track only",
        count=2,
    )
    require_in_function(
        SEQUENCER_STEP_HANDLER,
        "SequencerStepHandler::setupBindings",
        r"const\s+uint8_t\s+previewTarget\s*=\s*"
        r"focus\s*==\s*core::state::StructureNavigationFocus::TRACK\s*"
        r"\?\s*track_ui_\.previewTrackIndex\.get\s*\(\s*\)\s*"
        r":\s*focus\s*==\s*core::state::StructureNavigationFocus::STEP\s*"
        r"\?\s*sequencer_\.focusedStep\.get\s*\(\s*\)\s*"
        r":\s*sequencer_\.structureUi\.previewPageIndex\.get\s*\(\s*\)",
        "NAV press and hold must latch the exact Track/Page/Step target",
        count=2,
    )
    require_in_function(
        MACRO_STRUCTURE_WORKFLOW,
        "MacroStructureWorkflow::bindStateSync",
        r"shared_track_active_\.subscribe\s*\(.*?"
        r"if\s*\(\s*!\s*track_ui_\.hold\.active\s*\(\s*\)\s*\)\s*"
        r"\{\s*track_ui_\.syncPreviewTrack\s*\(\s*activeTrack\s*\)\s*;\s*\}",
        "Macro active-Track sync must preserve a shared hold's exact preview",
    )
    require_in_function(
        MACRO_STRUCTURE_WORKFLOW,
        "MacroStructureWorkflow::commitHoldAction",
        r"settleCapturedHoldAction\s*\(\s*action\s*,\s*true\s*\)",
        "Macro long release must settle exact captured provenance",
    )
    require_in_function(
        MACRO_STRUCTURE_WORKFLOW,
        "MacroStructureWorkflow::beginHoldAction",
        r"hold_target_\.action\s*\(\s*\)\s*!=\s*"
        r"core::state::StructureHoldAction::NONE\s*\|\|\s*"
        r"track_ui_\.hold\.active\s*\(\s*\)\s*\|\|\s*"
        r"macro_ui_\.pageHold\.active\s*\(\s*\).*?"
        r"return\s+false\s*;.*?"
        r"captureHoldTarget\s*\(\s*action\s*\)\s*;\s*"
        r"hold_target_\.setVisualHold\s*\(\s*armVisualHold\s*\)\s*;\s*"
        r"if\s*\(\s*!\s*armVisualHold\s*\)\s*return\s+true\s*;.*?"
        r"track_ui_\.hold\.begin\s*\(\s*action\s*,.*?\)\s*;\s*"
        r"hold_target_\.acquisitionId\s*=\s*"
        r"track_ui_\.hold\.acquisitionId\s*\(\s*\).*?"
        r"macro_ui_\.pageHold\.begin\s*\(\s*action\s*,.*?\)\s*;\s*"
        r"hold_target_\.acquisitionId\s*=\s*"
        r"macro_ui_\.pageHold\.acquisitionId\s*\(\s*\)",
        "Macro every press must capture a target and retain visual-hold identity",
    )
    require_in_function(
        MACRO_STRUCTURE_WORKFLOW,
        "MacroStructureWorkflow::hasCapturedAction",
        r"hold_target_\.action\s*\(\s*\)\s*==\s*action",
        "Macro release routing must include non-visual captured presses",
    )
    require_in_function(
        MACRO_STRUCTURE_WORKFLOW,
        "MacroStructureWorkflow::releaseShortHoldAction",
        r"return\s+settleCapturedHoldAction\s*\(\s*action\s*,\s*false\s*\)",
        "Macro short release must settle exact captured provenance",
    )
    require_in_function(
        MACRO_STRUCTURE_WORKFLOW,
        "MacroStructureWorkflow::settleCapturedHoldAction",
        r"hold_target_\.action\s*\(\s*\)\s*==\s*"
        r"core::state::StructureHoldAction::NONE.*?"
        r"return\s+false\s*;.*?"
        r"hold_target_\.action\s*\(\s*\)\s*!=\s*action\s*\)\s*"
        r"return\s+false\s*;.*?"
        r"const\s+bool\s+capturedVisualHold\s*=\s*"
        r"hold_target_\.visualHold\s*\(\s*\).*?"
        r"const\s+bool\s+ownsAcquisition\s*=.*?"
        r"!\s*requireVisualHold\s*\|\|\s*capturedVisualHold.*?"
        r"visualHold\.acquisitionId\s*\(\s*\)\s*==\s*"
        r"hold_target_\.acquisitionId.*?"
        r"!\s*track_ui_\.hold\.active\s*\(\s*\)\s*&&\s*"
        r"!\s*macro_ui_\.pageHold\.active\s*\(\s*\).*?"
        r"const\s+bool\s+noVisualHoldActive\s*=\s*"
        r"!\s*track_ui_\.hold\.active\s*\(\s*\)\s*&&\s*"
        r"!\s*macro_ui_\.pageHold\.active\s*\(\s*\).*?"
        r"const\s+bool\s+matches\s*=\s*"
        r"ownsAcquisition\s*&&\s*"
        r"capturedTargetStillMatches\s*\(\s*\).*?"
        r"if\s*\(\s*ownsAcquisition\s*&&\s*capturedVisualHold\s*\).*?"
        r"track_ui_\.hold\.clear\s*\(\s*\).*?"
        r"macro_ui_\.pageHold\.clear\s*\(\s*\).*?"
        r"hold_target_\s*=\s*\{\s*\}\s*;.*?"
        r"!\s*matches\s*&&\s*\(\s*ownsAcquisition\s*\|\|\s*"
        r"noVisualHoldActive\s*\).*?"
        r"syncPreviewToCurrentContext\s*\(\s*\)",
        "Macro hold settlement must be owner-exact, target-exact and reconcile rejection",
    )
    require(
        MACRO_STRUCTURE_WORKFLOW_HEADER,
        r"VISUAL_HOLD\s*=\s*0x20U\s*;.*?"
        r"uint32_t\s+acquisitionId\s*=\s*0U\s*;\s*"
        r"uint8_t\s+track\s*=\s*0xFFU\s*;\s*"
        r"uint8_t\s+page\s*=\s*0xFFU\s*;\s*"
        r"uint8_t\s+macro\s*=\s*0xFFU\s*;\s*"
        r"uint8_t\s+flags\s*=\s*0U\s*;.*?"
        r"bool\s+visualHold\s*\(\s*\)\s*const.*?"
        r"void\s+setVisualHold\s*\(\s*bool\s+value\s*\).*?"
        r"static_assert\s*\(\s*sizeof\s*\(\s*HoldTarget\s*\)\s*==\s*8U\s*\)",
        "Macro hold target must pack exact identity and target into eight bytes",
    )
    require(
        MACRO_STRUCTURE_WORKFLOW_HEADER,
        r"StructureHoldAction::COUNT\s*\)\s*-\s*1U\s*<=\s*"
        r"HoldTarget::ACTION_MASK.*?"
        r"StructureNavigationFocus::COUNT\s*\)\s*-\s*1U\s*\)\s*<<\s*"
        r"HoldTarget::FOCUS_SHIFT\s*\)\s*<=\s*HoldTarget::FOCUS_MASK",
        "Macro packed provenance must validate the closed enum sentinels",
    )
    require(
        MACRO_STRUCTURE_WORKFLOW_HEADER,
        r"sizeof\s*\(\s*void\s*\*\s*\)\s*!=\s*4U\s*\|\|\s*"
        r"sizeof\s*\(\s*MacroStructureWorkflow\s*\)\s*==\s*116U",
        "Macro Structure workflow must retain its ARM PSRAM envelope",
    )
    require_in_type(
        MACRO_STRUCTURE_WORKFLOW_HEADER,
        "MacroStructureWorkflow",
        r"toggleSlotSelectionAtPageIndex\s*\(\s*uint8_t\s+macroIndex\s*\)\s*;\s*"
        r"private\s*:\s*"
        r"void\s+applyCurrentStructureLongPress\s*\(\s*\)\s*;\s*"
        r"void\s+pasteCurrentStructure\s*\(\s*\)\s*;\s*"
        r"(?:\[\[nodiscard\]\]\s*)?bool\s+selectionPlacementActive\s*\(\s*\)\s*const\s*;\s*"
        r"(?:\[\[nodiscard\]\]\s*)?bool\s+selectionHasItems\s*\(\s*\)\s*const\s*;\s*"
        r"void\s+enterSlotSelection\s*\(\s*\)\s*;\s*"
        r"(?:\[\[nodiscard\]\]\s*)?bool\s+slotSelectionPlacementActive\s*\(\s*\)\s*const\s*;\s*"
        r"void\s+navigateSlotSelection\s*\(\s*float\s+delta\s*\)\s*;\s*"
        r"void\s+toggleSlotSelectionAtCursor\s*\(\s*\)\s*;\s*"
        r"(?:\[\[nodiscard\]\]\s*)?bool\s+copySlotSelection\s*\(\s*\)\s*;\s*"
        r"(?:\[\[nodiscard\]\]\s*)?bool\s+canPasteSlotSelection\s*\(\s*\)\s*const\s*;\s*"
        r"(?:\[\[nodiscard\]\]\s*)?bool\s+pasteSlotSelection\s*\(\s*\)\s*;\s*"
        r"void\s+refreshSlotSelectionPastePreview\s*\(\s*\)\s*;",
        "Macro implementation-only structure helpers must remain private",
    )
    require_in_function(
        MACRO_PERFORMANCE_HANDLER,
        "MacroPerformanceHandler::setupBindings",
        r"beginHoldAction\s*\(\s*"
        r"core::state::StructureHoldAction::REMOVE\s*,\s*"
        r"structure_workflow_\.canRemoveCurrentStructure\s*\(\s*\)\s*\).*?"
        r"beginHoldAction\s*\(\s*"
        r"core::state::StructureHoldAction::PASTE\s*,\s*canPaste\s*\)",
        "Macro accepted Clear/Copy presses must capture with or without visual hold",
    )
    require_in_function(
        MACRO_PERFORMANCE_HANDLER,
        "MacroPerformanceHandler::setupBindings",
        r"ignore_next_bottom_left_release_\s*\|\|\s*"
        r"structure_workflow_\.hasCapturedAction\s*\(\s*"
        r"core::state::StructureHoldAction::REMOVE\s*\).*?"
        r"ignore_next_bottom_right_release_\s*\|\|\s*"
        r"structure_workflow_\.hasCapturedAction\s*\(\s*"
        r"core::state::StructureHoldAction::PASTE\s*\)",
        "Macro releases must route captured non-visual actions to settlement",
    )
    require_in_function(
        STRUCTURE_NAVIGATION_STATE,
        "StructureHoldState::begin",
        r"\+\+\s*acquisition_id_\s*;\s*"
        r"if\s*\(\s*acquisition_id_\s*==\s*0U\s*\)\s*"
        r"\+\+\s*acquisition_id_\s*;\s*"
        r"startedAtMs\.set\s*\(\s*nowMs\s*\)\s*;\s*"
        r"action\.set\s*\(\s*nextAction\s*\)",
        "Structure holds must issue a distinct nonzero acquisition ID",
    )
    require(
        STRUCTURE_NAVIGATION_STATE_HEADER,
        r"enum\s+class\s+StructureNavigationFocus\s*:\s*uint8_t\s*\{\s*"
        r"PAGE\s*=\s*0\s*,\s*TRACK\s*=\s*1\s*,\s*STEP\s*=\s*2\s*,\s*"
        r"COUNT\s*=\s*3\s*,\s*\}\s*;.*?"
        r"enum\s+class\s+StructureHoldAction\s*:\s*uint8_t\s*\{\s*"
        r"NONE\s*=\s*0\s*,\s*REMOVE\s*=\s*1\s*,\s*PASTE\s*=\s*2\s*,\s*"
        r"COUNT\s*=\s*3\s*,\s*\}\s*;",
        "Structure focus and hold enums must retain explicit packed sentinels",
    )
    require(
        STRUCTURE_NAVIGATION_STATE_HEADER,
        r"Signal\s*<\s*uint32_t\s*,\s*2\s*>\s+startedAtMs\s*\{\s*0\s*\}\s*;.*?"
        r"uint32_t\s+acquisitionId\s*\(\s*\)\s*const.*?"
        r"uint32_t\s+acquisition_id_\s*=\s*0U\s*;.*?"
        r"sizeof\s*\(\s*StructureHoldState\s*\)\s*==\s*108U",
        "Structure hold identity must fit the reduced ARM signal envelope",
    )
    require_in_function(
        STRUCTURE_NAVIGATION_STATE,
        "StructureHoldState::clear",
        r"action\.set\s*\(\s*StructureHoldAction::NONE\s*\)\s*;\s*"
        r"startedAtMs\.set\s*\(\s*0U\s*\)",
        "Structure hold clear must reset presentation without recycling identity",
    )
    require_in_function(
        MACRO_VIEW,
        "MacroView::bindToState",
        r"trackNavigation\.hold\.startedAtMs\.subscribe\s*\(.*?"
        r"macroUi\.pageHold\.startedAtMs\.subscribe\s*\(",
        "Macro view must own exactly one Track and one Macro Page progress subscription",
    )
    require_in_function(
        SEQUENCER_VIEW,
        "SequencerView::bindBottomActionStripState",
        r"trackNavigation\.hold\.startedAtMs\s*,.*?"
        r"sequencer\.structureUi\.pageHold\.startedAtMs\s*,",
        "Sequencer bottom strip must own its Track and Page progress subscriptions",
    )
    require_in_function(
        SEQUENCER_OVERLAY_PRESENTER,
        "SequencerOverlayPresenter::bind",
        r"sequencer\.stepEdit\.contextHold\.startedAtMs",
        "Sequencer overlay must own its Context progress subscription",
    )
    require_across_files(
        r"\btrackNavigation\.hold\.startedAtMs\b",
        "Track hold progress topology must remain two product references",
        2,
    )
    require_across_files(
        r"\bmacroUi\.pageHold\.startedAtMs\b",
        "Macro Page hold progress topology must remain one observer plus one read",
        2,
    )
    require_across_files(
        r"\bsequencer\.structureUi\.pageHold\.startedAtMs\b",
        "Sequencer Page hold progress topology must remain one product reference",
        1,
    )
    require_across_files(
        r"\bsequencer\.stepEdit\.contextHold\.startedAtMs\b",
        "Context hold progress topology must remain one observer plus three reads",
        4,
    )
    for retired_selection_cancel_owner in (
        MACRO_STRUCTURE_WORKFLOW,
        MACRO_STRUCTURE_WORKFLOW_HEADER,
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW,
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER,
    ):
        require(
            retired_selection_cancel_owner,
            r"\bcancelSelectionMode\b",
            "caller-zero bulk selection cancellation must remain retired",
            count=0,
        )
    require_in_function(
        MACRO_PERFORMANCE_HANDLER,
        "MacroPerformanceHandler::setupBindings",
        r"releaseShortHoldAction\s*\(\s*"
        r"core::state::StructureHoldAction::REMOVE\s*\).*?"
        r"applyCurrentStructureShortPress\s*\(\s*\)",
        "Macro short Clear must validate captured Remove provenance before mutation",
    )
    require_in_function(
        MACRO_PERFORMANCE_HANDLER,
        "MacroPerformanceHandler::setupBindings",
        r"releaseShortHoldAction\s*\(\s*"
        r"core::state::StructureHoldAction::PASTE\s*\).*?"
        r"copyCurrentStructure\s*\(\s*\)",
        "Macro short Copy must validate captured Paste provenance before capture",
    )
    require(
        SEQUENCER_VIEW_HEADER,
        r"StaticWatchGroup\s*<\s*13\s*>\s+header_watcher_\s*;.*?"
        r"StaticWatchGroup\s*<\s*13\s*>\s+header_strip_watcher_\s*;.*?"
        r"StaticWatchGroup\s*<\s*41\s*>\s+grid_watcher_\s*;.*?"
        r"StaticWatchGroup\s*<\s*23\s*>\s+bottom_action_strip_watcher_\s*;",
        "Sequencer UI watcher capacities must retain the post-PageCreate compact locks",
    )

    require_in_type(
        CONTEXT_SELECTOR_WORKFLOW_HEADER,
        "SequencerContextSelectorWorkflow",
        r"uint8_t\s+press_context_\s*=\s*0U\s*;\s*"
        r"uint8_t\s+press_target_\s*=\s*0U\s*;",
        "context selector must retain compact context and exact-target provenance",
    )
    require(
        CONTEXT_SELECTOR_WORKFLOW_HEADER,
        r"sizeof\s*\(\s*void\s*\*\s*\)\s*!=\s*4U\s*\|\|\s*"
        r"sizeof\s*\(\s*SequencerContextSelectorWorkflow\s*\)\s*==\s*8U",
        "context selector must retain its 8-byte ARM RAM lock",
    )
    require(
        PRESS_HOLD_TURN_RELEASE_GESTURE_HEADER,
        r"static_assert\s*\(\s*sizeof\s*\(\s*PressHoldTurnReleaseGesture\s*\)"
        r"\s*==\s*1U\s*\)",
        "shared press/hold/turn gesture must remain one packed byte",
    )
    require_in_function(
        CONTEXT_SELECTOR_WORKFLOW,
        "SequencerContextSelectorWorkflow::press",
        r"press_target_\s*=\s*previewTarget\s*;.*?"
        r"static_cast<uint8_t>\s*\(\s*state_\.previewFocus\s*\)\s*&\s*0x03U.*?"
        r"previewAddSlot\s*\?\s*0x04U\s*:\s*0U.*?"
        r"includeTrack\s*\?\s*0x08U\s*:\s*0U",
        "context press must retain an exact target and packed context provenance",
    )
    require_in_function(
        CONTEXT_SELECTOR_WORKFLOW,
        "SequencerContextSelectorWorkflow::holdForSelection",
        r"^\s*if\s*\(\s*!\s*state_\.visible\s*\)\s*\{\s*"
        r"gesture_\.cancel\s*\(\s*\)\s*;\s*press_context_\s*=\s*0U\s*;\s*"
        r"press_target_\s*=\s*0U\s*;\s*"
        r"return\s+false\s*;\s*\}.*?"
        r"current\s*==\s*origin\s*&&\s*"
        r"previewTarget\s*==\s*press_target_\s*&&\s*"
        r"previewAddSlot\s*==\s*"
        r"\(\s*\(\s*press_context_\s*&\s*0x04U\s*\)\s*!=\s*0U\s*\).*?"
        r"if\s*\(\s*!\s*pressMatches\s*\)\s*\{\s*cancel\s*\(\s*\)",
        "context selection hold must fail closed on exact press provenance drift",
    )
    require_in_function(
        CONTEXT_SELECTOR_WORKFLOW,
        "SequencerContextSelectorWorkflow::holdForSelection",
        r"gesture_\.cancel\s*\(\s*\)\s*;\s*"
        r"press_context_\s*=\s*0U\s*;\s*press_target_\s*=\s*0U\s*;\s*"
        r"state_\.visible\s*=\s*false",
        "successful context selection transfer must clear private provenance",
    )
    require_in_function(
        CONTEXT_SELECTOR_WORKFLOW,
        "SequencerContextSelectorWorkflow::holdForSelection",
        r"gesture_\.hold\s*\(",
        "selection transfer must not set dead held state before cancellation",
        count=0,
    )
    require_in_function(
        CONTEXT_SELECTOR_WORKFLOW,
        "SequencerContextSelectorWorkflow::turn",
        r"^\s*if\s*\(\s*!\s*state_\.visible\s*\)\s*\{\s*"
        r"gesture_\.cancel\s*\(\s*\)\s*;\s*press_context_\s*=\s*0U\s*;\s*"
        r"press_target_\s*=\s*0U\s*;\s*return\s+false\s*;\s*\}.*?"
        r"press_context_\s*&\s*0x08U",
        "context turn must fail closed after external presentation reset",
    )
    require_in_function(
        CONTEXT_SELECTOR_WORKFLOW,
        "SequencerContextSelectorWorkflow::release",
        r"^\s*if\s*\(\s*!\s*state_\.visible\s*\)\s*\{\s*"
        r"gesture_\.cancel\s*\(\s*\)\s*;\s*press_context_\s*=\s*0U\s*;\s*"
        r"press_target_\s*=\s*0U\s*;\s*return\s*\{\s*\}\s*;\s*\}.*?"
        r"const\s+uint8_t\s+previewTarget\s*=\s*press_target_\s*;.*?"
        r"press_context_\s*=\s*0U\s*;\s*press_target_\s*=\s*0U\s*;",
        "context release must fail closed after external presentation reset",
    )
    require_in_function(
        CONTEXT_SELECTOR_WORKFLOW,
        "SequencerContextSelectorWorkflow::update",
        r"gesture_\.active\s*\(\s*\)\s*&&\s*!\s*state_\.visible.*?"
        r"gesture_\.cancel\s*\(\s*\)\s*;\s*press_context_\s*=\s*0U\s*;\s*"
        r"press_target_\s*=\s*0U\s*;",
        "context update must retire hidden gesture provenance",
    )
    require_in_function(
        CONTEXT_SELECTOR_WORKFLOW,
        "SequencerContextSelectorWorkflow::cancel",
        r"gesture_\.cancel\s*\(\s*\)\s*;\s*"
        r"press_context_\s*=\s*0U\s*;\s*press_target_\s*=\s*0U\s*;\s*"
        r"state_\.reset\s*\(\s*\)\s*;",
        "context cancellation must clear gesture, provenance and presentation",
    )

    require_in_type(
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER,
        "StateRefs",
        r"std::reference_wrapper\s*<\s*const\s+SharedTrackDomainServices\s*>\s*"
        r"sharedTracks\s*;",
        "Navigation StateRefs must reject temporary shared Track facades",
    )
    require(
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER,
        r"const\s+SharedTrackDomainServices\s*&\s*shared_tracks_\s*;",
        "Navigation workflow must retain only a shared Track facade reference",
    )
    require(
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER,
        r"(?:MacroPagesState|SequencerTrackActivationQueue|StructureClipboardState|"
        r"SequencerHistoryDomainServices)\s*\*",
        "Navigation workflow must not reacquire mutation ownership pointers",
        count=0,
    )
    require_in_type(
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER,
        "SequencerStructureNavigationWorkflow",
        r"\b(?:MacroPagesState|SequencerTrackActivationQueue|StructureClipboardState|"
        r"SequencerHistoryDomainServices)\b",
        "Navigation workflow must not retain mutation owner types in any form",
        count=0,
    )
    require_in_type(
        PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
        "StateRefs",
        r"\bSharedTrackDomainServices\s+sharedTracks\s*;",
        "Edit StateRefs must transfer the shared Track facade by value",
    )
    require(
        PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
        r"\bSharedTrackDomainServices\s+shared_tracks_\s*;",
        "Edit workflow must remain the unique shared Track facade owner",
    )
    require(
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER,
        r"sizeof\s*\(\s*SequencerStructureNavigationWorkflow\s*\)\s*"
        r"==\s*48U",
        "Navigation workflow must retain its ARM RAM lock",
    )
    require(
        PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
        r"sizeof\s*\(\s*SequencerStructureEditWorkflow\s*\)\s*==\s*116U",
        "Edit workflow must retain its ARM RAM lock",
    )
    require(
        PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
        r"const\s+SharedTrackDomainServices\s*&\s*sharedTrackServices\s*"
        r"\(\s*\)\s+const\s+noexcept\s*\{\s*return\s+shared_tracks_\s*;\s*\}",
        "Edit workflow must expose its owned shared Track facade as a const reference",
    )
    require(
        SEQUENCER_STEP_HANDLER_HEADER,
        r"SequencerStructureEditWorkflow\s+edit_workflow_\s*;\s*"
        r"SequencerStructureNavigationWorkflow\s+navigation_workflow_\s*;",
        "StepHandler must construct and destroy Edit around its borrowing Navigation workflow",
    )
    require(
        SEQUENCER_STEP_HANDLER_HEADER,
        r"sizeof\s*\(\s*SequencerStepHandler\s*\)\s*==\s*256U",
        "StepHandler must retain its zero-growth ARM PSRAM lock",
    )
    require(
        SEQUENCER_STEP_HANDLER,
        r"edit_workflow_\s*\(.*?\)\s*,\s*navigation_workflow_\s*\(\s*"
        r"SequencerStructureNavigationWorkflow::StateRefs\s*\{.*?"
        r"edit_workflow_\.sharedTrackServices\s*\(\s*\).*?\}\s*\)",
        "StepHandler must wire Navigation from the fully constructed Edit owner",
    )
    require(
        SEQUENCER_STEP_HANDLER,
        r"navigation_workflow_\s*\(.*?state\.sharedTracks",
        "Navigation must never borrow the by-value StepHandler StateRefs facade",
        count=0,
    )
    require(
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW,
        r"\bexecuteSequencerCreateTrackStructure\s*\(",
        "Navigation must not retain a second Track Create adapter route",
        count=0,
    )

    require(
        SHARED_TRACK_DOMAIN_SERVICES_HEADER,
        r"enum\s+class\s+PreparedTrackPresentationKind\s*:\s*uint8_t\s*\{\s*"
        r"MacroTrackTransfer\s*=\s*0U\s*,\s*SequencerActiveTrack\s*,\s*\}",
        "shared Track presentation dispatch must remain typed and exhaustive",
    )
    require_in_type(
        SHARED_TRACK_DOMAIN_SERVICES_HEADER,
        "Operations",
        r"ReconcilePreparedTrackPresentationFn\s*"
        r"reconcilePreparedTrackPresentation\s*=\s*nullptr\s*;",
        "shared Track operations must retain one typed presentation callback",
    )
    require_in_type(
        SHARED_TRACK_DOMAIN_SERVICES_HEADER,
        "Operations",
        r"^\s*void\s*\*\s*context\s*=\s*nullptr\s*;\s*"
        r"SetSharedTrackStateFn\s+setSharedTrackState\s*=\s*nullptr\s*;\s*"
        r"PublishPreparedSequencerStateFn\s+publishPreparedSequencerState\s*=\s*nullptr\s*;\s*"
        r"ReconcilePreparedTrackPresentationFn\s*"
        r"reconcilePreparedTrackPresentation\s*=\s*nullptr\s*;\s*"
        r"CapturePreparedTrackStructureSettlementCheckpointFn\s*"
        r"capturePreparedTrackStructureSettlementCheckpoint\s*=\s*nullptr\s*;\s*$",
        "shared Track operations layout must retain exactly five callback words",
    )
    require(
        SHARED_TRACK_DOMAIN_SERVICES_HEADER,
        r"ReconcilePrepared(?:MacroTrackTransfer|SequencerActiveTrackPresentation)Fn|"
        r"reconcilePrepared(?:MacroTrackTransfer|SequencerActiveTrackPresentation)\s*=",
        "shared Track facade must not restore split presentation callbacks",
        count=0,
    )
    require(
        SHARED_TRACK_DOMAIN_SERVICES_HEADER,
        r"sizeof\s*\(\s*SharedTrackDomainServices::Operations\s*\)\s*==\s*20U",
        "shared Track operations must retain their ARM ABI lock",
    )
    require(
        SHARED_TRACK_DOMAIN_SERVICES_HEADER,
        r"sizeof\s*\(\s*SharedTrackDomainServices::Operations\s*\)\s*==\s*40U",
        "shared Track operations must retain their native ABI lock",
    )
    require(
        SHARED_TRACK_DOMAIN_SERVICES_HEADER,
        r"sizeof\s*\(\s*SharedTrackDomainServices\s*\)\s*==\s*28U",
        "shared Track facade must retain its ARM ABI lock",
    )
    require(
        SHARED_TRACK_DOMAIN_SERVICES_HEADER,
        r"sizeof\s*\(\s*SharedTrackDomainServices\s*\)\s*==\s*56U",
        "shared Track facade must retain its native ABI lock",
    )
    require_in_function(
        SHARED_TRACK_DOMAIN_SERVICES,
        "reconcilePreparedTrackPresentationFromCoreState",
        r"switch\s*\(\s*kind\s*\)\s*\{\s*"
        r"case\s+PreparedTrackPresentationKind::MacroTrackTransfer\s*:"
        r"\s*state\.reconcilePreparedMacroTrackTransfer\s*\(\s*capturedTrackMask\s*\)\s*;"
        r"\s*return\s*;\s*"
        r"case\s+PreparedTrackPresentationKind::SequencerActiveTrack\s*:"
        r"\s*state\.reconcilePreparedSequencerActiveTrackPresentation\s*\(\s*\)\s*;"
        r"\s*return\s*;\s*default\s*:\s*failPreparedTrackPublicationInvariant\s*\(",
        "production shared Track presentation dispatch must preserve both typed routes",
    )
    require_in_function(
        SHARED_TRACK_DOMAIN_SERVICES,
        "SharedTrackDomainServices::reconcilePreparedMacroTrackTransfer",
        r"reconcilePreparedTrackPresentation\s*\(\s*operations_\.context\s*,\s*"
        r"PreparedTrackPresentationKind::MacroTrackTransfer\s*,\s*capturedTrackMask\s*\)",
        "Macro presentation reconciliation must dispatch its typed kind and mask",
    )
    require_in_function(
        SHARED_TRACK_DOMAIN_SERVICES,
        "SharedTrackDomainServices::reconcilePreparedSequencerActiveTrackPresentation",
        r"reconcilePreparedTrackPresentation\s*\(\s*operations_\.context\s*,\s*"
        r"PreparedTrackPresentationKind::SequencerActiveTrack\s*,\s*0U\s*\)",
        "Sequencer presentation reconciliation must dispatch its typed kind without a mask",
    )

    direct_pattern_boundary = (
        r"\b(?:commitCoalescedPatternEditOutcome|commitPatternHistoryBarrier)\s*\("
    )
    boundary_free_functions = (
        (
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "SequencerStructureEditWorkflow::pasteStructureSelection",
        ),
        (
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "SequencerStructureEditWorkflow::pastePageSelectionAfterBoundary",
        ),
        (
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "SequencerStructureEditWorkflow::pasteCurrentStructure",
        ),
        (
            PAGE_STRUCTURE_EDIT_WORKFLOW,
            "SequencerStructureEditWorkflow::pasteCurrentPageAfterBoundary",
        ),
    ) + tuple((rel, function) for rel, function, _, _ in prepared_structure_routes) + tuple(
        (rel, function) for rel, function, _ in prepared_structure_helpers
    )
    for rel, function in boundary_free_functions:
        require_in_function(
            rel,
            function,
            direct_pattern_boundary,
            "prepared Page/Step path must not own a direct Pattern boundary",
            count=0,
        )

    for method in ("pasteStructureSelection", "pasteCurrentStructure"):
        call_pattern = rf"(?:\.|->)\s*{re.escape(method)}\s*\(\s*\)\s*;"
        observed = sum(
            len(re.findall(call_pattern, masked_file(rel)))
            for rel, content in files.items()
            if rel.startswith("src/") and method in content
        )
        if observed != 1:
            errors.append(
                f"src: expected exactly one {method} Page dispatch call expression, "
                f"found {observed}"
            )

    require(
        SEQUENCER_STEP_HANDLER,
        r"return\s+sequencer_\.structureUi\.pageSelection\.active\.get\s*"
        r"\(\s*\).*?SelectionAction::PASTE_SELECTION\s*;\s*\}\s*\)\s*"
        r"\.then\s*\(\s*\[this\]\s*\(\s*\)\s*\{\s*"
        r"bottom_action_release_latch_\.arm\s*\(\s*"
        r"Config::ButtonID::BOTTOM_RIGHT\s*\)\s*;.*?#endif\s*"
        r"edit_workflow_\.pasteStructureSelection\s*\(\s*\)\s*;",
        "Page-selection BottomRight must latch then delegate without an old barrier",
    )
    require(
        SEQUENCER_STEP_HANDLER,
        r"\.when\s*\(\s*\[this\]\s*\(\s*\)\s*\{\s*return\s+"
        r"currentStructureBottomActionsAvailable\s*\(\s*\)\s*&&\s*"
        r"!trackFocusActive\s*\(\s*\)\s*;\s*\}\s*\)\s*\.then\s*"
        r"\(\s*\[this\]\s*\(\s*\)\s*\{\s*"
        r"bottom_action_release_latch_\.arm\s*\(\s*"
        r"Config::ButtonID::BOTTOM_RIGHT\s*\)\s*;.*?#endif\s*"
        r"edit_workflow_\.pasteCurrentStructure\s*"
        r"\(\s*\)\s*;",
        "current Page/Step BottomRight must latch then delegate without an old barrier",
    )
    require(
        SEQUENCER_STEP_HANDLER,
        r"return\s+sequencer_\.structureUi\.stepSelection\.active\.get\s*"
        r"\(\s*\)\s*&&.*?SelectionAction::PASTE_SELECTION\s*;\s*\}\s*\)\s*"
        r"\.then\s*\(\s*\[this\]\s*\(\s*\)\s*\{\s*"
        r"bottom_action_release_latch_\.arm\s*\(\s*"
        r"Config::ButtonID::BOTTOM_RIGHT\s*\)\s*;.*?#endif\s*"
        r"edit_workflow_\.pasteStepSelection\s*\(\s*\)\s*;",
        "Step-selection BottomRight must latch then delegate without pre-settlement UI or boundary",
    )
    require(
        SEQUENCER_STEP_HANDLER,
        r"if\s*\(\s*edit_workflow_\.selectionTrackRemoveHoldPending\s*"
        r"\(\s*\)\s*\)\s*return\s+true\s*;.*?"
        r"bottom_action_release_latch_\.arm\s*\(\s*"
        r"Config::ButtonID::BOTTOM_LEFT\s*\)\s*;.*?"
        r"if\s*\(\s*edit_workflow_\.selectionTrackRemoveHoldPending\s*"
        r"\(\s*\)\s*\)\s*\{\s*"
        r"edit_workflow_\.applyLatchedTrackSelectionLongPress\s*\(\s*\)\s*;\s*"
        r"return\s*;\s*\}.*?"
        r"if\s*\(\s*track_ui_\.selection\.active\.get\s*\(\s*\)\s*\)\s*\{\s*"
        r"edit_workflow_\.clearHoldAction\s*\(\s*\)\s*;\s*\}.*?"
        r"applySelectionBottomLeftHold\s*\(\s*\)",
        "selection BottomLeft hold must route tokenized Track provenance before legacy Page/Step",
    )
    require(
        SEQUENCER_STEP_HANDLER,
        r"return\s+bottom_action_release_latch_\.isArmed\s*\(.*?"
        r"\)\s*\|\|\s*edit_workflow_\.selectionTrackRemoveHoldPending\s*"
        r"\(\s*\)\s*\|\|.*?"
        r"bottom_action_release_latch_\.consume\s*\(\s*"
        r"Config::ButtonID::BOTTOM_LEFT\s*\).*?"
        r"if\s*\(\s*edit_workflow_\.selectionTrackRemoveHoldPending\s*"
        r"\(\s*\)\s*\)\s*\{\s*"
        r"edit_workflow_\.applyLatchedTrackSelectionShortPress\s*\(\s*\)\s*;\s*"
        r"return\s*;\s*\}.*?"
        r"commitPatternHistoryBarrier\s*\(\s*history_\s*\).*?"
        r"applySelectionBottomLeftTap\s*\(\s*\)",
        "selection BottomLeft tap must route tokenized Track provenance before legacy Page/Step",
    )
    require(
        SEQUENCER_STEP_HANDLER,
        r"if\s*\(\s*edit_workflow_\.currentTrackRemoveHoldPending\s*"
        r"\(\s*\)\s*\)\s*return\s+true\s*;.*?"
        r"bottom_action_release_latch_\.consume\s*\(\s*"
        r"Config::ButtonID::BOTTOM_LEFT\s*\).*?"
        r"if\s*\(\s*edit_workflow_\.currentTrackRemoveHoldPending\s*"
        r"\(\s*\)\s*\)\s*\{\s*"
        r"edit_workflow_\.applyLatchedCurrentTrackShortPress\s*\(\s*\)\s*;\s*"
        r"return\s*;\s*\}.*?"
        r"if\s*\(\s*edit_workflow_\.trackRemoveHoldPending\s*\(\s*\)\s*\)"
        r"\s*\{\s*edit_workflow_\.clearHoldAction\s*\(\s*\)\s*;\s*"
        r"return\s*;\s*\}.*?"
        r"if\s*\(\s*trackFocusActive\s*\(\s*\)\s*\).*?"
        r"commitPatternHistoryBarrier\s*\(\s*history_\s*\).*?"
        r"applyCurrentStructureShortPress\s*\(\s*\)",
        "current BottomLeft tap must route tokenized Track provenance before legacy Page/Step",
    )
    require(
        SEQUENCER_STEP_HANDLER,
        r"\.button\s*\(\s*Config::ButtonID::BOTTOM_LEFT\s*\)\s*"
        r"\.longPress\s*\([^;]*\)\s*\.scope\s*\([^;]*\)\s*\.when\s*"
        r"\(\s*\[this\]\s*\(\s*\)\s*\{.*?"
        r"currentStructureBottomActionsAvailable\s*\(\s*\).*?"
        r"canRemoveCurrentStructure\s*\(\s*\).*?\.then\s*"
        r"\(\s*\[this\]\s*\(\s*\)\s*\{\s*"
        r"bottom_action_release_latch_\.arm\s*\(\s*"
        r"Config::ButtonID::BOTTOM_LEFT\s*\)\s*;.*?#endif\s*"
        r"edit_workflow_\.applyCurrentStructureLongPress\s*"
        r"\(\s*\)\s*;",
        "current BottomLeft hold must latch then delegate without an old barrier",
    )

    project_snapshot = "src/state/project/ProjectSnapshot.cpp"
    require(
        project_snapshot,
        r"\bapplyProjectSnapshot\s*\([^)]*\)\s*\{\s*if\s*\(\s*"
        r"state\.sequencer\.stepContentDraft\.active\.get\s*\(\s*\)\s*\)\s*"
        r"\{\s*state\.sequencer\.stepContentDraft\.noteBlockedTransition\s*\(\s*"
        r"(?:[A-Za-z_][A-Za-z0-9_]*::)*\s*"
        r"SequencerStepContentDraftBlockedTransition::PROJECT_LOAD\s*\)\s*;\s*"
        r"return\s+false\s*;\s*\}",
        "applyProjectSnapshot must start with its PROJECT_LOAD guard",
    )
    deep_full_bank_guard = re.search(
        r"\bapplyHistorySnapshot\s*\([^)]*SequencerHistoryTrackBankSnapshot&"
        r"[^)]*\)\s*\{\s*if\s*\(\s*active\.stepContentDraft\.active\.get\s*"
        r"\(\s*\)\s*\).*?SequencerStepContentDraftBlockedTransition::PROJECT_LOAD",
        files.get(STEP_DRAFT_HISTORY, ""),
        flags=re.DOTALL,
    )
    if deep_full_bank_guard:
        errors.append(
            f"{STEP_DRAFT_HISTORY}: retired deep FullBank PROJECT_LOAD guard restored"
        )
    return errors


def self_test() -> int:
    step_draft_fixture = {
        path.relative_to(ROOT).as_posix(): path.read_text(encoding="utf-8")
        for path in source_files()
    }
    cold_placement_fixture = COLD_PLACEMENT.read_text(encoding="utf-8")
    missing_structure_cold_selector_fixtures = tuple(
        (selector, cold_placement_fixture.replace(selector, "", 1))
        for selector in COLD_PLACEMENT_CONTRACT_SELECTORS
    )

    def mutate(rel: str, before: str, after: str) -> dict[str, str]:
        result = dict(step_draft_fixture)
        result[rel] = result[rel].replace(before, after, 1)
        return result

    def mutate_pattern(
        rel: str,
        pattern: str,
        replacement: str,
    ) -> dict[str, str]:
        result = dict(step_draft_fixture)
        result[rel] = re.sub(
            pattern,
            replacement,
            result[rel],
            count=1,
            flags=re.DOTALL,
        )
        return result

    extmem_fixture = {
        rel: step_draft_fixture[rel]
        for rel in (
            EXTMEM_ALLOCATOR_SOURCE,
            CORE_STATE_SOURCE,
            CORE_STATE_HEADER,
        )
    }

    def mutate_extmem(rel: str, before: str, after: str) -> dict[str, str]:
        result = dict(extmem_fixture)
        result[rel] = result[rel].replace(before, after, 1)
        return result

    wrong_strict_allocate_pool = mutate_extmem(
        EXTMEM_ALLOCATOR_SOURCE,
        "return sm_malloc_pool(&extmem_smalloc_pool, bytes);",
        "return sm_malloc_pool(&other_smalloc_pool, bytes);",
    )
    wrong_strict_free_pool = mutate_extmem(
        EXTMEM_ALLOCATOR_SOURCE,
        "sm_free_pool(&extmem_smalloc_pool, ptr);",
        "sm_free_pool(&other_smalloc_pool, ptr);",
    )
    unpaired_extmem_deleter = mutate_extmem(
        EXTMEM_ALLOCATOR_SOURCE,
        "        freeExtmemStrict(ptr);",
        "        freeExtmemFallback(ptr);",
    )
    pending_apply_non_strict_allocation = mutate_extmem(
        CORE_STATE_SOURCE,
        "core::app::allocateExtmemStrict(",
        "core::app::allocateExtmemFallback(",
    )
    pending_apply_non_strict_free = mutate_extmem(
        CORE_STATE_SOURCE,
        "core::app::freeExtmemStrict(ptr);",
        "core::app::freeExtmemFallback(ptr);",
    )
    pending_apply_default_deleter = mutate_extmem(
        CORE_STATE_HEADER,
        "using PendingApplyPtr = std::unique_ptr<PendingApply, PendingApplyDeleter>;",
        "using PendingApplyPtr = std::unique_ptr<PendingApply>;",
    )
    pending_apply_not_adopted = mutate_extmem(
        CORE_STATE_SOURCE,
        "sequencerDomain_.pendingApply.reset(createPendingApply());",
        "(void)createPendingApply();",
    )
    escaped_strict_extmem_call = dict(extmem_fixture)
    escaped_strict_extmem_call["src/state/CoreStateProjectHistory.cpp"] = (
        "\nvoid* escapedStrictExtmemCall() {\n"
        "    return core::app::allocateExtmemStrict(32U);\n"
        "}\n"
    )

    changed_label = mutate(
        STEP_DRAFT_LABELS_SOURCE,
        '"APPLY BEFORE UNDO/REDO"',
        '"APPLY BEFORE HISTORY"',
    )
    missing_history_guard = mutate(
        "src/state/CoreStateProjectHistory.cpp",
        "SequencerStepContentDraftBlockedTransition::HISTORY",
        "SequencerStepContentDraftBlockedTransition::RESET",
    )
    restored_deep_guard = mutate(
        STEP_DRAFT_HISTORY,
        "                                   const SequencerHistoryTrackBankSnapshot& snapshot) {",
        "                                   const SequencerHistoryTrackBankSnapshot& snapshot) {\n"
        "    if (active.stepContentDraft.active.get()) {\n"
        "        active.stepContentDraft.noteBlockedTransition(\n"
        "            SequencerStepContentDraftBlockedTransition::PROJECT_LOAD);\n"
        "        return false;\n"
        "    }",
    )
    missing_structure_guard = mutate(
        PAGE_STRUCTURE_TRANSACTION,
        "SequencerStepContentDraftBlockedTransition::STRUCTURE_EDIT",
        "SequencerStepContentDraftBlockedTransition::RESET",
    )
    duplicate_page_boundary = mutate(
        PAGE_STRUCTURE_TRANSACTION,
        "const auto outcome = history_.commitCoalescedPatternEditOutcome();",
        "(void)history_.commitCoalescedPatternEditOutcome();\n"
        "    const auto outcome = history_.commitCoalescedPatternEditOutcome();",
    )
    missing_page_revalidation = mutate(
        PAGE_STRUCTURE_TRANSACTION,
        "execution.revalidate(execution.mutationContext, sequencer_)",
        "execution.skipRevalidation(execution.mutationContext, sequencer_)",
    )
    public_page_lifecycle = mutate(
        PAGE_STRUCTURE_TRANSACTION_HEADER,
        "private:\n    enum class Phase",
        "public:\n    enum class Phase",
    )
    nonfatal_page_abort = mutate(
        PAGE_STRUCTURE_TRANSACTION,
        "outcome != seq::SequencerPreparedPatternEditAbortOutcome::Aborted",
        "outcome == seq::SequencerPreparedPatternEditAbortOutcome::Aborted",
    )
    nonfatal_page_commit = mutate(
        PAGE_STRUCTURE_TRANSACTION,
        "case CommitOutcome::NoPending:\n            failPageStructureTransactionInvariant();",
        "case CommitOutcome::NoPending:\n            return Result::NoChange;",
    )
    nonfatal_invalid_seal = mutate(
        PAGE_STRUCTURE_TRANSACTION,
        "default:\n            failPageStructureTransactionInvariant();\n    }\n\n"
        "    const auto commitOutcome",
        "default:\n            return Result::Failed;\n    }\n\n"
        "    const auto commitOutcome",
    )
    removed_page_trap = mutate(
        PAGE_STRUCTURE_TRANSACTION,
        "__builtin_trap();",
        "for (;;) {}",
    )
    restored_page_paste_raw = dict(step_draft_fixture)
    restored_page_paste_raw[PAGE_STRUCTURE_EDIT_WORKFLOW] += (
        "\nvoid injectedRawPagePaste() { pastePageClipboard(); }\n"
    )
    restored_page_selection_raw = dict(step_draft_fixture)
    restored_page_selection_raw[PAGE_STRUCTURE_EDIT_WORKFLOW] += (
        "\nvoid injectedRawPageSelectionPaste() { "
        "pastePageSelectionClipboard(); }\n"
    )
    restored_page_create_raw = dict(step_draft_fixture)
    restored_page_create_raw[PAGE_STRUCTURE_EDIT_WORKFLOW] += (
        "\nvoid injectedRawPageCreate() { createSequencerStructurePage(); }\n"
    )
    restored_prepared_page_create = dict(step_draft_fixture)
    restored_prepared_page_create[PAGE_STRUCTURE_NAVIGATION_WORKFLOW] += (
        "\nvoid injectedPreparedPageCreate() { (void)PageCreate; }\n"
    )
    restored_sequencer_page_add_signal = dict(step_draft_fixture)
    restored_sequencer_page_add_signal[
        "src/state/sequencer/SequencerUiState.hpp"
    ] += "\nstruct InjectedPageAddState { bool previewAddPageSlot; };\n"
    missing_pattern_editor_add_rejection = mutate(
        SEQUENCER_STEP_HANDLER,
        "                    outcome.previewTarget || outcome.previewAddSlot) {",
        "                    outcome.previewTarget) {",
    )
    broadened_nav_add_capture = mutate(
        SEQUENCER_STEP_HANDLER,
        "                focus == core::state::StructureNavigationFocus::TRACK &&\n"
        "                track_ui_.previewAddSlot.get();",
        "                focus != core::state::StructureNavigationFocus::STEP &&\n"
        "                track_ui_.previewAddSlot.get();",
    )
    missing_step_target_capture = mutate(
        SEQUENCER_STEP_HANDLER,
        "                    : focus == core::state::StructureNavigationFocus::STEP\n"
        "                        ? sequencer_.focusedStep.get()\n"
        "                        : sequencer_.structureUi.previewPageIndex.get();",
        "                    : sequencer_.structureUi.previewPageIndex.get();",
    )
    missing_step_release_revalidation = mutate(
        SEQUENCER_STEP_HANDLER,
        "                    core::state::StructureNavigationFocus::STEP ||\n"
        "                sequencer_.focusedStep.get() != outcome.previewTarget) {",
        "                    core::state::StructureNavigationFocus::STEP) {",
    )
    truncated_selector_target = mutate(
        CONTEXT_SELECTOR_WORKFLOW,
        "    press_target_ = previewTarget;",
        "    press_target_ = static_cast<uint8_t>(previewTarget & 0x0FU);",
    )
    missing_hidden_selector_target_clear = mutate_pattern(
        CONTEXT_SELECTOR_WORKFLOW,
        r"(SequencerContextSelectorWorkflow::release\s*\(\s*\)\s*\{\s*"
        r"if\s*\(\s*!\s*state_\.visible\s*\)\s*\{\s*"
        r"gesture_\.cancel\s*\(\s*\)\s*;\s*"
        r"press_context_\s*=\s*0U\s*;)\s*press_target_\s*=\s*0U\s*;",
        r"\g<1>",
    )
    restored_selector_feedback = dict(step_draft_fixture)
    restored_selector_feedback[CONTEXT_SELECTOR_WORKFLOW_HEADER] += (
        "\nenum class SequencerContextSelectorFeedback : uint8_t { NONE = 0 };\n"
    )
    widened_selector_gesture = mutate(
        PRESS_HOLD_TURN_RELEASE_GESTURE_HEADER,
        "sizeof(PressHoldTurnReleaseGesture) == 1U",
        "sizeof(PressHoldTurnReleaseGesture) == 2U",
    )
    widened_context_selector_arm_ram = mutate(
        CONTEXT_SELECTOR_WORKFLOW_HEADER,
        "sizeof(SequencerContextSelectorWorkflow) == 8U",
        "sizeof(SequencerContextSelectorWorkflow) == 12U",
    )
    unguarded_macro_track_preview_sync = mutate(
        MACRO_STRUCTURE_WORKFLOW,
        "            if (!track_ui_.hold.active()) {\n"
        "                track_ui_.syncPreviewTrack(activeTrack);\n"
        "            }",
        "            track_ui_.syncPreviewTrack(activeTrack);",
    )
    missing_macro_short_clear_provenance = mutate(
        MACRO_PERFORMANCE_HANDLER,
        "            if (!structure_workflow_.releaseShortHoldAction(\n"
        "                    core::state::StructureHoldAction::REMOVE\n"
        "                )) {",
        "            if (false) {",
    )
    missing_macro_short_copy_provenance = mutate(
        MACRO_PERFORMANCE_HANDLER,
        "            if (!structure_workflow_.releaseShortHoldAction(\n"
        "                    core::state::StructureHoldAction::PASTE\n"
        "                )) {",
        "            if (false) {",
    )
    missing_macro_hold_acquisition_identity = mutate(
        MACRO_STRUCTURE_WORKFLOW,
        "                visualHold.acquisitionId() == hold_target_.acquisitionId",
        "                true",
    )
    missing_macro_external_clear_reconcile = mutate(
        MACRO_STRUCTURE_WORKFLOW,
        "    if (!matches && (ownsAcquisition || noVisualHoldActive)) {",
        "    if (ownsAcquisition && !matches) {",
    )
    missing_macro_nonvisual_press_capture = mutate(
        MACRO_PERFORMANCE_HANDLER,
        "                structure_workflow_.canRemoveCurrentStructure()",
        "                true",
    )
    missing_macro_captured_release_routing = mutate(
        MACRO_PERFORMANCE_HANDLER,
        "structure_workflow_.hasCapturedAction(",
        "false && structure_workflow_.hasCapturedAction(",
    )
    missing_macro_chord_exclusion = mutate_pattern(
        MACRO_STRUCTURE_WORKFLOW,
        r"\s*if\s*\(hold_target_\.action\(\)\s*!=\s*"
        r"core::state::StructureHoldAction::NONE\s*\|\|\s*"
        r"track_ui_\.hold\.active\(\)\s*\|\|\s*"
        r"macro_ui_\.pageHold\.active\(\)\)\s*\{\s*"
        r"return\s+false;\s*\}",
        "",
    )
    consumed_macro_mismatched_release = mutate(
        MACRO_STRUCTURE_WORKFLOW,
        "    if (hold_target_.action() != action) return false;",
        "",
    )
    missing_structure_hold_acquisition_id = mutate(
        STRUCTURE_NAVIGATION_STATE,
        "    ++acquisition_id_;",
        "",
    )
    widened_structure_hold_progress_signal = mutate(
        STRUCTURE_NAVIGATION_STATE_HEADER,
        "Signal<uint32_t, 2> startedAtMs",
        "Signal<uint32_t, 4> startedAtMs",
    )
    widened_macro_hold_target = mutate(
        MACRO_STRUCTURE_WORKFLOW_HEADER,
        "sizeof(HoldTarget) == 8U",
        "sizeof(HoldTarget) == 12U",
    )
    widened_macro_structure_workflow = mutate(
        MACRO_STRUCTURE_WORKFLOW_HEADER,
        "sizeof(MacroStructureWorkflow) == 116U",
        "sizeof(MacroStructureWorkflow) == 120U",
    )
    restored_public_macro_structure_helper = mutate(
        MACRO_STRUCTURE_WORKFLOW_HEADER,
        "    void toggleSlotSelectionAtPageIndex(uint8_t macroIndex);\n\n"
        "private:\n"
        "    void applyCurrentStructureLongPress();",
        "    void toggleSlotSelectionAtPageIndex(uint8_t macroIndex);\n"
        "    void applyCurrentStructureLongPress();\n\n"
        "private:",
    )
    extra_track_hold_progress_observer = dict(step_draft_fixture)
    extra_track_hold_progress_observer[MACRO_VIEW] += (
        "\nvoid injectedThirdTrackHoldProgressObserver() {\n"
        "    (void)state_refs_.trackNavigation.hold.startedAtMs;\n"
        "}\n"
    )
    stale_context_selection_provenance = mutate(
        CONTEXT_SELECTOR_WORKFLOW,
        "    gesture_.cancel();\n"
        "    press_context_ = 0U;\n"
        "    press_target_ = 0U;",
        "    gesture_.cancel();",
    )
    restored_selector_dead_hold = mutate(
        CONTEXT_SELECTOR_WORKFLOW,
        "    gesture_.cancel();\n"
        "    press_context_ = 0U;\n"
        "    press_target_ = 0U;\n"
        "    state_.visible = false;",
        "    gesture_.hold();\n"
        "    gesture_.cancel();\n"
        "    press_context_ = 0U;\n"
        "    press_target_ = 0U;\n"
        "    state_.visible = false;",
    )
    restored_bulk_selection_cancel = dict(step_draft_fixture)
    restored_bulk_selection_cancel[MACRO_STRUCTURE_WORKFLOW_HEADER] += (
        "\nvoid cancelSelectionMode();\n"
    )
    widened_sequencer_header_watcher = mutate(
        SEQUENCER_VIEW_HEADER,
        "StaticWatchGroup<13> header_watcher_;",
        "StaticWatchGroup<14> header_watcher_;",
    )
    restored_page_ensure_raw = dict(step_draft_fixture)
    restored_page_ensure_raw[PAGE_STRUCTURE_EDIT_WORKFLOW] += (
        "\nvoid injectedRawPageEnsure() { ensurePageExists(); }\n"
    )
    restored_step_paste_raw = dict(step_draft_fixture)
    restored_step_paste_raw[PAGE_STRUCTURE_EDIT_WORKFLOW] += (
        "\nvoid injectedRawStepPaste() { commitStructureStepPastePlan(); }\n"
    )
    restored_step_reset_raw = dict(step_draft_fixture)
    restored_step_reset_raw[PAGE_STRUCTURE_EDIT_WORKFLOW] += (
        "\nvoid injectedRawStepReset() { resetActiveContentStep(); }\n"
    )
    restored_page_selection_delete_raw = dict(step_draft_fixture)
    restored_page_selection_delete_raw[PAGE_STRUCTURE_SELECTION_WORKFLOW] += (
        "\nvoid injectedRawPageSelectionDelete() { deleteSelectedRootPages(); }\n"
    )
    restored_additional_raw_symbols = tuple(
        (
            symbol,
            {
                **step_draft_fixture,
                PAGE_STRUCTURE_EDIT_WORKFLOW:
                    step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
                    + f"\nvoid injectedRetiredPageSymbol() {{ (void)sizeof({symbol}); }}\n",
            },
        )
        for symbol in (
            "clearCurrentSequencerStructurePage",
            "deleteCurrentSequencerStructurePage",
            "SequencerPageStructureHistoryChangePtr",
            "sequencerHistoryPageCount",
            "makeSequencerPageStructureHistoryDescriptor",
        )
    )
    restored_track_raw_symbols = tuple(
        (
            symbol,
            {
                **step_draft_fixture,
                PAGE_STRUCTURE_EDIT_WORKFLOW:
                    step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
                    + f"\nvoid injectedRetiredTrackSymbol() {{ {symbol}(); }}\n",
            },
        )
        for symbol in RETIRED_RAW_TRACK_SYMBOLS
    )
    miswired_create_action = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "        Action::SequencerCreate,\n"
        "        TrackBank::TRACK_COUNT",
        "        Action::SequencerRemoveCurrent,\n"
        "        TrackBank::TRACK_COUNT",
    )
    miswired_remove_action = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "        Action::SequencerRemoveCurrent,\n"
        "        latchedTargetTrack",
        "        Action::SequencerCreate,\n"
        "        latchedTargetTrack",
    )
    miswired_selection_action = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "        Action::SequencerRemoveSelection,\n"
        "        latchedActiveTrack",
        "        Action::SequencerRemoveCurrent,\n"
        "        latchedActiveTrack",
    )
    miswired_macro_create_action = mutate(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "        Action::MacroCreate,\n"
        "        targetTrack,\n"
        "        nullptr,\n"
        "        nullptr\n"
        "    );\n"
        "}",
        "        Action::MacroReset,\n"
        "        targetTrack,\n"
        "        nullptr,\n"
        "        nullptr\n"
        "    );\n"
        "}",
    )
    bypassed_macro_reset_service = mutate(
        MACRO_STRUCTURE_DOMAIN_SERVICES,
        "    const auto result = executeMacroResetTrackStructure(\n",
        "    const auto result = executeMacroCreateTrackStructure(\n",
    )
    restored_macro_track_rollback = {
        **step_draft_fixture,
        MACRO_STRUCTURE_DOMAIN_SERVICES:
            step_draft_fixture[MACRO_STRUCTURE_DOMAIN_SERVICES]
            + "\nvoid injectedMacroRollback() { "
            + "rollbackMacroTrackStructureHistory(); }\n",
    }
    widened_macro_direct_context_arm_stack = mutate(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "sizeof(DirectContext) <= 96U",
        "sizeof(DirectContext) <= 100U",
    )
    missing_macro_clipboard_prevalidation = mutate(
        MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "macro_structure_automation_ops::trackClipboardValid(",
        "macro_structure_automation_ops::skipTrackClipboardValidation(",
    )
    lost_macro_navigation_suppression = mutate(
        CORE_SEQUENCER_HISTORY_RECORDING,
        "publishPreparedSequencerMutation(!directMacroTrackAction);",
        "publishPreparedSequencerMutation(true);",
    )
    broken_macro_paste_preview_settlement = mutate_pattern(
        MACRO_STRUCTURE_WORKFLOW,
        r"(MacroStructureWorkflow::pasteCurrentStructure\s*\(\s*\).*?"
        r"track_ui_\.previewAddSlot\.)set\s*\(\s*false\s*\)",
        r"\g<1>set(true)",
    )
    miswired_create_workflow = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "executeSequencerCreateTrackStructure({",
        "executeSequencerRemoveCurrentTrackStructure({",
    )
    miswired_remove_workflow = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "executeSequencerRemoveCurrentTrackStructure({",
        "executeSequencerCreateTrackStructure({",
    )
    miswired_selection_workflow = mutate(
        PAGE_STRUCTURE_SELECTION_WORKFLOW,
        "executeSequencerRemoveSelectionTrackStructure({",
        "executeSequencerRemoveCurrentTrackStructure({",
    )
    ignored_selection_result = mutate(
        PAGE_STRUCTURE_SELECTION_WORKFLOW,
        "        if (!result.settled()) return;\n"
        "        return;",
        "        (void)result;\n"
        "        return;",
    )
    premature_selection_settlement = mutate(
        PAGE_STRUCTURE_SELECTION_WORKFLOW,
        "        const auto result = executeSequencerRemoveSelectionTrackStructure({",
        "        track_ui_.selection.reset(\n"
        "            core::state::StructureSelectionScope::TRACK,\n"
        "            currentActiveTrack()\n"
        "        );\n"
        "        const auto result = executeSequencerRemoveSelectionTrackStructure({",
    )
    restored_selection_raw_history = mutate(
        PAGE_STRUCTURE_SELECTION_WORKFLOW,
        "        const auto result = executeSequencerRemoveSelectionTrackStructure({",
        "        captureTrackHistoryBefore();\n"
        "        const auto result = executeSequencerRemoveSelectionTrackStructure({",
    )
    residual_selection_boundary = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "    applySelectionBottomLeftHold();\n"
        "}",
        "    (void)history_.commitCoalescedPatternEditOutcome();\n"
        "    applySelectionBottomLeftHold();\n"
        "}",
    )
    incomplete_selection_remove_intent = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "        return token.trackSelection.active &&\n"
        "               token.trackSelection.scope ==\n"
        "                   core::state::StructureSelectionScope::TRACK &&",
        "        return token.trackSelection.active &&",
    )
    unsanitized_selection_topology = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "        const uint16_t selectedMask = activeTrackSelectionMask(\n"
        "            context.token.trackSelection.selectedMask,\n"
        "            enabledMask\n"
        "        );",
        "        const uint16_t selectedMask =\n"
        "            context.token.trackSelection.selectedMask;",
    )
    active_survivor_uses_cold_length = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "        const uint8_t incomingLength = mutation.nextActive == beforeActive\n"
        "            ? context.state.sequencer.pattern.length.get()\n"
        "            : context.state.tracks.track(mutation.nextActive).length.get();",
        "        const uint8_t incomingLength =\n"
        "            context.state.tracks.track(mutation.nextActive).length.get();",
    )
    widened_selection_capture_mask = mutate_pattern(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        r"(else\s+if\s*\(\s*action\s*==\s*Action::SequencerRemoveSelection.*?"
        r"plan\.affectedTrackMask\s*=\s*selectedMask\s*;)\s*"
        r"plan\.capturedTrackMask\s*=\s*static_cast<uint16_t>\s*\(.*?\)\s*;",
        r"\g<1>\n        plan.capturedTrackMask = beforeMask;",
    )
    reconstructive_selection_reset = mutate_pattern(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        r"(else\s+if\s*\(\s*action\s*==\s*Action::SequencerRemoveSelection.*?"
        r"plan\.affectedTrackMask\s*=\s*selectedMask\s*;)",
        r"\g<1>\n        plan.canonicalResetTrackMask = selectedMask;",
    )
    unconditional_selection_settlement = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "    if (plan.action == Action::SequencerRemoveSelection) {",
        "    if (true) {",
    )
    missing_track_presentation_reconciliation = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "reconcilePreparedSequencerActiveTrackPresentation();",
        "skipPreparedSequencerActiveTrackPresentation();",
    )
    missing_track_history_presentation_reconciliation = mutate_pattern(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        r"(\bCoreState::publishSequencerHistoryTraversal_\s*\([^)]*\)\s*\{.*?)"
        r"\breconcilePreparedSequencerActiveTrackPresentation\s*\(\s*\)",
        r"\g<1>skipPreparedSequencerActiveTrackPresentation()",
    )
    active_change_condition = (
        "result.descriptor.kind ==\n"
        "                   sequencer::SequencerHistoryActionKind::TrackStructure &&\n"
        "               activeTrackBefore != sequencerTracks.activeTrackIndex()"
    )
    unconditional_track_history_presentation = mutate(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        active_change_condition,
        "result.descriptor.kind ==\n"
        "                   sequencer::SequencerHistoryActionKind::TrackStructure",
    )
    missing_coupled_replay_preparation = mutate(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        "sequencerHistory.prepareStructureHistoryReplay(",
        "sequencerHistory.skipStructureHistoryReplayPreparation(",
    )
    legacy_coupled_activation_planning = mutate(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        "sequencerTrackActivations.planHistoryTransition(",
        "sequencerTrackActivations.prepareHistoryTransition(",
    )
    missing_coupled_atomic_arm = mutate(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        "sequencerTrackActivations.tryArmPlannedHistoryTransition(",
        "sequencerTrackActivations.skipArmPlannedHistoryTransition(",
    )
    compensating_coupled_replay = mutate(
        CORE_SEQUENCER_HISTORY_TRAVERSAL,
        ": sequencerHistory.redoWithResult(sequencerTracks, sequencer);",
        ": sequencerHistory.redoWithResult(sequencerTracks, sequencer);\n"
        "        (void)sequencerHistory.undoWithResult(sequencerTracks, sequencer);",
    )
    missing_macro_replay_prevalidation = mutate(
        "src/state/sequencer/SequencerHistory.cpp",
        "validateMacroTrackStructureHistoryReplay(pages, *macroStructure, after)",
        "skipMacroTrackStructureHistoryReplayValidation(pages, *macroStructure, after)",
    )
    generic_structure_replay_escape = mutate(
        "src/state/sequencer/SequencerHistory.cpp",
        "if (entry.scope == SequencerHistoryScope::Structure) return false;",
        "if (entry.scope == SequencerHistoryScope::Structure) return true;",
    )
    widened_structure_replay_handle = mutate(
        "src/state/sequencer/SequencerStructureHistory.hpp",
        "sizeof(SequencerPreparedStructureHistoryReplay) <= 256U",
        "sizeof(SequencerPreparedStructureHistoryReplay) <= 512U",
    )
    allocating_structure_replay_tail = mutate(
        "src/state/sequencer/SequencerHistory.cpp",
        "    result.descriptor = descriptorForEntry(entry);\n"
        "    commitPreparedHistoryStructureReplayState(bank, active, replay);",
        "    result.descriptor = descriptorForEntry(entry);\n"
        "    (void)prepareHistoryStructureReplayOwners(\n"
        "        *replay.targetSnapshot, bank.activeTrackIndex(), replay);\n"
        "    commitPreparedHistoryStructureReplayState(bank, active, replay);",
    )
    validation_before_draft_priority = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "    // Draft owns Track transition priority",
        "    if (latchedTargetTrack > TrackBank::TRACK_COUNT) {\n"
        "        return {Status::Invalid, {}};\n"
        "    }\n"
        "    // Draft owns Track transition priority",
    )
    incomplete_selection_intent = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        ".destinationMask = selection.destinationMask.get(),",
        ".destinationMask = 0U,",
    )
    bypassed_initial_topology_preflight = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "switch (validateInitialTopology(context, action)) {",
        "switch (InitialTopologyOutcome::Ready) {",
    )
    missing_presentation_capability_pregate = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "    if (!state.sharedTracks.\n"
        "            canReconcilePreparedSequencerActiveTrackPresentation()) {\n"
        "        return {Status::PublicationUnavailable, {}};\n"
        "    }",
        "    if (false) {\n"
        "        return {Status::PublicationUnavailable, {}};\n"
        "    }",
    )
    merged_track_hold_provenance = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
        "        SelectionRemove,\n",
        "        SelectionRemove = CurrentRemove,\n",
    )
    selection_press_uses_current_hold_path = mutate(
        SEQUENCER_STEP_HANDLER,
        "edit_workflow_.beginSelectionHoldAction(",
        "edit_workflow_.beginHoldAction(",
    )
    live_focus_first_track_remove_dispatch = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "    if (trackRemoveHoldPending()) {\n"
        "        const bool holdStillMatches = currentTrackRemoveHoldStillMatches();",
        "    if (navigation_focus_.get() ==\n"
        "        core::state::StructureNavigationFocus::TRACK) {\n"
        "        const bool holdStillMatches = currentTrackRemoveHoldStillMatches();",
    )
    incomplete_track_selection_hold_token = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
        "        uint16_t destinationMask = 0U;",
        "        uint16_t destinationMaskIgnored = 0U;",
    )
    missing_selection_hold_capture = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "                track_ui_.selection.scope.get(),",
        "                core::state::StructureSelectionScope::TRACK,",
    )
    missing_selection_hold_focus_revalidation = mutate_pattern(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        r"(SequencerStructureEditWorkflow::selectionTrackRemoveIntentMatches\s*"
        r"\([^)]*\)\s*const\s*\{\s*return\s+)"
        r"navigation_focus_\.get\s*\(\s*\)\s*==\s*"
        r"core::state::StructureNavigationFocus::TRACK\s*&&",
        r"\g<1>true &&",
    )
    missing_selection_hold_token_revalidation = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "           ) == token.flags &&",
        "           ) == 0U &&",
    )
    missing_current_hold_owner_revalidation = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "           currentActiveTrack() == targetTrack;",
        "           currentActiveTrack() < TrackBank::TRACK_COUNT;",
    )
    missing_current_post_boundary_revalidation = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "    if (track_ui_.hold.active() ||\n"
        "        !currentTrackRemoveIntentMatches(targetTrack)) {",
        "    if (track_ui_.hold.active()) {",
    )
    missing_selection_post_boundary_revalidation = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "    if (track_ui_.hold.active() ||\n"
        "        !selectionTrackRemoveIntentMatches(token, targetTrack)) {",
        "    if (track_ui_.hold.active()) {",
    )
    missing_current_rejected_hold_settlement = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::applyLatchedCurrentTrackShortPress() {\n"
        "    if (!currentTrackRemoveHoldStillMatches()) {\n"
        "        if (trackRemoveHoldOwnsSharedState()) track_ui_.hold.clear();",
        "SequencerStructureEditWorkflow::applyLatchedCurrentTrackShortPress() {\n"
        "    if (!currentTrackRemoveHoldStillMatches()) {",
    )
    missing_selection_rejected_hold_settlement = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "SequencerStructureEditWorkflow::applyLatchedTrackSelectionShortPress() {\n"
        "    if (!selectionTrackRemoveHoldStillMatches()) {\n"
        "        if (trackRemoveHoldOwnsSharedState()) track_ui_.hold.clear();",
        "SequencerStructureEditWorkflow::applyLatchedTrackSelectionShortPress() {\n"
        "    if (!selectionTrackRemoveHoldStillMatches()) {",
    )
    missing_selection_scope_entry_guard = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "            !track_ui_.selection.active.get() ||\n"
        "            track_ui_.selection.scope.get() !=\n"
        "                core::state::StructureSelectionScope::TRACK ||\n"
        "            track_ui_.previewAddSlot.get()) {",
        "            !track_ui_.selection.active.get() ||\n"
        "            track_ui_.previewAddSlot.get()) {",
    )
    missing_release_latch_eligibility = mutate(
        SEQUENCER_STEP_HANDLER,
        "return bottom_action_release_latch_.isArmed(",
        "return bottom_action_release_latch_.consume(",
    )
    missing_nav_press_hold_blocker = mutate_pattern(
        SEQUENCER_STEP_HANDLER,
        r"(\.button\s*\(\s*Config::ButtonID::NAV\s*\)\s*"
        r"\.press\s*\(\s*\).*?"
        r"!edit_workflow_\.trackPasteNavigationBlocked\s*\(\s*\))\s*&&\s*"
        r"!edit_workflow_\.trackRemoveNavigationBlocked\s*\(\s*\)",
        r"\g<1>",
    )
    live_recomputed_remove_target = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "const uint8_t latchedTarget = track_hold_target_;",
        "const uint8_t latchedTarget = currentActiveTrack();",
    )
    broad_remove_hold_release_block = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "return paste.buttonOwned || paste.gestureActive() || paste.detailVisible;",
        "return track_ui_.hold.active() || paste.buttonOwned || "
        "paste.gestureActive() || paste.detailVisible;",
    )
    missing_create_clipboard_wiring = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "        structure_clipboard_,\n"
        "        macro_pages_,",
        "        macro_pages_,\n"
        "        macro_pages_,",
    )
    missing_cold_track_preview_hold_guard = mutate(
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW,
        "    if (track_ui_.hold.active()) return;\n"
        "    track_ui_.syncPreviewTrack(activeTrack);",
        "    track_ui_.syncPreviewTrack(activeTrack);",
    )
    premature_create_preview_settlement = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "    return executeSequencerCreateTrackStructure({",
        "    track_ui_.previewAddSlot.set(false);\n"
        "    return executeSequencerCreateTrackStructure({",
    )
    premature_remove_preview_settlement = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "        const auto result = executeSequencerRemoveCurrentTrackStructure({",
        "        track_ui_.syncPreviewTrack(currentActiveTrack());\n"
        "        const auto result = executeSequencerRemoveCurrentTrackStructure({",
    )
    residual_track_create_boundary = mutate(
        SEQUENCER_STEP_HANDLER,
        "                if (!edit_workflow_.createPreviewedTrackStructure().settled()) {",
        "                if (!commitPatternHistoryBarrier(history_)) return;\n"
        "                if (!edit_workflow_.createPreviewedTrackStructure().settled()) {",
    )
    dangling_navigation_shared_facade = mutate(
        SEQUENCER_STEP_HANDLER,
        "          edit_workflow_.sharedTrackServices(),",
        "          state.sharedTracks,",
    )
    navigation_owns_duplicate_shared_facade = mutate(
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER,
        "    const SharedTrackDomainServices& shared_tracks_;",
        "    SharedTrackDomainServices shared_tracks_;",
    )
    navigation_state_refs_accepts_temporary_facade = mutate(
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER,
        "        std::reference_wrapper<const SharedTrackDomainServices> sharedTracks;",
        "        const SharedTrackDomainServices& sharedTracks;",
    )
    navigation_reacquires_create_owner = mutate(
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER,
        "        std::reference_wrapper<const SharedTrackDomainServices> sharedTracks;",
        "        std::reference_wrapper<const SharedTrackDomainServices> sharedTracks;\n"
        "        core::state::macro::MacroPagesState* macroPages = nullptr;",
    )
    split_shared_presentation_callback = mutate(
        SHARED_TRACK_DOMAIN_SERVICES_HEADER,
        "ReconcilePreparedTrackPresentationFn\n"
        "            reconcilePreparedTrackPresentation = nullptr;",
        "ReconcilePreparedMacroTrackTransferFn\n"
        "            reconcilePreparedMacroTrackTransfer = nullptr;",
    )
    widened_shared_facade_arm_abi = mutate(
        SHARED_TRACK_DOMAIN_SERVICES_HEADER,
        "sizeof(SharedTrackDomainServices) == 28U",
        "sizeof(SharedTrackDomainServices) == 32U",
    )
    copied_direct_context_state = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "    const SequencerDirectTrackStructureStateRefs& state;",
        "    SequencerDirectTrackStructureStateRefs state;",
    )
    widened_direct_context_arm_stack = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "sizeof(DirectContext) <= 160U",
        "sizeof(DirectContext) <= 216U",
    )
    widened_track_selection_hold_token = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
        "sizeof(TrackSelectionHoldToken) == 16U",
        "sizeof(TrackSelectionHoldToken) == 20U",
    )
    widened_direct_selection_intent = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "sizeof(SelectionIntentToken) == 16U",
        "sizeof(SelectionIntentToken) == 20U",
    )
    edit_drops_shared_facade_ownership = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
        "    SharedTrackDomainServices shared_tracks_;",
        "    const SharedTrackDomainServices& shared_tracks_;",
    )
    edit_state_refs_borrows_shared_facade = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
        "        SharedTrackDomainServices sharedTracks;",
        "        const SharedTrackDomainServices& sharedTracks;",
    )
    misrouted_sequencer_presentation_kind = mutate(
        SHARED_TRACK_DOMAIN_SERVICES,
        "        PreparedTrackPresentationKind::SequencerActiveTrack,\n"
        "        0U",
        "        PreparedTrackPresentationKind::MacroTrackTransfer,\n"
        "        0U",
    )
    duplicate_shared_presentation_slot = mutate(
        SHARED_TRACK_DOMAIN_SERVICES_HEADER,
        "            reconcilePreparedTrackPresentation = nullptr;",
        "            reconcilePreparedTrackPresentation = nullptr;\n"
        "        ReconcilePreparedTrackPresentationFn\n"
        "            duplicateTrackPresentation = nullptr;",
    )
    widened_direct_context_native_stack = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION,
        "sizeof(DirectContext) <= 168U",
        "sizeof(DirectContext) <= 176U",
    )
    navigation_reacquires_create_owner_by_reference = mutate(
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER,
        "        std::reference_wrapper<const SharedTrackDomainServices> sharedTracks;",
        "        std::reference_wrapper<const SharedTrackDomainServices> sharedTracks;\n"
        "        const core::state::StructureClipboardState& clipboard;",
    )
    dropped_paste_blocked_hold_flag = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "(pasteBlocked ? TRACK_SELECTION_HOLD_PASTE_BLOCKED : 0U)",
        "(pasteBlocked ? 0U : 0U)",
    )
    widened_navigation_workflow_arm_ram = mutate(
        PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER,
        "sizeof(SequencerStructureNavigationWorkflow) == 48U",
        "sizeof(SequencerStructureNavigationWorkflow) == 52U",
    )
    widened_edit_workflow_arm_ram = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER,
        "sizeof(SequencerStructureEditWorkflow) == 116U",
        "sizeof(SequencerStructureEditWorkflow) == 120U",
    )
    widened_step_handler_arm_psram = mutate(
        SEQUENCER_STEP_HANDLER_HEADER,
        "sizeof(SequencerStepHandler) == 256U",
        "sizeof(SequencerStepHandler) == 260U",
    )
    widened_direct_state_refs_arm_stack = mutate(
        DIRECT_TRACK_STRUCTURE_TRANSACTION_HEADER,
        "sizeof(SequencerDirectTrackStructureStateRefs) == 64U",
        "sizeof(SequencerDirectTrackStructureStateRefs) == 68U",
    )
    residual_remove_current_boundary = mutate(
        SEQUENCER_STEP_HANDLER,
        "#endif\n            edit_workflow_.applyCurrentStructureLongPress();",
        "#endif\n            if (!commitPatternHistoryBarrier(history_)) return;\n"
        "            edit_workflow_.applyCurrentStructureLongPress();",
    )
    added_tenth_page_action = mutate(
        PAGE_STRUCTURE_TRANSACTION_HEADER,
        "    PageSelectionDeleteOrDeepReset,\n};",
        "    PageSelectionDeleteOrDeepReset,\n"
        "    ExperimentalTenthAction,\n};",
    )
    miswired_page_selection_builder = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "buildSequencerPageSelectionPasteMutationPlan(",
        "buildSequencerPagePasteMutationPlan(",
    )
    miswired_page_paste_builder = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "buildSequencerPagePasteMutationPlan(",
        "buildSequencerPageSelectionPasteMutationPlan(",
    )
    miswired_step_paste_builder = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "buildSequencerStepPasteMutationPlan(",
        "buildSequencerFocusedStepResetMutationPlan(",
    )
    miswired_page_selection_reset_builder = mutate(
        PAGE_STRUCTURE_SELECTION_WORKFLOW,
        "buildSequencerPageSelectionResetMutationPlan(",
        "buildSequencerPageSelectionDeleteOrDeepResetMutationPlan(",
    )
    balanced_early_page_paste_guard = mutate_pattern(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        r"(\bSequencerStructureEditWorkflow::pasteCurrentPageAfterBoundary\s*"
        r"\([^)]*\)\s*\{)",
        r"\g<1>"
        "\n    if (!structure_clipboard_.hasSequencerPage()) {\n"
        "        return packPagePasteSettlement(PagePasteSettlement::Failed);\n"
        "    }",
    )
    escaped_page_insert_lease = dict(step_draft_fixture)
    escaped_page_insert_lease[PAGE_STRUCTURE_EDIT_WORKFLOW] += (
        "\nvoid injectedRawPageInsert() { insertPage(); }\n"
    )
    escaped_page_clear_lease = dict(step_draft_fixture)
    escaped_page_clear_lease[PAGE_STRUCTURE_EDIT_WORKFLOW] += (
        "\nvoid injectedVersionedPageClear() { clearStepRange(); }\n"
    )
    escaped_page_delete_lease = dict(step_draft_fixture)
    escaped_page_delete_lease[PAGE_STRUCTURE_EDIT_WORKFLOW] += (
        "\nvoid injectedVersionedPageDelete() { deletePage(); }\n"
    )
    allocating_page_compaction = dict(step_draft_fixture)
    allocating_page_compaction[PAGE_STRUCTURE_MUTATION_PLAN] += (
        "\nvoid injectedAllocatingCompaction() { compactSequencerGraph(); }\n"
    )
    ignored_page_graph_result = dict(step_draft_fixture)
    ignored_page_graph_result[PAGE_STRUCTURE_MUTATION_PLAN] += (
        "\nvoid injectedIgnoredGraphResult() {\n"
        "    seq::copyStepNodePayloadFromGraphUnversioned();\n"
        "}\n"
    )
    residual_selection_page_boundary = mutate(
        SEQUENCER_STEP_HANDLER,
        "#endif\n            edit_workflow_.pasteStructureSelection();",
        "#endif\n            if (!commitPatternHistoryBarrier(history_)) return;\n"
        "            edit_workflow_.pasteStructureSelection();",
    )
    residual_current_page_boundary = mutate(
        SEQUENCER_STEP_HANDLER,
        "#endif\n            edit_workflow_.pasteCurrentStructure();",
        "#endif\n            if (!commitPatternHistoryBarrier(history_)) return;\n"
        "            edit_workflow_.pasteCurrentStructure();",
    )
    residual_step_selection_boundary = mutate(
        SEQUENCER_STEP_HANDLER,
        "#endif\n            edit_workflow_.pasteStepSelection();",
        "#endif\n            if (!commitPatternHistoryBarrier(history_)) return;\n"
        "            edit_workflow_.pasteStepSelection();",
    )
    premature_step_selection_settlement = mutate(
        SEQUENCER_STEP_HANDLER,
        "        .then([this]() {\n"
        "            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);\n"
        "#if defined(MS_UX_RECORDER)\n"
        "            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = true;\n"
        "#endif\n            edit_workflow_.pasteStepSelection();",
        "        .then([this]() {\n"
        "            edit_workflow_.clearStepPastePreview();\n"
        "            edit_workflow_.clearHoldAction();\n"
        "            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);\n"
        "#if defined(MS_UX_RECORDER)\n"
        "            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = true;\n"
        "#endif\n            edit_workflow_.pasteStepSelection();",
    )
    duplicate_page_selection_dispatch = mutate(
        SEQUENCER_STEP_HANDLER,
        "            edit_workflow_.pasteStructureSelection();",
        "            edit_workflow_.pasteStructureSelection();\n"
        "            auto& duplicate_workflow = edit_workflow_;\n"
        "            duplicate_workflow.pasteStructureSelection();",
    )
    duplicate_current_page_dispatch = mutate(
        SEQUENCER_STEP_HANDLER,
        "            edit_workflow_.pasteCurrentStructure();",
        "            edit_workflow_.pasteCurrentStructure();\n"
        "            auto& duplicate_workflow = edit_workflow_;\n"
        "            duplicate_workflow.pasteCurrentStructure();",
    )
    direct_page_selection_boundary = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "    constexpr auto action = Action::PageSelectionPaste;",
        "    auto& duplicate_history = history_;\n"
        "    (void)duplicate_history.commitCoalescedPatternEditOutcome();\n"
        "    constexpr auto action = Action::PageSelectionPaste;",
    )
    direct_current_page_boundary = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "    constexpr auto action = Action::PagePaste;",
        "    auto& duplicate_history = history_;\n"
        "    (void)duplicate_history.commitCoalescedPatternEditOutcome();\n"
        "    constexpr auto action = Action::PagePaste;",
    )
    direct_step_paste_boundary = mutate(
        PAGE_STRUCTURE_EDIT_WORKFLOW,
        "    constexpr auto action = Action::StepPaste;",
        "    auto& duplicate_history = history_;\n"
        "    (void)duplicate_history.commitCoalescedPatternEditOutcome();\n"
        "    constexpr auto action = Action::StepPaste;",
    )
    checks = (
        (
            not extmem_lifetime_contract_errors(extmem_fixture),
            "valid strict EXTMEM lifetime contract is accepted",
        ),
        (
            bool(extmem_lifetime_contract_errors(wrong_strict_allocate_pool)),
            "mismatched strict EXTMEM allocation pool is rejected",
        ),
        (
            bool(extmem_lifetime_contract_errors(wrong_strict_free_pool)),
            "mismatched strict EXTMEM free pool is rejected",
        ),
        (
            bool(extmem_lifetime_contract_errors(unpaired_extmem_deleter)),
            "EXTMEM deleter bypassing freeExtmemStrict is rejected",
        ),
        (
            bool(extmem_lifetime_contract_errors(
                pending_apply_non_strict_allocation
            )),
            "PendingApply non-strict allocation is rejected",
        ),
        (
            bool(extmem_lifetime_contract_errors(pending_apply_non_strict_free)),
            "PendingApply non-strict free is rejected",
        ),
        (
            bool(extmem_lifetime_contract_errors(pending_apply_default_deleter)),
            "PendingApply default deleter is rejected",
        ),
        (
            bool(extmem_lifetime_contract_errors(pending_apply_not_adopted)),
            "unowned PendingApply allocation is rejected",
        ),
        (
            bool(extmem_lifetime_contract_errors(escaped_strict_extmem_call)),
            "strict EXTMEM primitive escaping canonical owners is rejected",
        ),
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
        (
            not step_draft_transition_contract_errors(step_draft_fixture),
            "valid Step-draft transition contract is accepted",
        ),
        (
            miswired_create_action[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                miswired_create_action
            )),
            "miswired SequencerCreate action is rejected",
        ),
        (
            miswired_remove_action[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                miswired_remove_action
            )),
            "miswired SequencerRemoveCurrent action is rejected",
        ),
        (
            miswired_selection_action[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                miswired_selection_action
            )),
            "miswired SequencerRemoveSelection action is rejected",
        ),
        (
            miswired_macro_create_action[
                MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                miswired_macro_create_action
            )),
            "miswired MacroCreate action is rejected",
        ),
        (
            bypassed_macro_reset_service[MACRO_STRUCTURE_DOMAIN_SERVICES]
            != step_draft_fixture[MACRO_STRUCTURE_DOMAIN_SERVICES]
            and bool(step_draft_transition_contract_errors(
                bypassed_macro_reset_service
            )),
            "Macro Reset service bypassing its typed adapter is rejected",
        ),
        (
            restored_macro_track_rollback[MACRO_STRUCTURE_DOMAIN_SERVICES]
            != step_draft_fixture[MACRO_STRUCTURE_DOMAIN_SERVICES]
            and bool(step_draft_transition_contract_errors(
                restored_macro_track_rollback
            )),
            "restored Macro Track rollback route is rejected",
        ),
        (
            widened_macro_direct_context_arm_stack[
                MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                widened_macro_direct_context_arm_stack
            )),
            "widened Macro direct context ARM stack ceiling is rejected",
        ),
        (
            missing_macro_clipboard_prevalidation[
                MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[MACRO_DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                missing_macro_clipboard_prevalidation
            )),
            "Macro Paste without pre-allocation clipboard validation is rejected",
        ),
        (
            lost_macro_navigation_suppression[
                CORE_SEQUENCER_HISTORY_RECORDING
            ] != step_draft_fixture[CORE_SEQUENCER_HISTORY_RECORDING]
            and bool(step_draft_transition_contract_errors(
                lost_macro_navigation_suppression
            )),
            "direct Macro commit publishing Project navigation is rejected",
        ),
        (
            broken_macro_paste_preview_settlement[MACRO_STRUCTURE_WORKFLOW]
            != step_draft_fixture[MACRO_STRUCTURE_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                broken_macro_paste_preview_settlement
            )),
            "Macro Track Paste without success-owned preview settlement is rejected",
        ),
        (
            miswired_create_workflow[PAGE_STRUCTURE_EDIT_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                miswired_create_workflow
            )),
            "miswired Track Create workflow is rejected",
        ),
        (
            miswired_remove_workflow[PAGE_STRUCTURE_EDIT_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                miswired_remove_workflow
            )),
            "miswired RemoveCurrent workflow is rejected",
        ),
        (
            miswired_selection_workflow[PAGE_STRUCTURE_SELECTION_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_SELECTION_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                miswired_selection_workflow
            )),
            "miswired SelectionRemove workflow is rejected",
        ),
        (
            ignored_selection_result[PAGE_STRUCTURE_SELECTION_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_SELECTION_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                ignored_selection_result
            )),
            "ignored SelectionRemove typed result is rejected",
        ),
        (
            premature_selection_settlement[
                PAGE_STRUCTURE_SELECTION_WORKFLOW
            ] != step_draft_fixture[PAGE_STRUCTURE_SELECTION_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                premature_selection_settlement
            )),
            "premature SelectionRemove UI settlement is rejected",
        ),
        (
            restored_selection_raw_history[
                PAGE_STRUCTURE_SELECTION_WORKFLOW
            ] != step_draft_fixture[PAGE_STRUCTURE_SELECTION_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                restored_selection_raw_history
            )),
            "restored SelectionRemove raw History path is rejected",
        ),
        (
            residual_selection_boundary[PAGE_STRUCTURE_EDIT_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                residual_selection_boundary
            )),
            "residual SelectionRemove caller boundary is rejected",
        ),
        (
            incomplete_selection_remove_intent[
                DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                incomplete_selection_remove_intent
            )),
            "incomplete SelectionRemove direct intent is rejected",
        ),
        (
            unsanitized_selection_topology[
                DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                unsanitized_selection_topology
            )),
            "unsanitized SelectionRemove topology is rejected",
        ),
        (
            active_survivor_uses_cold_length[
                DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                active_survivor_uses_cold_length
            )),
            "SelectionRemove active-survivor cold-length focus clamp is rejected",
        ),
        (
            widened_selection_capture_mask[
                DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                widened_selection_capture_mask
            )),
            "widened SelectionRemove capture mask is rejected",
        ),
        (
            reconstructive_selection_reset[
                DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                reconstructive_selection_reset
            )),
            "reconstructive SelectionRemove canonical reset is rejected",
        ),
        (
            unconditional_selection_settlement[
                DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                unconditional_selection_settlement
            )),
            "unconditional SelectionRemove settlement is rejected",
        ),
        (
            missing_track_presentation_reconciliation[
                DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                missing_track_presentation_reconciliation
            )),
            "missing committed Track presentation reconciliation is rejected",
        ),
        (
            missing_track_history_presentation_reconciliation[
                CORE_SEQUENCER_HISTORY_TRAVERSAL
            ] != step_draft_fixture[CORE_SEQUENCER_HISTORY_TRAVERSAL]
            and bool(step_draft_transition_contract_errors(
                missing_track_history_presentation_reconciliation
            )),
            "missing Track Structure History presentation reconciliation is rejected",
        ),
        (
            unconditional_track_history_presentation[
                CORE_SEQUENCER_HISTORY_TRAVERSAL
            ] != step_draft_fixture[CORE_SEQUENCER_HISTORY_TRAVERSAL]
            and bool(step_draft_transition_contract_errors(
                unconditional_track_history_presentation
            )),
            "unconditional Track Structure History presentation reconciliation is rejected",
        ),
        (
            missing_coupled_replay_preparation[
                CORE_SEQUENCER_HISTORY_TRAVERSAL
            ] != step_draft_fixture[CORE_SEQUENCER_HISTORY_TRAVERSAL]
            and bool(step_draft_transition_contract_errors(
                missing_coupled_replay_preparation
            )),
            "missing coupled Structure replay preparation is rejected",
        ),
        (
            legacy_coupled_activation_planning[
                CORE_SEQUENCER_HISTORY_TRAVERSAL
            ] != step_draft_fixture[CORE_SEQUENCER_HISTORY_TRAVERSAL]
            and bool(step_draft_transition_contract_errors(
                legacy_coupled_activation_planning
            )),
            "legacy mutating coupled activation preparation is rejected",
        ),
        (
            missing_coupled_atomic_arm[
                CORE_SEQUENCER_HISTORY_TRAVERSAL
            ] != step_draft_fixture[CORE_SEQUENCER_HISTORY_TRAVERSAL]
            and bool(step_draft_transition_contract_errors(
                missing_coupled_atomic_arm
            )),
            "missing coupled atomic activation arm is rejected",
        ),
        (
            compensating_coupled_replay[
                CORE_SEQUENCER_HISTORY_TRAVERSAL
            ] != step_draft_fixture[CORE_SEQUENCER_HISTORY_TRAVERSAL]
            and bool(step_draft_transition_contract_errors(
                compensating_coupled_replay
            )),
            "compensating opposite coupled replay is rejected",
        ),
        (
            missing_macro_replay_prevalidation[
                "src/state/sequencer/SequencerHistory.cpp"
            ] != step_draft_fixture["src/state/sequencer/SequencerHistory.cpp"]
            and bool(step_draft_transition_contract_errors(
                missing_macro_replay_prevalidation
            )),
            "missing Macro replay prevalidation is rejected",
        ),
        (
            generic_structure_replay_escape[
                "src/state/sequencer/SequencerHistory.cpp"
            ] != step_draft_fixture["src/state/sequencer/SequencerHistory.cpp"]
            and bool(step_draft_transition_contract_errors(
                generic_structure_replay_escape
            )),
            "generic Structure replay escape is rejected",
        ),
        (
            widened_structure_replay_handle[
                "src/state/sequencer/SequencerStructureHistory.hpp"
            ] != step_draft_fixture[
                "src/state/sequencer/SequencerStructureHistory.hpp"
            ]
            and bool(step_draft_transition_contract_errors(
                widened_structure_replay_handle
            )),
            "widened prepared Structure replay handle is rejected",
        ),
        (
            allocating_structure_replay_tail[
                "src/state/sequencer/SequencerHistory.cpp"
            ] != step_draft_fixture["src/state/sequencer/SequencerHistory.cpp"]
            and bool(step_draft_transition_contract_errors(
                allocating_structure_replay_tail
            )),
            "allocation in prepared Structure replay tail is rejected",
        ),
        (
            validation_before_draft_priority[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                validation_before_draft_priority
            )),
            "adapter-local validation before Draft rejection is rejected",
        ),
        (
            incomplete_selection_intent[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                incomplete_selection_intent
            )),
            "incomplete direct Track selection-intent capture is rejected",
        ),
        (
            bypassed_initial_topology_preflight[
                DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                bypassed_initial_topology_preflight
            )),
            "bypassed direct Track initial-topology preflight is rejected",
        ),
        (
            missing_presentation_capability_pregate[
                DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                missing_presentation_capability_pregate
            )),
            "missing direct Track presentation-capability pre-gate is rejected",
        ),
        (
            merged_track_hold_provenance[PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER]
            != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                merged_track_hold_provenance
            )),
            "merged CurrentRemove/SelectionRemove provenance is rejected",
        ),
        (
            selection_press_uses_current_hold_path[SEQUENCER_STEP_HANDLER]
            != step_draft_fixture[SEQUENCER_STEP_HANDLER]
            and bool(step_draft_transition_contract_errors(
                selection_press_uses_current_hold_path
            )),
            "Selection press routed through CurrentRemove is rejected",
        ),
        (
            live_focus_first_track_remove_dispatch[
                PAGE_STRUCTURE_EDIT_WORKFLOW
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                live_focus_first_track_remove_dispatch
            )),
            "live-focus-first Track Remove dispatch is rejected",
        ),
        (
            incomplete_track_selection_hold_token[
                PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                incomplete_track_selection_hold_token
            )),
            "incomplete SelectionRemove hold token storage is rejected",
        ),
        (
            missing_selection_hold_capture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_selection_hold_capture
            )),
            "incomplete SelectionRemove hold token capture is rejected",
        ),
        (
            missing_selection_hold_focus_revalidation[
                PAGE_STRUCTURE_EDIT_WORKFLOW
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_selection_hold_focus_revalidation
            )),
            "missing SelectionRemove focus revalidation is rejected",
        ),
        (
            missing_selection_hold_token_revalidation[
                PAGE_STRUCTURE_EDIT_WORKFLOW
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_selection_hold_token_revalidation
            )),
            "missing SelectionRemove exact-token revalidation is rejected",
        ),
        (
            missing_current_hold_owner_revalidation[
                PAGE_STRUCTURE_EDIT_WORKFLOW
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_current_hold_owner_revalidation
            )),
            "missing CurrentRemove owner revalidation is rejected",
        ),
        (
            missing_current_post_boundary_revalidation[
                PAGE_STRUCTURE_EDIT_WORKFLOW
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_current_post_boundary_revalidation
            )),
            "missing Current Track post-boundary target revalidation is rejected",
        ),
        (
            missing_selection_post_boundary_revalidation[
                PAGE_STRUCTURE_EDIT_WORKFLOW
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_selection_post_boundary_revalidation
            )),
            "missing SelectionRemove post-boundary token revalidation is rejected",
        ),
        (
            missing_current_rejected_hold_settlement[
                PAGE_STRUCTURE_EDIT_WORKFLOW
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_current_rejected_hold_settlement
            )),
            "CurrentRemove rejection that leaves its owned hold active is rejected",
        ),
        (
            missing_selection_rejected_hold_settlement[
                PAGE_STRUCTURE_EDIT_WORKFLOW
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_selection_rejected_hold_settlement
            )),
            "SelectionRemove rejection that leaves its owned hold active is rejected",
        ),
        (
            missing_selection_scope_entry_guard[
                PAGE_STRUCTURE_EDIT_WORKFLOW
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_selection_scope_entry_guard
            )),
            "missing SelectionRemove initial scope guard is rejected",
        ),
        (
            missing_release_latch_eligibility[SEQUENCER_STEP_HANDLER]
            != step_draft_fixture[SEQUENCER_STEP_HANDLER]
            and bool(step_draft_transition_contract_errors(
                missing_release_latch_eligibility
            )),
            "BottomLeft release route without armed-latch eligibility is rejected",
        ),
        (
            missing_nav_press_hold_blocker[SEQUENCER_STEP_HANDLER]
            != step_draft_fixture[SEQUENCER_STEP_HANDLER]
            and bool(step_draft_transition_contract_errors(
                missing_nav_press_hold_blocker
            )),
            "new NAV selector press admitted during Track Remove is rejected",
        ),
        (
            live_recomputed_remove_target[PAGE_STRUCTURE_EDIT_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                live_recomputed_remove_target
            )),
            "live-recomputed Remove hold target is rejected",
        ),
        (
            broad_remove_hold_release_block[PAGE_STRUCTURE_EDIT_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                broad_remove_hold_release_block
            )),
            "Remove hold broadening paste-owned release blocking is rejected",
        ),
        (
            missing_create_clipboard_wiring[PAGE_STRUCTURE_EDIT_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_create_clipboard_wiring
            )),
            "missing Track Create clipboard intent wiring is rejected",
        ),
        (
            missing_cold_track_preview_hold_guard[
                PAGE_STRUCTURE_NAVIGATION_WORKFLOW
            ] != step_draft_fixture[PAGE_STRUCTURE_NAVIGATION_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_cold_track_preview_hold_guard
            )),
            "missing cold active-Track preview hold guard is rejected",
        ),
        (
            premature_create_preview_settlement[
                PAGE_STRUCTURE_EDIT_WORKFLOW
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                premature_create_preview_settlement
            )),
            "premature Track Create preview settlement is rejected",
        ),
        (
            premature_remove_preview_settlement[PAGE_STRUCTURE_EDIT_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                premature_remove_preview_settlement
            )),
            "premature RemoveCurrent preview settlement is rejected",
        ),
        (
            residual_track_create_boundary[SEQUENCER_STEP_HANDLER]
            != step_draft_fixture[SEQUENCER_STEP_HANDLER]
            and bool(step_draft_transition_contract_errors(
                residual_track_create_boundary
            )),
            "residual Track Create caller barrier is rejected",
        ),
        (
            dangling_navigation_shared_facade[SEQUENCER_STEP_HANDLER]
            != step_draft_fixture[SEQUENCER_STEP_HANDLER]
            and bool(step_draft_transition_contract_errors(
                dangling_navigation_shared_facade
            )),
            "dangling Navigation shared Track facade reference is rejected",
        ),
        (
            navigation_owns_duplicate_shared_facade[
                PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER
            ] != step_draft_fixture[PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                navigation_owns_duplicate_shared_facade
            )),
            "duplicate Navigation shared Track facade ownership is rejected",
        ),
        (
            navigation_state_refs_accepts_temporary_facade[
                PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER
            ] != step_draft_fixture[PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                navigation_state_refs_accepts_temporary_facade
            )),
            "temporary-capable Navigation shared facade references are rejected",
        ),
        (
            navigation_reacquires_create_owner[
                PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER
            ] != step_draft_fixture[PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                navigation_reacquires_create_owner
            )),
            "Navigation reacquisition of Track Create ownership is rejected",
        ),
        (
            split_shared_presentation_callback[
                SHARED_TRACK_DOMAIN_SERVICES_HEADER
            ] != step_draft_fixture[SHARED_TRACK_DOMAIN_SERVICES_HEADER]
            and bool(step_draft_transition_contract_errors(
                split_shared_presentation_callback
            )),
            "split shared Track presentation callbacks are rejected",
        ),
        (
            widened_shared_facade_arm_abi[
                SHARED_TRACK_DOMAIN_SERVICES_HEADER
            ] != step_draft_fixture[SHARED_TRACK_DOMAIN_SERVICES_HEADER]
            and bool(step_draft_transition_contract_errors(
                widened_shared_facade_arm_abi
            )),
            "widened shared Track facade ARM ABI is rejected",
        ),
        (
            copied_direct_context_state[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                copied_direct_context_state
            )),
            "copied direct Track context StateRefs are rejected",
        ),
        (
            widened_direct_context_arm_stack[
                DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                widened_direct_context_arm_stack
            )),
            "widened direct Track context ARM stack ceiling is rejected",
        ),
        (
            widened_track_selection_hold_token[
                PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                widened_track_selection_hold_token
            )),
            "widened Track Selection hold token ABI is rejected",
        ),
        (
            widened_direct_selection_intent[
                DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                widened_direct_selection_intent
            )),
            "widened direct Selection intent ABI is rejected",
        ),
        (
            edit_drops_shared_facade_ownership[
                PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                edit_drops_shared_facade_ownership
            )),
            "Edit dropping shared Track facade ownership is rejected",
        ),
        (
            edit_state_refs_borrows_shared_facade[
                PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                edit_state_refs_borrows_shared_facade
            )),
            "Edit StateRefs borrowing a temporary shared facade is rejected",
        ),
        (
            misrouted_sequencer_presentation_kind[
                SHARED_TRACK_DOMAIN_SERVICES
            ] != step_draft_fixture[SHARED_TRACK_DOMAIN_SERVICES]
            and bool(step_draft_transition_contract_errors(
                misrouted_sequencer_presentation_kind
            )),
            "misrouted Sequencer presentation kind is rejected",
        ),
        (
            duplicate_shared_presentation_slot[
                SHARED_TRACK_DOMAIN_SERVICES_HEADER
            ] != step_draft_fixture[SHARED_TRACK_DOMAIN_SERVICES_HEADER]
            and bool(step_draft_transition_contract_errors(
                duplicate_shared_presentation_slot
            )),
            "duplicate shared Track presentation callback slot is rejected",
        ),
        (
            widened_direct_context_native_stack[
                DIRECT_TRACK_STRUCTURE_TRANSACTION
            ] != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION]
            and bool(step_draft_transition_contract_errors(
                widened_direct_context_native_stack
            )),
            "widened direct Track context native stack ceiling is rejected",
        ),
        (
            navigation_reacquires_create_owner_by_reference[
                PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER
            ] != step_draft_fixture[PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                navigation_reacquires_create_owner_by_reference
            )),
            "Navigation reacquisition of Create ownership by reference is rejected",
        ),
        (
            dropped_paste_blocked_hold_flag[PAGE_STRUCTURE_EDIT_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                dropped_paste_blocked_hold_flag
            )),
            "dropped SelectionRemove paste-blocked flag is rejected",
        ),
        (
            widened_navigation_workflow_arm_ram[
                PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER
            ] != step_draft_fixture[PAGE_STRUCTURE_NAVIGATION_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                widened_navigation_workflow_arm_ram
            )),
            "widened Navigation workflow ARM RAM lock is rejected",
        ),
        (
            widened_edit_workflow_arm_ram[
                PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER
            ] != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                widened_edit_workflow_arm_ram
            )),
            "widened Edit workflow ARM RAM lock is rejected",
        ),
        (
            widened_step_handler_arm_psram[SEQUENCER_STEP_HANDLER_HEADER]
            != step_draft_fixture[SEQUENCER_STEP_HANDLER_HEADER]
            and bool(step_draft_transition_contract_errors(
                widened_step_handler_arm_psram
            )),
            "widened StepHandler ARM PSRAM lock is rejected",
        ),
        (
            widened_direct_state_refs_arm_stack[
                DIRECT_TRACK_STRUCTURE_TRANSACTION_HEADER
            ] != step_draft_fixture[DIRECT_TRACK_STRUCTURE_TRANSACTION_HEADER]
            and bool(step_draft_transition_contract_errors(
                widened_direct_state_refs_arm_stack
            )),
            "widened direct StateRefs ARM stack lock is rejected",
        ),
        (
            residual_remove_current_boundary[SEQUENCER_STEP_HANDLER]
            != step_draft_fixture[SEQUENCER_STEP_HANDLER]
            and bool(step_draft_transition_contract_errors(
                residual_remove_current_boundary
            )),
            "residual RemoveCurrent caller barrier is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(changed_label)),
            "changed Step-draft frozen label is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(missing_history_guard)),
            "missing production HISTORY guard is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(restored_deep_guard)),
            "restored deep FullBank guard is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(missing_structure_guard)),
            "missing Page STRUCTURE_EDIT guard is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(duplicate_page_boundary)),
            "duplicate Page Pattern boundary is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(missing_page_revalidation)),
            "missing Page plan revalidation is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(public_page_lifecycle)),
            "public Page lifecycle primitives are rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(nonfatal_page_abort)),
            "non-fatal armed Page abort is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(nonfatal_page_commit)),
            "non-fatal Page commit NoPending is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(nonfatal_invalid_seal)),
            "non-fatal invalid Page seal outcome is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(removed_page_trap)),
            "removed Page invariant trap is rejected",
        ),
        (
            added_tenth_page_action[PAGE_STRUCTURE_TRANSACTION_HEADER]
            != step_draft_fixture[PAGE_STRUCTURE_TRANSACTION_HEADER]
            and bool(step_draft_transition_contract_errors(
                added_tenth_page_action
            )),
            "a tenth live Page Structure action is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(restored_page_paste_raw)),
            "restored raw Page paste helper is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(restored_page_selection_raw)),
            "restored raw Page-selection paste helper is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(restored_page_create_raw)),
            "restored raw Page-create helper is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(
                restored_prepared_page_create
            )),
            "restored prepared Navigation PageCreate symbol is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(
                restored_sequencer_page_add_signal
            )),
            "restored Sequencer Page add-preview signal is rejected",
        ),
        (
            missing_pattern_editor_add_rejection[SEQUENCER_STEP_HANDLER]
            != step_draft_fixture[SEQUENCER_STEP_HANDLER]
            and bool(step_draft_transition_contract_errors(
                missing_pattern_editor_add_rejection
            )),
            "Pattern Editor release without add-provenance rejection is rejected",
        ),
        (
            broadened_nav_add_capture[SEQUENCER_STEP_HANDLER]
            != step_draft_fixture[SEQUENCER_STEP_HANDLER]
            and bool(step_draft_transition_contract_errors(
                broadened_nav_add_capture
            )),
            "NAV add provenance outside Track focus is rejected",
        ),
        (
            missing_step_target_capture[SEQUENCER_STEP_HANDLER]
            != step_draft_fixture[SEQUENCER_STEP_HANDLER]
            and bool(step_draft_transition_contract_errors(
                missing_step_target_capture
            )),
            "NAV selector without exact Step target capture is rejected",
        ),
        (
            missing_step_release_revalidation[SEQUENCER_STEP_HANDLER]
            != step_draft_fixture[SEQUENCER_STEP_HANDLER]
            and bool(step_draft_transition_contract_errors(
                missing_step_release_revalidation
            )),
            "Step Editor release without target revalidation is rejected",
        ),
        (
            truncated_selector_target[CONTEXT_SELECTOR_WORKFLOW]
            != step_draft_fixture[CONTEXT_SELECTOR_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                truncated_selector_target
            )),
            "truncated selector target provenance is rejected",
        ),
        (
            missing_hidden_selector_target_clear[CONTEXT_SELECTOR_WORKFLOW]
            != step_draft_fixture[CONTEXT_SELECTOR_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_hidden_selector_target_clear
            )),
            "hidden selector release retaining target provenance is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(
                restored_selector_feedback
            )),
            "restored unproducible selector feedback is rejected",
        ),
        (
            widened_selector_gesture[PRESS_HOLD_TURN_RELEASE_GESTURE_HEADER]
            != step_draft_fixture[PRESS_HOLD_TURN_RELEASE_GESTURE_HEADER]
            and bool(step_draft_transition_contract_errors(
                widened_selector_gesture
            )),
            "widened selector gesture storage is rejected",
        ),
        (
            widened_context_selector_arm_ram[CONTEXT_SELECTOR_WORKFLOW_HEADER]
            != step_draft_fixture[CONTEXT_SELECTOR_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                widened_context_selector_arm_ram
            )),
            "widened context selector ARM RAM lock is rejected",
        ),
        (
            unguarded_macro_track_preview_sync[MACRO_STRUCTURE_WORKFLOW]
            != step_draft_fixture[MACRO_STRUCTURE_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                unguarded_macro_track_preview_sync
            )),
            "Macro subscriber rewriting a held Track preview is rejected",
        ),
        (
            missing_macro_short_clear_provenance[MACRO_PERFORMANCE_HANDLER]
            != step_draft_fixture[MACRO_PERFORMANCE_HANDLER]
            and bool(step_draft_transition_contract_errors(
                missing_macro_short_clear_provenance
            )),
            "Macro short Clear without exact hold provenance is rejected",
        ),
        (
            missing_macro_short_copy_provenance[MACRO_PERFORMANCE_HANDLER]
            != step_draft_fixture[MACRO_PERFORMANCE_HANDLER]
            and bool(step_draft_transition_contract_errors(
                missing_macro_short_copy_provenance
            )),
            "Macro short Copy without exact hold provenance is rejected",
        ),
        (
            missing_macro_hold_acquisition_identity[MACRO_STRUCTURE_WORKFLOW]
            != step_draft_fixture[MACRO_STRUCTURE_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_macro_hold_acquisition_identity
            )),
            "Macro hold settlement without acquisition identity is rejected",
        ),
        (
            missing_macro_external_clear_reconcile[MACRO_STRUCTURE_WORKFLOW]
            != step_draft_fixture[MACRO_STRUCTURE_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_macro_external_clear_reconcile
            )),
            "Macro release after external hold clear without preview reconciliation is rejected",
        ),
        (
            missing_macro_nonvisual_press_capture[MACRO_PERFORMANCE_HANDLER]
            != step_draft_fixture[MACRO_PERFORMANCE_HANDLER]
            and bool(step_draft_transition_contract_errors(
                missing_macro_nonvisual_press_capture
            )),
            "Macro short press without exact non-visual capture is rejected",
        ),
        (
            missing_macro_captured_release_routing[MACRO_PERFORMANCE_HANDLER]
            != step_draft_fixture[MACRO_PERFORMANCE_HANDLER]
            and bool(step_draft_transition_contract_errors(
                missing_macro_captured_release_routing
            )),
            "Macro release routing that drops non-visual capture is rejected",
        ),
        (
            missing_macro_chord_exclusion[MACRO_STRUCTURE_WORKFLOW]
            != step_draft_fixture[MACRO_STRUCTURE_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                missing_macro_chord_exclusion
            )),
            "Macro cross-button provenance replacement is rejected",
        ),
        (
            consumed_macro_mismatched_release[MACRO_STRUCTURE_WORKFLOW]
            != step_draft_fixture[MACRO_STRUCTURE_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                consumed_macro_mismatched_release
            )),
            "Macro mismatched release consuming another action is rejected",
        ),
        (
            missing_structure_hold_acquisition_id[STRUCTURE_NAVIGATION_STATE]
            != step_draft_fixture[STRUCTURE_NAVIGATION_STATE]
            and bool(step_draft_transition_contract_errors(
                missing_structure_hold_acquisition_id
            )),
            "Structure hold without a unique acquisition ID is rejected",
        ),
        (
            widened_structure_hold_progress_signal[
                STRUCTURE_NAVIGATION_STATE_HEADER
            ] != step_draft_fixture[STRUCTURE_NAVIGATION_STATE_HEADER]
            and bool(step_draft_transition_contract_errors(
                widened_structure_hold_progress_signal
            )),
            "widened Structure hold progress signal is rejected",
        ),
        (
            widened_macro_hold_target[MACRO_STRUCTURE_WORKFLOW_HEADER]
            != step_draft_fixture[MACRO_STRUCTURE_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                widened_macro_hold_target
            )),
            "widened Macro hold target ARM RAM lock is rejected",
        ),
        (
            widened_macro_structure_workflow[MACRO_STRUCTURE_WORKFLOW_HEADER]
            != step_draft_fixture[MACRO_STRUCTURE_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                widened_macro_structure_workflow
            )),
            "widened Macro Structure workflow ARM PSRAM lock is rejected",
        ),
        (
            restored_public_macro_structure_helper[
                MACRO_STRUCTURE_WORKFLOW_HEADER
            ] != step_draft_fixture[MACRO_STRUCTURE_WORKFLOW_HEADER]
            and bool(step_draft_transition_contract_errors(
                restored_public_macro_structure_helper
            )),
            "restored public Macro implementation helper is rejected",
        ),
        (
            extra_track_hold_progress_observer[MACRO_VIEW]
            != step_draft_fixture[MACRO_VIEW]
            and bool(step_draft_transition_contract_errors(
                extra_track_hold_progress_observer
            )),
            "third Track hold progress observer is rejected",
        ),
        (
            stale_context_selection_provenance[CONTEXT_SELECTOR_WORKFLOW]
            != step_draft_fixture[CONTEXT_SELECTOR_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                stale_context_selection_provenance
            )),
            "stale selector provenance after selection transfer is rejected",
        ),
        (
            restored_selector_dead_hold[CONTEXT_SELECTOR_WORKFLOW]
            != step_draft_fixture[CONTEXT_SELECTOR_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                restored_selector_dead_hold
            )),
            "dead selector held state before cancellation is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(
                restored_bulk_selection_cancel
            )),
            "caller-zero bulk selection cancellation is rejected",
        ),
        (
            widened_sequencer_header_watcher[SEQUENCER_VIEW_HEADER]
            != step_draft_fixture[SEQUENCER_VIEW_HEADER]
            and bool(step_draft_transition_contract_errors(
                widened_sequencer_header_watcher
            )),
            "widened Sequencer header watcher lock is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(restored_page_ensure_raw)),
            "restored raw Page-ensure helper is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(restored_step_paste_raw)),
            "restored raw Step-paste helper is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(restored_step_reset_raw)),
            "restored raw Step-reset helper is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(
                restored_page_selection_delete_raw
            )),
            "restored raw Page-selection delete helper is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(
                miswired_page_selection_builder
            )),
            "miswired PageSelectionPaste builder is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(miswired_page_paste_builder)),
            "miswired PagePaste builder is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(miswired_step_paste_builder)),
            "miswired StepPaste builder is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(
                miswired_page_selection_reset_builder
            )),
            "miswired PageSelectionReset builder is rejected",
        ),
        (
            balanced_early_page_paste_guard[PAGE_STRUCTURE_EDIT_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and not step_draft_transition_contract_errors(
                balanced_early_page_paste_guard
            ),
            "balanced PagePaste helper with an early block is accepted",
        ),
        (
            bool(step_draft_transition_contract_errors(escaped_page_insert_lease)),
            "test-leased Page insertion escaping SnapshotOps is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(escaped_page_clear_lease)),
            "test-leased versioned Page clear escaping SnapshotOps is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(escaped_page_delete_lease)),
            "test-leased versioned Page delete escaping SnapshotOps is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(allocating_page_compaction)),
            "allocating Page Graph compaction is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(ignored_page_graph_result)),
            "ignored prepared Graph mutation result is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(
                residual_selection_page_boundary
            )),
            "residual PageSelectionPaste caller boundary is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(
                residual_current_page_boundary
            )),
            "residual current Page/Step paste caller boundary is rejected",
        ),
        (
            bool(step_draft_transition_contract_errors(
                residual_step_selection_boundary
            )),
            "residual StepSelectionPaste caller boundary is rejected",
        ),
        (
            premature_step_selection_settlement[SEQUENCER_STEP_HANDLER]
            != step_draft_fixture[SEQUENCER_STEP_HANDLER]
            and bool(step_draft_transition_contract_errors(
                premature_step_selection_settlement
            )),
            "premature StepSelectionPaste UI settlement is rejected",
        ),
        (
            duplicate_page_selection_dispatch[SEQUENCER_STEP_HANDLER]
            != step_draft_fixture[SEQUENCER_STEP_HANDLER]
            and bool(step_draft_transition_contract_errors(
                duplicate_page_selection_dispatch
            )),
            "aliased duplicate PageSelectionPaste dispatch call is rejected",
        ),
        (
            duplicate_current_page_dispatch[SEQUENCER_STEP_HANDLER]
            != step_draft_fixture[SEQUENCER_STEP_HANDLER]
            and bool(step_draft_transition_contract_errors(
                duplicate_current_page_dispatch
            )),
            "aliased duplicate PagePaste dispatch call is rejected",
        ),
        (
            direct_page_selection_boundary[PAGE_STRUCTURE_EDIT_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                direct_page_selection_boundary
            )),
            "aliased direct PageSelectionPaste Pattern boundary is rejected",
        ),
        (
            direct_current_page_boundary[PAGE_STRUCTURE_EDIT_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                direct_current_page_boundary
            )),
            "aliased direct PagePaste Pattern boundary is rejected",
        ),
        (
            direct_step_paste_boundary[PAGE_STRUCTURE_EDIT_WORKFLOW]
            != step_draft_fixture[PAGE_STRUCTURE_EDIT_WORKFLOW]
            and bool(step_draft_transition_contract_errors(
                direct_step_paste_boundary
            )),
            "aliased direct StepPaste Pattern boundary is rejected",
        ),
    ) + tuple(
        (
            bool(step_draft_transition_contract_errors(fixture)),
            f"restored retired raw Page symbol is rejected: {symbol}",
        )
        for symbol, fixture in restored_additional_raw_symbols
    ) + tuple(
        (
            bool(step_draft_transition_contract_errors(fixture)),
            f"restored retired raw Track symbol is rejected: {symbol}",
        )
        for symbol, fixture in restored_track_raw_symbols
    ) + tuple(
        (
            selector not in fixture
            and bool(cold_placement_contract_errors(fixture)),
            f"missing contracted cold selector is rejected: {selector}",
        )
        for selector, fixture in missing_structure_cold_selector_fixtures
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
    contract_sources = {
        path.relative_to(ROOT).as_posix(): path.read_text(encoding="utf-8")
        for path in source_files()
    }
    errors.extend(step_draft_transition_contract_errors(contract_sources))
    errors.extend(extmem_lifetime_contract_errors(contract_sources))

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
        errors.extend(cold_placement_contract_errors(cold_placement))
        for selector in (
            "*(.text.*_M_manager*)",
            "*(.text.*9subscribe*)",
            "*(.text._ZN2oc5state12Subscription5resetEv*)",
            "*(.text._ZNSt5arrayIN2oc5state12SubscriptionE*D*Ev)",
            "*(.text._ZN4core3app16makeExtmemUniqueINS_5state9sequencer30SequencerHistoryFullBankChangeE*)",
            "*(.text._ZNSt15__uniq_ptr_implIN4core5state9sequencer30SequencerHistoryFullBankChangeE*)",
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
        content_code = cpp_code_mask(content)
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

        if DIRECT_EXTMEM_CALL.search(content_code) and rel not in DIRECT_EXTMEM_OWNERS:
            errors.append(
                f"{rel}: direct EXTMEM allocation bypasses the tracked owners"
            )
        if (DIRECT_SMALLOC_MUTATION_CALL.search(content_code) and
                rel != "app/ExtmemAllocator.hpp"):
            errors.append(
                f"{rel}: direct smalloc mutation bypasses the strict EXTMEM owner"
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
