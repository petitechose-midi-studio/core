#!/usr/bin/env python3
"""Focused positive and fail-closed fixtures for post-link gate v1."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
RELEASE_DIR = ROOT / "script" / "release"
ACTIVE_CORE_PROFILE = RELEASE_DIR / "profiles" / "core-product-profile-v1.json"
if str(RELEASE_DIR) not in sys.path:
    sys.path.insert(0, str(RELEASE_DIR))

import teensy_post_link_gate_v1 as gate  # noqa: E402


FIXTURES = Path(__file__).parent / "fixtures" / "teensy-post-link-gate"
R04_FIXTURES = Path(__file__).parent / "fixtures" / "teensy-elf-topology"


def text(name: str) -> str:
    return (FIXTURES / name).read_text(encoding="utf-8")


def evaluated(prefix: str, profile_name: str) -> gate.TeensyPostLinkResult:
    policy = gate.load_product_policy(FIXTURES / profile_name)
    return gate.evaluate_post_link(
        policy,
        text(f"{prefix}.teensy-size.txt"),
        text(f"{prefix}.sections.txt"),
        text(f"{prefix}.symbols.txt"),
    )


def replace_once(value: str, old: str, new: str) -> str:
    if value.count(old) != 1:
        raise AssertionError(f"fixture marker must occur once: {old!r}")
    return value.replace(old, new, 1)


def main() -> int:
    assert gate.POST_LINK_GATE_INTERFACE_VERSION == 1
    assert gate.__all__ == (
        "POST_LINK_GATE_INTERFACE_VERSION",
        "TeensyMemoryUsage",
        "TeensyProductPolicy",
        "TeensyPostLinkResult",
        "load_product_policy",
        "evaluate_post_link",
        "format_summary",
        "main",
    )
    core_policy = gate.load_product_policy(
        FIXTURES / "core-product-profile-v1.json"
    )
    active_core_policy = gate.load_product_policy(ACTIVE_CORE_PROFILE)
    bitwig_policy = gate.load_product_policy(
        FIXTURES / "bitwig-product-profile-v1.json"
    )
    assert core_policy.profile_id == "midi-studio-core-teensy41"
    assert core_policy.vector_name == "R04-current"
    assert active_core_policy.profile_id == "midi-studio-core-teensy41"
    assert active_core_policy.profile_version == "release-2026.08.1"
    assert active_core_policy.vector_name == "release-current"
    assert active_core_policy.flash_enforcement == "advisory"
    assert bitwig_policy.flash_enforcement == "blocking"
    assert bitwig_policy.profile_id == "midi-studio-bitwig-teensy41"
    assert bitwig_policy.vector_name == "R11-initial"
    bitwig_final_policy = gate.load_product_policy(
        FIXTURES / "bitwig-product-profile-v1.json", "post-R15-final"
    )
    assert bitwig_final_policy.ram2_variables_exact == 171168
    assert bitwig_final_policy.ram2_free_exact == 353120

    core = evaluated("core", "core-product-profile-v1.json")
    active_core = gate.evaluate_post_link(
        active_core_policy,
        text("core.teensy-size.txt"),
        text("core.sections.txt"),
        text("core.symbols.txt"),
    )
    bitwig = evaluated("bitwig", "bitwig-product-profile-v1.json")
    assert core.violations == (), core.violations
    assert active_core.violations == (), active_core.violations
    assert bitwig.violations == (), bitwig.violations
    assert active_core.advisories == (), active_core.advisories
    assert core.physical_itcm_bytes == core.copy_itcm_bytes == 292264
    assert bitwig.physical_itcm_bytes == bitwig.copy_itcm_bytes == 273752
    assert core.itcm_banks == bitwig.itcm_banks == 9
    assert "midi-studio-core-teensy41@r04.2/R04-current" in gate.format_summary(core)
    assert "midi-studio-bitwig-teensy41@1.0.0/R11-initial" in gate.format_summary(bitwig)

    core_size = text("core.teensy-size.txt")
    core_sections = text("core.sections.txt")
    core_symbols = text("core.symbols.txt")
    bitwig_size = text("bitwig.teensy-size.txt")
    bitwig_sections = text("bitwig.sections.txt")
    bitwig_symbols = text("bitwig.symbols.txt")

    flash_over = replace_once(bitwig_size, "code:331536", "code:331537")
    violations = gate.evaluate_post_link(
        bitwig_policy, flash_over, bitwig_sections, bitwig_symbols
    ).violations
    assert any("Flash code 331537B exceeds maximum 331536B" in item for item in violations)

    advisory_flash_over = replace_once(core_size, "code:1083376", "code:1208321")
    advisory_flash_over = replace_once(
        advisory_flash_over,
        "free for files:6759424",
        "free for files:6634479",
    )
    advisory_result = gate.evaluate_post_link(
        active_core_policy,
        advisory_flash_over,
        core_sections,
        core_symbols,
    )
    assert advisory_result.passed
    assert advisory_result.violations == ()
    assert any(
        "Flash code 1208321B exceeds maximum 1208320B" in item
        for item in advisory_result.advisories
    )
    assert "WARN" in gate.format_summary(advisory_result)

    ram1_over = replace_once(core_size, "variables:76032", "variables:86017")
    ram1_over = replace_once(
        ram1_over,
        "free for local variables:153344",
        "free for local variables:143359",
    )
    violations = gate.evaluate_post_link(
        core_policy, ram1_over, core_sections, core_symbols
    ).violations
    assert any("RAM1 variables 86017B exceeds maximum 86016B" in item for item in violations)
    assert any("RAM1 local/stack free 143359B is below minimum 143360B" in item for item in violations)

    itcm_over = replace_once(bitwig_size, "code:273752", "code:273753")
    itcm_over = replace_once(itcm_over, "padding:21160", "padding:21159")
    violations = gate.evaluate_post_link(
        bitwig_policy, itcm_over, bitwig_sections, bitwig_symbols
    ).violations
    assert any("ITCM code 273753B exceeds maximum 273752B" in item for item in violations)
    assert any("physical ITCM 273752B does not equal teensy_size code 273753B" in item for item in violations)

    ram2_drift = replace_once(bitwig_size, "variables:171168", "variables:171169")
    ram2_drift = replace_once(ram2_drift, "malloc/new:353120", "malloc/new:353119")
    violations = gate.evaluate_post_link(
        bitwig_policy, ram2_drift, bitwig_sections, bitwig_symbols
    ).violations
    assert any("RAM2 variables 171169B exceeds maximum 171168B" in item for item in violations)
    assert any("RAM2 free 353119B is below minimum 353120B" in item for item in violations)

    psram_over = replace_once(bitwig_size, "variables:4300512", "variables:4300513")
    violations = gate.evaluate_post_link(
        bitwig_policy, psram_over, bitwig_sections, bitwig_symbols
    ).violations
    assert any("PSRAM static 4300513B exceeds maximum 4300512B" in item for item in violations)
    assert any("PSRAM static free 4088095B is below minimum 4088096B" in item for item in violations)

    unknown = bitwig_sections + (
        "  [12] .mystery          PROGBITS        60076000 0c8000 "
        "000020 00  AX  0   0  4\n"
    )
    violations = gate.evaluate_post_link(
        bitwig_policy, bitwig_size, unknown, bitwig_symbols
    ).violations
    assert any("unknown allocated section .mystery" in item for item in violations)

    misplaced = replace_once(
        bitwig_sections,
        ".bss.dma          NOBITS          20200000",
        ".bss.dma          NOBITS          20010000",
    )
    violations = gate.evaluate_post_link(
        bitwig_policy, bitwig_size, misplaced, bitwig_symbols
    ).violations
    assert any("section .bss.dma must stay in ram2" in item for item in violations)

    owner_drift = replace_once(core_sections, "002900 00  WA", "0028fc 00  WA")
    violations = gate.evaluate_post_link(
        core_policy, core_size, owner_drift, core_symbols
    ).violations
    assert any("ram1 allocated owners total 76028B" in item for item in violations)

    cross_product = gate.evaluate_post_link(
        bitwig_policy, core_size, core_sections, core_symbols
    )
    assert any("Flash code 1083376B exceeds maximum 331536B" in item for item in cross_product.violations)

    final_vector = gate.evaluate_post_link(
        bitwig_final_policy, bitwig_size, bitwig_sections, bitwig_symbols
    )
    assert any(
        "Flash code 331536B exceeds maximum 330120B" in item
        for item in final_vector.violations
    )

    old_sections = (R04_FIXTURES / "gap-orphan.sections.txt").read_text(encoding="utf-8")
    old_symbols = (R04_FIXTURES / "gap-orphan.symbols.txt").read_text(encoding="utf-8")
    old_violations = gate.topology_violations(old_sections, old_symbols)
    assert any("unclassified allocated ITCM section .fini" in item for item in old_violations)
    assert any("_etext 0x475a8" in item for item in old_violations)

    profile = json.loads(text("bitwig-product-profile-v1.json"))
    del profile["memoryVectors"]["R11-initial"]["flash"]["headersMaxBytes"]
    with tempfile.TemporaryDirectory() as directory:
        malformed = Path(directory) / "profile.json"
        malformed.write_text(json.dumps(profile), encoding="utf-8")
        try:
            gate.load_product_policy(malformed)
        except ValueError as error:
            assert "headersMaxBytes" in str(error)
        else:
            raise AssertionError("incomplete product profile must fail closed")

        mixed = json.loads(text("bitwig-product-profile-v1.json"))
        mixed_ram2 = mixed["memoryVectors"]["R11-initial"]["ram2"]
        mixed_ram2["freeExactBytes"] = mixed_ram2.pop("freeMinBytes")
        mixed_profile = Path(directory) / "mixed-profile.json"
        mixed_profile.write_text(json.dumps(mixed), encoding="utf-8")
        try:
            gate.load_product_policy(mixed_profile)
        except ValueError as error:
            assert "variablesMaxBytes/freeMinBytes" in str(error)
        else:
            raise AssertionError("mixed RAM2 bound modes must fail closed")

    try:
        gate.load_product_policy(
            FIXTURES / "bitwig-product-profile-v1.json", "missing-vector"
        )
    except ValueError as error:
        assert "memory vector is missing" in str(error)
    else:
        raise AssertionError("missing explicit vector must fail closed")

    malformed_metadata = gate.evaluate_post_link(
        bitwig_policy,
        "not teensy_size metadata",
        "not readelf metadata",
        "not nm metadata",
    )
    assert not malformed_metadata.passed
    assert any(
        "invalid teensy_size metadata:" in item
        for item in malformed_metadata.violations
    )
    assert any(
        "invalid ELF section metadata:" in item
        for item in malformed_metadata.violations
    )
    assert all(
        item.startswith("midi-studio-bitwig-teensy41/R11-initial ")
        for item in malformed_metadata.violations
    )

    assert gate.main(
        [
            "--profile",
            str(FIXTURES / "bitwig-product-profile-v1.json"),
            "--teensy-size-output",
            str(FIXTURES / "bitwig.teensy-size.txt"),
            "--readelf-sections-output",
            str(FIXTURES / "bitwig.sections.txt"),
            "--nm-symbols-output",
            str(FIXTURES / "bitwig.symbols.txt"),
        ]
    ) == 0

    print("Teensy post-link gate v1 fixtures: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
