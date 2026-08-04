import re


FLASH_START = 0x60000000
RAM2_START = 0x20200000
RAM2_END = 0x20280000
EXTRAM_START = 0x70000000
EXTRAM_END = 0x70800000
PSRAM_SPAN_TABLE_BYTES = 12_408

_NM_SYMBOL_RE = re.compile(r"^(\d+)\s+(\d+)\s+([A-Za-z])\s+(.+)$")


def _symbols(nm_output: str) -> tuple[tuple[int, int, str, str], ...]:
    symbols: list[tuple[int, int, str, str]] = []
    for line in nm_output.splitlines():
        match = _NM_SYMBOL_RE.match(line.strip())
        if match is None:
            continue
        symbols.append((
            int(match.group(1)),
            int(match.group(2)),
            match.group(3),
            match.group(4),
        ))
    return tuple(symbols)


def diagnostics_placement_violations(nm_output: str) -> tuple[str, ...]:
    symbols = _symbols(nm_output)
    violations: list[str] = []

    reporter_methods = tuple(
        address
        for address, _size, symbol_type, name in symbols
        if symbol_type in "TtWw"
        and "core::diagnostics::PerformanceReporter::" in name
    )
    if not reporter_methods:
        violations.append("diagnostics reporter methods are missing from the ELF")
    elif any(address < FLASH_START for address in reporter_methods):
        violations.append("diagnostics reporter methods must execute from Flash")

    required_flash_symbols = (
        "core::diagnostics::performanceReporter()",
        "core::diagnostics::logMemoryFootprint(",
        "core::state::diagnostics::configureDebugLabels(",
        "oc::state::NotificationQueue::reportOverflow_(",
    )
    for marker in required_flash_symbols:
        matches = tuple(
            address
            for address, _size, symbol_type, name in symbols
            if symbol_type in "TtWw" and marker in name
        )
        if not matches:
            violations.append(f"required diagnostics Flash symbol is missing: {marker}")
        elif any(address < FLASH_START for address in matches):
            violations.append(f"diagnostics symbol must execute from Flash: {marker}")

    for storage_marker in ("reporterStorage", "memoryHighWaterStorage"):
        storage = tuple(
            address
            for address, _size, symbol_type, name in symbols
            if symbol_type in "BbDd" and storage_marker in name
        )
        if not storage:
            violations.append(
                f"diagnostics storage is missing from the ELF: {storage_marker}"
            )
        elif any(
            address < RAM2_START or address >= RAM2_END
            for address in storage
        ):
            violations.append(
                "diagnostics reporter samples and counters must stay in RAM2"
            )

    span_tables = tuple(
        (address, size)
        for address, size, symbol_type, name in symbols
        if symbol_type in "BbDd" and "psramSpanTable" in name
    )
    if len(span_tables) != 1:
        violations.append(
            "diagnostics must contain exactly one authoritative PSRAM span table"
        )
    else:
        address, size = span_tables[0]
        if address < EXTRAM_START or address >= EXTRAM_END:
            violations.append("diagnostics PSRAM span table must stay in EXTRAM")
        if size != PSRAM_SPAN_TABLE_BYTES:
            violations.append(
                "diagnostics PSRAM span table must be exactly 12408 bytes"
            )

    return tuple(violations)


def normal_build_diagnostics_violations(nm_output: str) -> tuple[str, ...]:
    """Reject opt-in diagnostics state or code from a normal firmware ELF."""
    symbols = _symbols(nm_output)
    forbidden_markers = (
        "core::diagnostics::PerformanceReporter",
        "core::diagnostics::performanceReporter()",
        "core::diagnostics::beginMemoryFootprintTracking()",
        "core::diagnostics::trackExtmemAllocation(",
        "core::diagnostics::trackExtmemFree(",
        "core::diagnostics::trackExtmemAllocationFailure()",
        "core::diagnostics::dynamicMemorySnapshot()",
        "core::diagnostics::recordDynamicMemorySample(",
        "core::diagnostics::logMemoryFootprint(",
        "core::diagnostics::storage_qualification::TraceBuffer",
        "storage_qualification::(anonymous namespace)::traceStorage",
        "storage_qualification::(anonymous namespace)::updateCold(",
        "storage_qualification::(anonymous namespace)::kRecordLine",
        "reporterStorage",
        "memoryHighWaterStorage",
        "psramSpanTable",
        "core::diagnostics::detail::PsramSpanTracker",
    )
    violations: list[str] = []
    for marker in forbidden_markers:
        if any(marker in name for _address, _size, _symbol_type, name in symbols):
            violations.append(
                f"normal firmware contains opt-in diagnostics symbol: {marker}"
            )
    return tuple(violations)
