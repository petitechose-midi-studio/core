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

COLD_PLACEMENT_CONTRACT_SELECTORS = (
    "*SequencerPreparedPageStructureMutationPlan.cpp.o(.text* .rodata*)",
    "*SequencerPreparedPageStructureTransaction.cpp.o(.text* .rodata*)",
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
PAGE_STRUCTURE_SELECTION_WORKFLOW = (
    "src/handler/sequencer/SequencerStructureSelectionWorkflow.cpp"
)
PAGE_STRUCTURE_NAVIGATION_WORKFLOW = (
    "src/handler/sequencer/SequencerStructureNavigationWorkflow.cpp"
)
SEQUENCER_STEP_HANDLER = "src/handler/sequencer/SequencerStepHandler.cpp"
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
PAGE_STRUCTURE_ACTIONS = (
    "PageCreate",
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
        found = len(re.findall(pattern, files.get(rel, ""), flags=re.DOTALL))
        if found != count:
            errors.append(f"{rel}: {description} (expected {count}, found {found})")

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
        found = len(re.findall(pattern, bodies[0], flags=re.DOTALL))
        if found != count:
            errors.append(
                f"{rel}: {description} in {qualified_name} "
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
         "CoreState::undoSequencerHistory", "sequencer"),
        ("src/state/CoreStateSequencerHistoryTraversal.cpp",
         "CoreState::redoSequencerHistory", "sequencer"),
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
    if history_count != 5:
        errors.append(
            f"src: expected exactly five production HISTORY guards, found {history_count}"
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
        "Page Structure action enum must contain exactly the ten frozen actions",
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
            PAGE_STRUCTURE_NAVIGATION_WORKFLOW,
            "SequencerStructureNavigationWorkflow::createPreviewedStructure",
            "PageCreate",
            "createPreviewedPageAfterBoundary",
        ),
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
            PAGE_STRUCTURE_NAVIGATION_WORKFLOW,
            "SequencerStructureNavigationWorkflow::createPreviewedPageAfterBoundary",
            "buildSequencerPageCreateMutationPlan",
        ),
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
        r"\.button\s*\(\s*Config::ButtonID::BOTTOM_LEFT\s*\)\s*"
        r"\.longPress\s*\([^;]*\)\s*\.scope\s*\([^;]*\)\s*\.when\s*"
        r"\(\s*\[this\]\s*\(\s*\)\s*\{.*?"
        r"navigation_workflow_\.selectionActive\s*\(\s*\).*?"
        r"selectionHoldActionAvailable\s*\(\s*\).*?\.then\s*"
        r"\(\s*\[this\]\s*\(\s*\)\s*\{\s*"
        r"bottom_action_release_latch_\.arm\s*\(\s*"
        r"Config::ButtonID::BOTTOM_LEFT\s*\)\s*;.*?#endif\s*"
        r"if\s*\(\s*track_ui_\.selection\.active\.get\s*\(\s*\)\s*\)\s*"
        r"\{\s*edit_workflow_\.clearHoldAction\s*\(\s*\)\s*;\s*"
        r"if\s*\(\s*!commitPatternHistoryBarrier\s*\(\s*history_\s*\)\s*\)\s*"
        r"return\s*;\s*\}\s*edit_workflow_\.applySelectionBottomLeftHold\s*"
        r"\(\s*\)\s*;",
        "selection BottomLeft hold must keep the old boundary Track-only after the latch",
    )
    require(
        SEQUENCER_STEP_HANDLER,
        r"\.button\s*\(\s*Config::ButtonID::BOTTOM_LEFT\s*\)\s*"
        r"\.release\s*\(\s*\)\s*\.scope\s*\([^;]*\)\s*\.when\s*"
        r"\(\s*\[this\]\s*\(\s*\)\s*\{\s*return\s+"
        r"navigation_workflow_\.selectionActive\s*\(\s*\)\s*;\s*\}\s*\)"
        r".*?if\s*\(\s*track_ui_\.selection\.active\.get\s*\(\s*\)\s*\)\s*"
        r"\{\s*edit_workflow_\.clearHoldAction\s*\(\s*\)\s*;\s*"
        r"if\s*\(\s*!commitPatternHistoryBarrier\s*\(\s*history_\s*\)\s*\)\s*"
        r"return\s*;\s*\}\s*edit_workflow_\.applySelectionBottomLeftTap\s*"
        r"\(\s*\)\s*;",
        "selection BottomLeft tap must keep the old boundary Track-only",
    )
    require(
        SEQUENCER_STEP_HANDLER,
        r"\.button\s*\(\s*Config::ButtonID::BOTTOM_LEFT\s*\)\s*"
        r"\.release\s*\(\s*\)\s*\.scope\s*\([^;]*\)\s*\.when\s*"
        r"\(\s*\[this\]\s*\(\s*\)\s*\{\s*return\s+"
        r"currentStructureBottomActionsAvailable\s*\(\s*\)\s*;\s*\}\s*\)"
        r".*?if\s*\(\s*trackFocusActive\s*\(\s*\)\s*\)\s*\{\s*"
        r"edit_workflow_\.clearHoldAction\s*\(\s*\)\s*;\s*if\s*\(\s*"
        r"!commitPatternHistoryBarrier\s*\(\s*history_\s*\)\s*\)\s*"
        r"return\s*;\s*\}\s*edit_workflow_\.applyCurrentStructureShortPress\s*"
        r"\(\s*\)\s*;",
        "current BottomLeft tap must keep the old boundary Track-only",
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
        r"if\s*\(\s*trackFocusActive\s*\(\s*\)\s*\)\s*\{\s*"
        r"edit_workflow_\.clearHoldAction\s*\(\s*\)\s*;\s*if\s*\(\s*"
        r"!commitPatternHistoryBarrier\s*\(\s*history_\s*\)\s*\)\s*"
        r"return\s*;\s*\}\s*edit_workflow_\.applyCurrentStructureLongPress\s*"
        r"\(\s*\)\s*;",
        "current BottomLeft hold must keep the old boundary Track-only after the latch",
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
    added_eleventh_page_action = mutate(
        PAGE_STRUCTURE_TRANSACTION_HEADER,
        "    PageSelectionDeleteOrDeepReset,\n};",
        "    PageSelectionDeleteOrDeepReset,\n"
        "    ExperimentalEleventhAction,\n};",
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
            added_eleventh_page_action[PAGE_STRUCTURE_TRANSACTION_HEADER]
            != step_draft_fixture[PAGE_STRUCTURE_TRANSACTION_HEADER]
            and bool(step_draft_transition_contract_errors(
                added_eleventh_page_action
            )),
            "an eleventh Page Structure action is rejected",
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
