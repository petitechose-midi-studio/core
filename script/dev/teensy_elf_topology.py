#!/usr/bin/env python3

from dataclasses import dataclass
import re


TOPOLOGY_CONTRACT_VERSION = 1
ITCM_ORIGIN = 0x00000000
ITCM_SIZE = 512 * 1024
ITCM_END = ITCM_ORIGIN + ITCM_SIZE
ITCM_BANK_SIZE = 32 * 1024
FLEXRAM_BANK_COUNT = 16
CLASSIFIED_ITCM_SECTIONS = (".text.itcm", ".ARM.exidx")
REQUIRED_LINKER_SYMBOLS = (
    "_stext",
    "_etext",
    "_itcm_block_count",
    "_flexram_bank_config",
)

_READELF_SECTION_RE = re.compile(
    r"^\s*\[\s*(?P<index>\d+)\]\s+"
    r"(?P<name>\S+)\s+\S+\s+"
    r"(?P<address>[0-9A-Fa-f]+)\s+"
    r"[0-9A-Fa-f]+\s+"
    r"(?P<size>[0-9A-Fa-f]+)\s+"
    r"[0-9A-Fa-f]+\s+"
    r"(?P<flags>[A-Za-z]+)\s+"
    r"\d+\s+\d+\s+\d+\s*$"
)
_NM_POSIX_RE = re.compile(
    r"^(?P<name>\S+)\s+(?P<kind>\S)\s+"
    r"(?P<value>[0-9A-Fa-f]+)(?:\s+[0-9A-Fa-f]+)?\s*$"
)


@dataclass(frozen=True)
class ElfSection:
    index: int
    name: str
    address: int
    size: int
    flags: str

    @property
    def end(self) -> int:
        return self.address + self.size


@dataclass(frozen=True)
class ElfSymbol:
    name: str
    value: int


def parse_readelf_sections(output: str) -> tuple[ElfSection, ...]:
    sections: list[ElfSection] = []
    for line in output.splitlines():
        match = _READELF_SECTION_RE.match(line)
        if match is None:
            continue
        sections.append(
            ElfSection(
                index=int(match.group("index")),
                name=match.group("name"),
                address=int(match.group("address"), 16),
                size=int(match.group("size"), 16),
                flags=match.group("flags"),
            )
        )
    if not sections:
        raise ValueError("readelf output contains no parseable section rows")
    return tuple(sections)


def parse_nm_symbols(output: str) -> tuple[ElfSymbol, ...]:
    symbols: list[ElfSymbol] = []
    for line in output.splitlines():
        match = _NM_POSIX_RE.match(line.strip())
        if match is None:
            continue
        name = match.group("name")
        if name in REQUIRED_LINKER_SYMBOLS:
            symbols.append(ElfSymbol(name=name, value=int(match.group("value"), 16)))
    return tuple(symbols)


def _allocated_itcm_sections(
    sections: tuple[ElfSection, ...],
) -> tuple[ElfSection, ...]:
    return tuple(
        sorted(
            (
                section
                for section in sections
                if "A" in section.flags
                and section.size > 0
                and section.address < ITCM_END
                and section.end > ITCM_ORIGIN
            ),
            key=lambda section: (section.address, section.index),
        )
    )


def _symbol_values(
    symbols: tuple[ElfSymbol, ...],
) -> tuple[dict[str, int], tuple[str, ...]]:
    values: dict[str, int] = {}
    violations: list[str] = []
    for name in REQUIRED_LINKER_SYMBOLS:
        matches = tuple(symbol.value for symbol in symbols if symbol.name == name)
        if not matches:
            violations.append(f"required linker symbol is missing: {name}")
        elif len(matches) != 1:
            violations.append(f"linker symbol appears more than once: {name}")
        else:
            values[name] = matches[0]
    return values, tuple(violations)


def topology_violations(
    readelf_sections_output: str,
    nm_symbols_output: str,
) -> tuple[str, ...]:
    try:
        sections = parse_readelf_sections(readelf_sections_output)
    except ValueError as error:
        return (f"invalid ELF section metadata: {error}",)

    itcm_sections = _allocated_itcm_sections(sections)
    symbols = parse_nm_symbols(nm_symbols_output)
    symbol_values, symbol_violations = _symbol_values(symbols)
    violations = list(symbol_violations)

    by_name: dict[str, list[ElfSection]] = {}
    for section in itcm_sections:
        by_name.setdefault(section.name, []).append(section)

    for name in CLASSIFIED_ITCM_SECTIONS:
        matches = by_name.get(name, [])
        if not matches:
            violations.append(f"required allocated ITCM section is missing: {name}")
        elif len(matches) != 1:
            violations.append(f"allocated ITCM section appears more than once: {name}")

    section_names = tuple(section.name for section in itcm_sections)
    if section_names != CLASSIFIED_ITCM_SECTIONS:
        violations.append(
            "allocated ITCM section order must be "
            f"{','.join(CLASSIFIED_ITCM_SECTIONS)}; got {','.join(section_names)}"
        )

    for section in itcm_sections:
        if section.name not in CLASSIFIED_ITCM_SECTIONS:
            violations.append(
                "unclassified allocated ITCM section "
                f"{section.name} at 0x{section.address:x}..0x{section.end:x}"
            )
        if section.address < ITCM_ORIGIN or section.end > ITCM_END:
            violations.append(
                f"allocated ITCM section {section.name} exceeds "
                f"0x{ITCM_ORIGIN:x}..0x{ITCM_END:x}"
            )

    for previous, current in zip(itcm_sections, itcm_sections[1:]):
        if current.address < previous.end:
            violations.append(
                "allocated ITCM sections overlap: "
                f"{previous.name} ends at 0x{previous.end:x}, "
                f"{current.name} starts at 0x{current.address:x}"
            )

    if not itcm_sections:
        violations.append("ELF contains no non-empty allocated ITCM section")
        return tuple(violations)

    physical_start = min(section.address for section in itcm_sections)
    physical_end = max(section.end for section in itcm_sections)

    if physical_start != ITCM_ORIGIN:
        violations.append(
            f"allocated ITCM extent starts at 0x{physical_start:x}, "
            f"expected 0x{ITCM_ORIGIN:x}"
        )

    stext = symbol_values.get("_stext")
    etext = symbol_values.get("_etext")
    block_count = symbol_values.get("_itcm_block_count")
    flexram_config = symbol_values.get("_flexram_bank_config")

    if stext is not None and stext != physical_start:
        violations.append(
            f"_stext 0x{stext:x} does not equal allocated ITCM start "
            f"0x{physical_start:x}"
        )
    if etext is not None and etext != physical_end:
        violations.append(
            f"_etext 0x{etext:x} does not equal physical allocated ITCM end "
            f"0x{physical_end:x}"
        )

    if stext is not None and etext is not None:
        if etext <= stext:
            violations.append(
                f"startup ITCM copy span is not positive: 0x{stext:x}..0x{etext:x}"
            )
        elif block_count is not None:
            copy_span = etext - stext
            expected_blocks = (copy_span + ITCM_BANK_SIZE - 1) // ITCM_BANK_SIZE
            if block_count != expected_blocks:
                violations.append(
                    f"_itcm_block_count {block_count} does not equal "
                    f"ceil({copy_span} / {ITCM_BANK_SIZE}) = {expected_blocks}"
                )

    if block_count is not None:
        if block_count < 1 or block_count > FLEXRAM_BANK_COUNT:
            violations.append(
                f"_itcm_block_count {block_count} is outside 1..{FLEXRAM_BANK_COUNT}"
            )
        elif ITCM_ORIGIN + block_count * ITCM_BANK_SIZE < physical_end:
            violations.append(
                f"{block_count} ITCM banks end at "
                f"0x{ITCM_ORIGIN + block_count * ITCM_BANK_SIZE:x}, below "
                f"physical allocated end 0x{physical_end:x}"
            )

        if flexram_config is not None and 1 <= block_count <= FLEXRAM_BANK_COUNT:
            expected_config = 0xAAAAAAAA | ((1 << (block_count * 2)) - 1)
            if flexram_config != expected_config:
                violations.append(
                    f"_flexram_bank_config 0x{flexram_config:x} does not match "
                    f"{block_count} ITCM banks (0x{expected_config:x})"
                )

    return tuple(violations)


def topology_summary(readelf_sections_output: str, nm_symbols_output: str) -> str:
    sections = parse_readelf_sections(readelf_sections_output)
    itcm_sections = _allocated_itcm_sections(sections)
    symbols = parse_nm_symbols(nm_symbols_output)
    symbol_values, symbol_violations = _symbol_values(symbols)
    if not itcm_sections or symbol_violations:
        raise ValueError("cannot summarize incomplete ELF topology metadata")
    physical_start = min(section.address for section in itcm_sections)
    physical_end = max(section.end for section in itcm_sections)
    copy_span = symbol_values["_etext"] - symbol_values["_stext"]
    names = ",".join(section.name for section in itcm_sections)
    return (
        f"ELF topology v{TOPOLOGY_CONTRACT_VERSION}: "
        f"ITCM sections={names}, physical={physical_end - physical_start}B, "
        f"copy={copy_span}B, banks={symbol_values['_itcm_block_count']}/"
        f"{FLEXRAM_BANK_COUNT}"
    )
