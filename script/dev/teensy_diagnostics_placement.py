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

    reporter_storage = tuple(
        address
        for address, symbol_type, name in symbols
        if symbol_type in "BbDd" and "reporterStorage" in name
    )
    if not reporter_storage:
        violations.append("diagnostics reporter storage is missing from the ELF")
    elif any(
        address < RAM2_START or address >= RAM2_END
        for address in reporter_storage
    ):
        violations.append("diagnostics reporter samples and counters must stay in RAM2")

    return tuple(violations)
