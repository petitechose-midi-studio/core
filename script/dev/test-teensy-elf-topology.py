#!/usr/bin/env python3

from pathlib import Path

from teensy_elf_topology import (
    CLASSIFIED_ITCM_SECTIONS,
    TOPOLOGY_CONTRACT_VERSION,
    parse_nm_symbols,
    parse_readelf_sections,
    topology_summary,
    topology_violations,
)


FIXTURE_DIR = Path(__file__).parent / "fixtures" / "teensy-elf-topology"


def fixture(name: str, kind: str) -> str:
    return (FIXTURE_DIR / f"{name}.{kind}.txt").read_text(encoding="utf-8")


def main() -> int:
    assert TOPOLOGY_CONTRACT_VERSION == 1
    assert CLASSIFIED_ITCM_SECTIONS == (".text.itcm", ".ARM.exidx")

    expected = {
        "product": (292264, 9),
        "ux-recorder": (294184, 9),
        "diagnostics": (296184, 10),
    }
    for name, (physical_bytes, banks) in expected.items():
        sections = fixture(name, "sections")
        symbols = fixture(name, "symbols")
        assert topology_violations(sections, symbols) == ()
        summary = topology_summary(sections, symbols)
        assert f"physical={physical_bytes}B" in summary
        assert f"copy={physical_bytes}B" in summary
        assert f"banks={banks}/16" in summary
        assert len(parse_readelf_sections(sections)) >= 4
        assert len(parse_nm_symbols(symbols)) == 4

    old_sections = fixture("gap-orphan", "sections")
    old_symbols = fixture("gap-orphan", "symbols")
    old_violations = topology_violations(old_sections, old_symbols)
    assert any("unclassified allocated ITCM section .fini" in item for item in old_violations)
    assert (
        "_etext 0x475a8 does not equal physical allocated ITCM end 0x475ac"
        in old_violations
    )

    product_sections = fixture("product", "sections")
    product_symbols = fixture("product", "symbols")

    missing_section = "\n".join(
        line for line in product_sections.splitlines() if ".ARM.exidx" not in line
    )
    assert any(
        "required allocated ITCM section is missing: .ARM.exidx" == item
        for item in topology_violations(missing_section, product_symbols)
    )

    duplicate_section = product_sections + (
        "\n  [12] .ARM.exidx        ARM_EXIDX       000475a0 13e5a0 "
        "000008 00  AL  4   0  4\n"
    )
    assert any(
        "allocated ITCM section appears more than once: .ARM.exidx" == item
        for item in topology_violations(duplicate_section, product_symbols)
    )

    overlapping = product_sections.replace(
        "000475a0 13e5a0 000008", "0004759c 13e5a0 00000c"
    )
    assert any(
        "allocated ITCM sections overlap" in item
        for item in topology_violations(overlapping, product_symbols)
    )

    outside_itcm = product_sections.replace(
        "00000000 0f7000 0475a0", "00000000 0f7000 080004"
    )
    assert any(
        "allocated ITCM section .text.itcm exceeds" in item
        for item in topology_violations(outside_itcm, product_symbols)
    )

    missing_symbol = "\n".join(
        line for line in product_symbols.splitlines() if not line.startswith("_etext ")
    )
    assert (
        "required linker symbol is missing: _etext"
        in topology_violations(product_sections, missing_symbol)
    )

    duplicate_symbol = product_symbols + "_etext T 475a8\n"
    assert (
        "linker symbol appears more than once: _etext"
        in topology_violations(product_sections, duplicate_symbol)
    )

    wrong_blocks = product_symbols.replace("_itcm_block_count A 9", "_itcm_block_count A 8")
    wrong_block_violations = topology_violations(product_sections, wrong_blocks)
    assert any("_itcm_block_count 8 does not equal" in item for item in wrong_block_violations)
    assert any("8 ITCM banks end" in item for item in wrong_block_violations)

    wrong_config = product_symbols.replace(
        "_flexram_bank_config A aaabffff", "_flexram_bank_config A aaaaffff"
    )
    assert any(
        "_flexram_bank_config 0xaaaaffff does not match" in item
        for item in topology_violations(product_sections, wrong_config)
    )

    assert topology_violations("not readelf metadata", product_symbols) == (
        "invalid ELF section metadata: readelf output contains no parseable section rows",
    )

    print("Teensy ELF topology parser: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
