#!/usr/bin/env python3
"""Compatibility façade for the versioned shared Teensy post-link gate.

Lease: CL-ELF-GATE. This exact surface expires at L-R11-02R after the Bitwig
consumer has migrated to script/release/teensy_post_link_gate_v1.py.
"""

from pathlib import Path
import sys


_RELEASE_DIR = Path(__file__).resolve().parents[1] / "release"
if str(_RELEASE_DIR) not in sys.path:
    sys.path.insert(0, str(_RELEASE_DIR))

from teensy_post_link_gate_v1 import (  # noqa: E402,F401
    CLASSIFIED_ITCM_SECTIONS,
    FLEXRAM_BANK_COUNT,
    ITCM_BANK_SIZE,
    ITCM_END,
    ITCM_ORIGIN,
    ITCM_SIZE,
    REQUIRED_LINKER_SYMBOLS,
    TOPOLOGY_CONTRACT_VERSION,
    ElfSection,
    ElfSymbol,
    parse_nm_symbols,
    parse_readelf_sections,
    topology_summary,
    topology_violations,
)


__all__ = (
    "TOPOLOGY_CONTRACT_VERSION",
    "ITCM_ORIGIN",
    "ITCM_SIZE",
    "ITCM_END",
    "ITCM_BANK_SIZE",
    "FLEXRAM_BANK_COUNT",
    "CLASSIFIED_ITCM_SECTIONS",
    "REQUIRED_LINKER_SYMBOLS",
    "ElfSection",
    "ElfSymbol",
    "parse_readelf_sections",
    "parse_nm_symbols",
    "topology_violations",
    "topology_summary",
)
