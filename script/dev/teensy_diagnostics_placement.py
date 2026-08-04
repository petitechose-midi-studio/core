import re


FLASH_START = 0x60000000
RAM2_START = 0x20200000
RAM2_END = 0x20280000

_NM_SYMBOL_RE = re.compile(r"^(\d+)\s+(\d+)\s+([A-Za-z])\s+(.+)$")


def _symbols(nm_output: str) -> tuple[tuple[int, str, str], ...]:
    symbols: list[tuple[int, str, str]] = []
    for line in nm_output.splitlines():
        match = _NM_SYMBOL_RE.match(line.strip())
        if match is None:
            continue
        symbols.append((int(match.group(1)), match.group(3), match.group(4)))
    return tuple(symbols)


def diagnostics_placement_violations(nm_output: str) -> tuple[str, ...]:
    symbols = _symbols(nm_output)
    violations: list[str] = []

    reporter_methods = tuple(
        address
        for address, symbol_type, name in symbols
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
            for address, symbol_type, name in symbols
            if symbol_type in "TtWw" and marker in name
        )
        if not matches:
            violations.append(f"required diagnostics Flash symbol is missing: {marker}")
        elif any(address < FLASH_START for address in matches):
            violations.append(f"diagnostics symbol must execute from Flash: {marker}")

    for storage_marker in ("reporterStorage", "memoryHighWaterStorage"):
        storage = tuple(
            address
            for address, symbol_type, name in symbols
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
    )
    violations: list[str] = []
    for marker in forbidden_markers:
        if any(marker in name for _address, _symbol_type, name in symbols):
            violations.append(
                f"normal firmware contains opt-in diagnostics symbol: {marker}"
            )
    return tuple(violations)
