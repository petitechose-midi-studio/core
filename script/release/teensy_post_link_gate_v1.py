#!/usr/bin/env python3
"""Versioned, product-neutral Teensy 4.1 post-link memory and ELF gate."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import sys
from typing import Any, Mapping, Sequence


POST_LINK_GATE_INTERFACE_VERSION = 1

__all__ = (
    "POST_LINK_GATE_INTERFACE_VERSION",
    "TeensyMemoryUsage",
    "TeensyProductPolicy",
    "TeensyPostLinkResult",
    "load_product_policy",
    "evaluate_post_link",
    "format_summary",
    "main",
)

FLASH_ORIGIN = 0x60000000
FLASH_SIZE = 7936 * 1024
ITCM_ORIGIN = 0x00000000
ITCM_SIZE = 512 * 1024
ITCM_END = ITCM_ORIGIN + ITCM_SIZE
ITCM_BANK_SIZE = 32 * 1024
FLEXRAM_BANK_COUNT = 16
RAM1_ORIGIN = 0x20000000
RAM2_ORIGIN = 0x20200000
RAM2_SIZE = 512 * 1024
PSRAM_ORIGIN = 0x70000000
PSRAM_LINKER_MAX_SIZE = 32 * 1024 * 1024

TOPOLOGY_CONTRACT_VERSION = 1
CLASSIFIED_ITCM_SECTIONS = (".text.itcm", ".ARM.exidx")
REQUIRED_LINKER_SYMBOLS = (
    "_stext",
    "_etext",
    "_itcm_block_count",
    "_flexram_bank_config",
)

_SECTION_REGION = {
    ".text.headers": "flash",
    ".text.code": "flash",
    ".text.progmem": "flash",
    ".text.itcm": "itcm",
    ".ARM.exidx": "itcm",
    ".data": "ram1",
    ".bss": "ram1",
    ".bss.dma": "ram2",
    ".bss.extram": "psram",
    ".text.csf": "flash",
}
_REQUIRED_ALLOCATED_SECTIONS = (
    ".text.headers",
    ".text.code",
    ".text.itcm",
    ".ARM.exidx",
    ".data",
    ".bss",
    ".bss.dma",
    ".bss.extram",
    ".text.csf",
)

_FLASH_RE = re.compile(
    r"FLASH:\s+code:(\d+),\s+data:(\d+),\s+headers:(\d+)"
    r"\s+free for files:(\d+)"
)
_RAM1_RE = re.compile(
    r"RAM1:\s+variables:(\d+),\s+code:(\d+),\s+padding:(\d+)"
    r"\s+free for local variables:(\d+)"
)
_RAM2_RE = re.compile(
    r"RAM2:\s+variables:(\d+)\s+free for malloc/new:(\d+)"
)
_EXTRAM_RE = re.compile(r"EXTRAM:\s+variables:(\d+)")
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
class TeensyMemoryUsage:
    flash_code: int
    flash_data: int
    flash_headers: int
    flash_free: int
    ram1_variables: int
    ram1_code: int
    ram1_padding: int
    ram1_free: int
    ram2_variables: int
    ram2_free: int
    extram_variables: int

    @property
    def flash_loaded(self) -> int:
        return self.flash_code + self.flash_data + self.flash_headers


@dataclass(frozen=True)
class TeensyProductPolicy:
    profile_id: str
    profile_version: str
    vector_name: str
    itcm_banks_exact: int
    flash_code_max: int
    flash_data_max: int
    flash_headers_max: int
    flash_enforcement: str
    ram1_variables_max: int
    itcm_max: int
    itcm_slack_min: int
    ram1_free_min: int
    ram2_variables_max: int | None
    ram2_variables_exact: int | None
    ram2_free_min: int | None
    ram2_free_exact: int | None
    psram_capacity: int
    psram_static_max: int
    psram_free_min: int


@dataclass(frozen=True)
class TeensyPostLinkResult:
    policy: TeensyProductPolicy
    usage: TeensyMemoryUsage | None
    physical_itcm_bytes: int | None
    copy_itcm_bytes: int | None
    itcm_banks: int | None
    violations: tuple[str, ...]
    advisories: tuple[str, ...]

    @property
    def passed(self) -> bool:
        return not self.violations


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


def _mapping(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise ValueError(f"{label} must be an object")
    return value


def _required_text(values: Mapping[str, Any], key: str, label: str) -> str:
    value = values.get(key)
    if not isinstance(value, str) or not value:
        raise ValueError(f"{label}.{key} must be a non-empty string")
    return value


def _required_int(values: Mapping[str, Any], key: str, label: str) -> int:
    value = values.get(key)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ValueError(f"{label}.{key} must be a non-negative integer")
    return value


def _optional_int(values: Mapping[str, Any], key: str, label: str) -> int | None:
    if key not in values:
        return None
    return _required_int(values, key, label)


def _exclusive_bound(
    values: Mapping[str, Any],
    maximum_key: str,
    exact_key: str,
    label: str,
) -> tuple[int | None, int | None]:
    maximum = _optional_int(values, maximum_key, label)
    exact = _optional_int(values, exact_key, label)
    if (maximum is None) == (exact is None):
        raise ValueError(
            f"{label} must define exactly one of {maximum_key} or {exact_key}"
        )
    return maximum, exact


def _policy_from_mapping(
    profile: Mapping[str, Any], vector_name: str | None
) -> TeensyProductPolicy:
    if profile.get("schemaVersion") != POST_LINK_GATE_INTERFACE_VERSION:
        raise ValueError(
            "product profile schemaVersion must equal post-link interface version 1"
        )

    identity = _mapping(profile.get("profile"), "profile")
    profile_id = _required_text(identity, "id", "profile")
    profile_version = _required_text(identity, "version", "profile")
    selected_vector = vector_name
    if selected_vector is None:
        selected_vector = _required_text(identity, "activeMemoryVector", "profile")

    vectors = _mapping(profile.get("memoryVectors"), "memoryVectors")
    if selected_vector not in vectors:
        raise ValueError(f"memory vector is missing: {selected_vector}")
    vector = _mapping(vectors[selected_vector], f"memoryVectors.{selected_vector}")
    vector_label = f"memoryVectors.{selected_vector}"

    banks = _required_int(vector, "itcmBanksExact", vector_label)
    if not 1 <= banks <= FLEXRAM_BANK_COUNT:
        raise ValueError(
            f"{vector_label}.itcmBanksExact must be within 1..{FLEXRAM_BANK_COUNT}"
        )

    linker = profile.get("linker")
    if linker is not None:
        linker_values = _mapping(linker, "linker")
        if "itcmBankBytes" in linker_values:
            bank_bytes = _required_int(linker_values, "itcmBankBytes", "linker")
            if bank_bytes != ITCM_BANK_SIZE:
                raise ValueError(
                    f"linker.itcmBankBytes must equal {ITCM_BANK_SIZE}, got {bank_bytes}"
                )
        if "itcmBanksExact" in linker_values:
            linker_banks = _required_int(
                linker_values, "itcmBanksExact", "linker"
            )
            if linker_banks != banks:
                raise ValueError(
                    "linker.itcmBanksExact does not match the selected memory vector"
                )

    flash = _mapping(vector.get("flash"), f"{vector_label}.flash")
    ram1 = _mapping(vector.get("ram1"), f"{vector_label}.ram1")
    ram2 = _mapping(vector.get("ram2"), f"{vector_label}.ram2")
    psram = _mapping(vector.get("psram"), f"{vector_label}.psram")

    flash_code_max = _required_int(flash, "codeMaxBytes", f"{vector_label}.flash")
    flash_data_max = _required_int(flash, "dataMaxBytes", f"{vector_label}.flash")
    flash_headers_max = _required_int(
        flash, "headersMaxBytes", f"{vector_label}.flash"
    )
    flash_enforcement = flash.get("enforcement", "blocking")
    if flash_enforcement not in ("blocking", "advisory"):
        raise ValueError(
            f"{vector_label}.flash.enforcement must be 'blocking' or 'advisory'"
        )
    if flash_code_max + flash_data_max + flash_headers_max > FLASH_SIZE:
        raise ValueError(f"{vector_label}.flash maxima exceed Teensy Flash capacity")

    ram1_variables_max = _required_int(
        ram1, "variablesMaxBytes", f"{vector_label}.ram1"
    )
    itcm_max = _required_int(ram1, "itcmMaxBytes", f"{vector_label}.ram1")
    itcm_slack_min = _required_int(
        ram1, "itcmSlackMinBytes", f"{vector_label}.ram1"
    )
    ram1_free_min = _required_int(
        ram1, "localAndStackFreeMinBytes", f"{vector_label}.ram1"
    )
    itcm_capacity = banks * ITCM_BANK_SIZE
    ram1_capacity = (FLEXRAM_BANK_COUNT - banks) * ITCM_BANK_SIZE
    if itcm_max + itcm_slack_min != itcm_capacity:
        raise ValueError(
            f"{vector_label}.ram1 ITCM maximum plus slack must equal {itcm_capacity}"
        )
    if ram1_variables_max + ram1_free_min != ram1_capacity:
        raise ValueError(
            f"{vector_label}.ram1 variables maximum plus free minimum must equal "
            f"{ram1_capacity}"
        )

    ram2_variables_max, ram2_variables_exact = _exclusive_bound(
        ram2,
        "variablesMaxBytes",
        "variablesExactBytes",
        f"{vector_label}.ram2",
    )
    ram2_free_min, ram2_free_exact = _exclusive_bound(
        ram2,
        "freeMinBytes",
        "freeExactBytes",
        f"{vector_label}.ram2",
    )
    if (ram2_variables_exact is None) != (ram2_free_exact is None):
        raise ValueError(
            f"{vector_label}.ram2 must use variablesMaxBytes/freeMinBytes or "
            "variablesExactBytes/freeExactBytes"
        )
    variables_bound = (
        ram2_variables_exact
        if ram2_variables_exact is not None
        else ram2_variables_max
    )
    free_bound = ram2_free_exact if ram2_free_exact is not None else ram2_free_min
    if variables_bound is None or free_bound is None:
        raise ValueError(f"{vector_label}.ram2 contains an incomplete bound")
    if variables_bound + free_bound != RAM2_SIZE:
        raise ValueError(
            f"{vector_label}.ram2 variables plus free must equal {RAM2_SIZE}"
        )

    psram_capacity = _required_int(
        psram, "capacityBytes", f"{vector_label}.psram"
    )
    psram_static_max = _required_int(
        psram, "staticMaxBytes", f"{vector_label}.psram"
    )
    psram_free_min = _required_int(
        psram, "staticFreeMinBytes", f"{vector_label}.psram"
    )
    if not 0 < psram_capacity <= PSRAM_LINKER_MAX_SIZE:
        raise ValueError(
            f"{vector_label}.psram.capacityBytes must be within 1.."
            f"{PSRAM_LINKER_MAX_SIZE}"
        )
    if psram_static_max + psram_free_min != psram_capacity:
        raise ValueError(
            f"{vector_label}.psram static maximum plus free minimum must equal "
            f"{psram_capacity}"
        )

    return TeensyProductPolicy(
        profile_id=profile_id,
        profile_version=profile_version,
        vector_name=selected_vector,
        itcm_banks_exact=banks,
        flash_code_max=flash_code_max,
        flash_data_max=flash_data_max,
        flash_headers_max=flash_headers_max,
        flash_enforcement=flash_enforcement,
        ram1_variables_max=ram1_variables_max,
        itcm_max=itcm_max,
        itcm_slack_min=itcm_slack_min,
        ram1_free_min=ram1_free_min,
        ram2_variables_max=ram2_variables_max,
        ram2_variables_exact=ram2_variables_exact,
        ram2_free_min=ram2_free_min,
        ram2_free_exact=ram2_free_exact,
        psram_capacity=psram_capacity,
        psram_static_max=psram_static_max,
        psram_free_min=psram_free_min,
    )


def load_product_policy(
    path: Path | str, vector_name: str | None = None
) -> TeensyProductPolicy:
    profile_path = Path(path)
    try:
        value = json.loads(profile_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read product profile {profile_path}: {error}") from error
    return _policy_from_mapping(_mapping(value, "product profile"), vector_name)


def parse_teensy_size(output: str) -> TeensyMemoryUsage:
    flash = _FLASH_RE.search(output)
    ram1 = _RAM1_RE.search(output)
    ram2 = _RAM2_RE.search(output)
    extram = _EXTRAM_RE.search(output)
    if flash is None or ram1 is None or ram2 is None or extram is None:
        raise ValueError(
            "teensy_size output does not contain complete Flash, RAM1, RAM2, "
            "and EXTRAM usage"
        )
    return TeensyMemoryUsage(
        flash_code=int(flash.group(1)),
        flash_data=int(flash.group(2)),
        flash_headers=int(flash.group(3)),
        flash_free=int(flash.group(4)),
        ram1_variables=int(ram1.group(1)),
        ram1_code=int(ram1.group(2)),
        ram1_padding=int(ram1.group(3)),
        ram1_free=int(ram1.group(4)),
        ram2_variables=int(ram2.group(1)),
        ram2_free=int(ram2.group(2)),
        extram_variables=int(extram.group(1)),
    )


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


def _memory_findings(
    policy: TeensyProductPolicy, usage: TeensyMemoryUsage
) -> tuple[tuple[str, ...], tuple[str, ...]]:
    prefix = f"{policy.profile_id}/{policy.vector_name}"
    violations: list[str] = []
    advisories: list[str] = []

    if usage.flash_loaded + usage.flash_free != FLASH_SIZE:
        violations.append(
            f"{prefix} Flash equation: loaded {usage.flash_loaded}B plus free "
            f"{usage.flash_free}B does not equal {FLASH_SIZE}B"
        )
    if usage.ram1_code + usage.ram1_padding != policy.itcm_banks_exact * ITCM_BANK_SIZE:
        violations.append(
            f"{prefix} ITCM equation: code {usage.ram1_code}B plus padding "
            f"{usage.ram1_padding}B does not equal "
            f"{policy.itcm_banks_exact * ITCM_BANK_SIZE}B"
        )
    ram1_capacity = (FLEXRAM_BANK_COUNT - policy.itcm_banks_exact) * ITCM_BANK_SIZE
    if usage.ram1_variables + usage.ram1_free != ram1_capacity:
        violations.append(
            f"{prefix} RAM1 equation: variables {usage.ram1_variables}B plus free "
            f"{usage.ram1_free}B does not equal {ram1_capacity}B"
        )
    if usage.ram2_variables + usage.ram2_free != RAM2_SIZE:
        violations.append(
            f"{prefix} RAM2 equation: variables {usage.ram2_variables}B plus free "
            f"{usage.ram2_free}B does not equal {RAM2_SIZE}B"
        )

    flash_maximums = (
        ("Flash code", usage.flash_code, policy.flash_code_max),
        ("Flash data", usage.flash_data, policy.flash_data_max),
        ("Flash headers", usage.flash_headers, policy.flash_headers_max),
    )
    flash_findings = (
        advisories if policy.flash_enforcement == "advisory" else violations
    )
    for label, observed, maximum in flash_maximums:
        if observed > maximum:
            flash_findings.append(
                f"{prefix} {label} {observed}B exceeds maximum {maximum}B"
            )

    blocking_maximums = (
        ("RAM1 variables", usage.ram1_variables, policy.ram1_variables_max),
        ("ITCM code", usage.ram1_code, policy.itcm_max),
        ("PSRAM static", usage.extram_variables, policy.psram_static_max),
    )
    for label, observed, maximum in blocking_maximums:
        if observed > maximum:
            violations.append(
                f"{prefix} {label} {observed}B exceeds maximum {maximum}B"
            )

    minimums = (
        ("ITCM slack", usage.ram1_padding, policy.itcm_slack_min),
        ("RAM1 local/stack free", usage.ram1_free, policy.ram1_free_min),
        (
            "PSRAM static free",
            policy.psram_capacity - usage.extram_variables,
            policy.psram_free_min,
        ),
    )
    for label, observed, minimum in minimums:
        if observed < minimum:
            violations.append(
                f"{prefix} {label} {observed}B is below minimum {minimum}B"
            )

    if policy.ram2_variables_exact is not None:
        if usage.ram2_variables != policy.ram2_variables_exact:
            violations.append(
                f"{prefix} RAM2 variables {usage.ram2_variables}B does not equal "
                f"{policy.ram2_variables_exact}B"
            )
    elif policy.ram2_variables_max is not None:
        if usage.ram2_variables > policy.ram2_variables_max:
            violations.append(
                f"{prefix} RAM2 variables {usage.ram2_variables}B exceeds maximum "
                f"{policy.ram2_variables_max}B"
            )

    if policy.ram2_free_exact is not None:
        if usage.ram2_free != policy.ram2_free_exact:
            violations.append(
                f"{prefix} RAM2 free {usage.ram2_free}B does not equal "
                f"{policy.ram2_free_exact}B"
            )
    elif policy.ram2_free_min is not None:
        if usage.ram2_free < policy.ram2_free_min:
            violations.append(
                f"{prefix} RAM2 free {usage.ram2_free}B is below minimum "
                f"{policy.ram2_free_min}B"
            )

    if usage.extram_variables > policy.psram_capacity:
        violations.append(
            f"{prefix} PSRAM static {usage.extram_variables}B exceeds installed "
            f"capacity {policy.psram_capacity}B"
        )
    return tuple(violations), tuple(advisories)


def _allocated_section_violations(
    policy: TeensyProductPolicy,
    sections: tuple[ElfSection, ...],
    usage: TeensyMemoryUsage | None,
) -> tuple[str, ...]:
    prefix = f"{policy.profile_id}/{policy.vector_name}"
    allocated = tuple(section for section in sections if "A" in section.flags and section.size > 0)
    violations: list[str] = []
    by_name: dict[str, list[ElfSection]] = {}
    for section in allocated:
        by_name.setdefault(section.name, []).append(section)

    for name in _REQUIRED_ALLOCATED_SECTIONS:
        matches = by_name.get(name, [])
        if not matches:
            violations.append(f"{prefix} required allocated section is missing: {name}")
        elif len(matches) != 1:
            violations.append(f"{prefix} allocated section appears more than once: {name}")

    ranges = {
        "flash": (FLASH_ORIGIN, FLASH_ORIGIN + FLASH_SIZE),
        "itcm": (ITCM_ORIGIN, policy.itcm_banks_exact * ITCM_BANK_SIZE),
        "ram1": (
            RAM1_ORIGIN,
            RAM1_ORIGIN
            + (FLEXRAM_BANK_COUNT - policy.itcm_banks_exact) * ITCM_BANK_SIZE,
        ),
        "ram2": (RAM2_ORIGIN, RAM2_ORIGIN + RAM2_SIZE),
        "psram": (PSRAM_ORIGIN, PSRAM_ORIGIN + policy.psram_capacity),
    }
    by_region: dict[str, list[ElfSection]] = {name: [] for name in ranges}
    for section in allocated:
        region = _SECTION_REGION.get(section.name)
        if region is None:
            violations.append(
                f"{prefix} unknown allocated section {section.name} at "
                f"0x{section.address:x}..0x{section.end:x}"
            )
            continue
        by_region[region].append(section)
        start, end = ranges[region]
        if section.address < start or section.end > end:
            violations.append(
                f"{prefix} section {section.name} must stay in {region} "
                f"0x{start:x}..0x{end:x}; got "
                f"0x{section.address:x}..0x{section.end:x}"
            )

    for region, region_sections in by_region.items():
        ordered = sorted(region_sections, key=lambda item: (item.address, item.index))
        for previous, current in zip(ordered, ordered[1:]):
            if current.address < previous.end:
                violations.append(
                    f"{prefix} {region} sections overlap: {previous.name} ends at "
                    f"0x{previous.end:x}, {current.name} starts at 0x{current.address:x}"
                )

    if usage is not None:
        expected_owner_bytes = {
            "itcm": usage.ram1_code,
            "ram1": usage.ram1_variables,
            "ram2": usage.ram2_variables,
            "psram": usage.extram_variables,
        }
        for region, expected in expected_owner_bytes.items():
            observed = sum(section.size for section in by_region[region])
            if observed != expected:
                violations.append(
                    f"{prefix} {region} allocated owners total {observed}B, "
                    f"teensy_size reports {expected}B"
                )
    return tuple(violations)


def evaluate_post_link(
    policy: TeensyProductPolicy,
    teensy_size_output: str,
    readelf_sections_output: str,
    nm_symbols_output: str,
) -> TeensyPostLinkResult:
    prefix = f"{policy.profile_id}/{policy.vector_name}"
    violations: list[str] = []
    advisories: list[str] = []
    usage: TeensyMemoryUsage | None = None
    sections: tuple[ElfSection, ...] | None = None
    physical_bytes: int | None = None
    copy_bytes: int | None = None
    banks: int | None = None

    try:
        usage = parse_teensy_size(teensy_size_output)
    except ValueError as error:
        violations.append(f"{prefix} invalid teensy_size metadata: {error}")
    else:
        memory_violations, memory_advisories = _memory_findings(policy, usage)
        violations.extend(memory_violations)
        advisories.extend(memory_advisories)

    topology_errors = topology_violations(
        readelf_sections_output, nm_symbols_output
    )
    violations.extend(f"{prefix} ITCM topology: {item}" for item in topology_errors)
    try:
        sections = parse_readelf_sections(readelf_sections_output)
    except ValueError:
        sections = None
    if sections is not None:
        violations.extend(_allocated_section_violations(policy, sections, usage))

    if not topology_errors and sections is not None:
        itcm_sections = _allocated_itcm_sections(sections)
        symbols = parse_nm_symbols(nm_symbols_output)
        symbol_values, symbol_errors = _symbol_values(symbols)
        if not symbol_errors and itcm_sections:
            physical_start = min(section.address for section in itcm_sections)
            physical_end = max(section.end for section in itcm_sections)
            physical_bytes = physical_end - physical_start
            copy_bytes = symbol_values["_etext"] - symbol_values["_stext"]
            banks = symbol_values["_itcm_block_count"]
            if banks != policy.itcm_banks_exact:
                violations.append(
                    f"{prefix} ITCM banks {banks} does not equal profile "
                    f"{policy.itcm_banks_exact}"
                )
            if usage is not None and physical_bytes != usage.ram1_code:
                violations.append(
                    f"{prefix} physical ITCM {physical_bytes}B does not equal "
                    f"teensy_size code {usage.ram1_code}B"
                )
            if physical_bytes != copy_bytes:
                violations.append(
                    f"{prefix} physical ITCM {physical_bytes}B does not equal "
                    f"startup copy {copy_bytes}B"
                )

    return TeensyPostLinkResult(
        policy=policy,
        usage=usage,
        physical_itcm_bytes=physical_bytes,
        copy_itcm_bytes=copy_bytes,
        itcm_banks=banks,
        violations=tuple(violations),
        advisories=tuple(advisories),
    )


def format_summary(result: TeensyPostLinkResult) -> str:
    identity = (
        f"{result.policy.profile_id}@{result.policy.profile_version}/"
        f"{result.policy.vector_name}"
    )
    if not result.passed or result.usage is None:
        return (
            f"Teensy post-link gate v{POST_LINK_GATE_INTERFACE_VERSION} FAIL: "
            f"profile={identity}, violations={len(result.violations)}"
        )
    usage = result.usage
    psram_free = result.policy.psram_capacity - usage.extram_variables
    status = "WARN" if result.advisories else "PASS"
    advisory_suffix = (
        f", advisories={len(result.advisories)}" if result.advisories else ""
    )
    return (
        f"Teensy post-link gate v{POST_LINK_GATE_INTERFACE_VERSION} {status}: "
        f"profile={identity}, Flash={usage.flash_code}/{usage.flash_data}/"
        f"{usage.flash_headers}B, RAM1={usage.ram1_variables}B, "
        f"ITCM={result.physical_itcm_bytes}B/{result.itcm_banks} banks, "
        f"RAM1-free={usage.ram1_free}B, RAM2={usage.ram2_variables}/"
        f"{usage.ram2_free}B, PSRAM={usage.extram_variables}/{psram_free}B"
        f"{advisory_suffix}"
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--vector")
    parser.add_argument("--teensy-size-output", type=Path, required=True)
    parser.add_argument("--readelf-sections-output", type=Path, required=True)
    parser.add_argument("--nm-symbols-output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        policy = load_product_policy(args.profile, args.vector)
        result = evaluate_post_link(
            policy,
            args.teensy_size_output.read_text(encoding="utf-8"),
            args.readelf_sections_output.read_text(encoding="utf-8"),
            args.nm_symbols_output.read_text(encoding="utf-8"),
        )
    except (OSError, UnicodeError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    if not result.passed:
        print(format_summary(result), file=sys.stderr)
        for violation in result.violations:
            print(f"  - {violation}", file=sys.stderr)
        return 1
    stream = sys.stderr if result.advisories else sys.stdout
    print(format_summary(result), file=stream)
    for advisory in result.advisories:
        print(f"  ! {advisory}", file=stream)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
