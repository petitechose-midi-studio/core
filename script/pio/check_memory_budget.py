"""Report Teensy memory trends and reject unsafe placement or capacity failures."""

# pyright: reportUndefinedVariable=false

from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import subprocess
import sys

Import("env")

pio_env = env

DEV_SCRIPT_DIR = Path(pio_env["PROJECT_DIR"]) / "script" / "dev"
sys.path.insert(0, str(DEV_SCRIPT_DIR))
RELEASE_SCRIPT_DIR = Path(pio_env["PROJECT_DIR"]) / "script" / "release"
sys.path.insert(0, str(RELEASE_SCRIPT_DIR))

from teensy_diagnostics_placement import (  # noqa: E402
    diagnostics_placement_violations,
    normal_build_diagnostics_violations,
)
from teensy_product_placement import product_placement_violations  # noqa: E402
from teensy_post_link_gate_v1 import (  # noqa: E402
    POST_LINK_GATE_INTERFACE_VERSION,
    TeensyPostLinkResult,
    evaluate_post_link,
    format_summary as format_post_link_summary,
    load_product_policy,
)


def project_flag(action_env, name: str, default: bool = False) -> bool:
    fallback = "yes" if default else "no"
    value = str(action_env.GetProjectOption(name, fallback)).strip().lower()
    if value in ("1", "true", "yes", "on"):
        return True
    if value in ("0", "false", "no", "off"):
        return False
    raise RuntimeError(f"invalid boolean project option {name}={value!r}")


def project_text(action_env, name: str, default: str = "") -> str:
    return str(action_env.GetProjectOption(name, default)).strip()


def project_path(action_env, name: str) -> Path | None:
    value = project_text(action_env, name)
    if not value:
        return None
    path = Path(value)
    if not path.is_absolute():
        path = Path(action_env["PROJECT_DIR"]) / path
    return path.resolve()


def teensy_size_path(action_env) -> Path:
    package_dir = Path(action_env.PioPlatform().get_package_dir("tool-teensy"))
    for name in ("teensy_size", "teensy_size.exe"):
        candidate = package_dir / name
        if candidate.exists():
            return candidate
    raise RuntimeError(f"teensy_size not found under {package_dir}")


def arm_nm_path(action_env) -> Path:
    package_dir = Path(
        action_env.PioPlatform().get_package_dir("toolchain-gccarmnoneeabi-teensy")
    )
    for name in ("arm-none-eabi-nm", "arm-none-eabi-nm.exe"):
        candidate = package_dir / "bin" / name
        if candidate.exists():
            return candidate
    raise RuntimeError(f"arm-none-eabi-nm not found under {package_dir}")


def arm_readelf_path(action_env) -> Path:
    package_dir = Path(
        action_env.PioPlatform().get_package_dir("toolchain-gccarmnoneeabi-teensy")
    )
    for name in ("arm-none-eabi-readelf", "arm-none-eabi-readelf.exe"):
        candidate = package_dir / "bin" / name
        if candidate.exists():
            return candidate
    raise RuntimeError(f"arm-none-eabi-readelf not found under {package_dir}")


def elf_placement_violations(action_env, elf_path: Path) -> tuple[str, ...]:
    result = subprocess.run(
        [str(arm_nm_path(action_env)), "-S", "--radix=d", "-C", str(elf_path)],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"arm-none-eabi-nm failed for {elf_path}:\n{result.stderr}")
    violations = product_placement_violations(result.stdout)
    if project_flag(action_env, "custom_diagnostics_build"):
        violations += diagnostics_placement_violations(result.stdout)
    else:
        violations += normal_build_diagnostics_violations(result.stdout)
    return violations


def elf_topology_metadata(action_env, elf_path: Path) -> tuple[str, str]:
    sections = subprocess.run(
        [str(arm_readelf_path(action_env)), "-W", "-S", str(elf_path)],
        check=False,
        capture_output=True,
        text=True,
    )
    if sections.returncode != 0:
        raise RuntimeError(
            f"arm-none-eabi-readelf failed for {elf_path}:\n{sections.stderr}"
        )

    symbols = subprocess.run(
        [
            str(arm_nm_path(action_env)),
            "--defined-only",
            "--format=posix",
            "--radix=x",
            str(elf_path),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if symbols.returncode != 0:
        raise RuntimeError(f"arm-none-eabi-nm failed for {elf_path}:\n{symbols.stderr}")
    return sections.stdout, symbols.stdout


def write_post_link_evidence(
    elf_path: Path,
    profile_path: Path,
    teensy_size_output: str,
    section_metadata: str,
    symbol_metadata: str,
    result: TeensyPostLinkResult,
) -> None:
    evidence_dir = elf_path.parent / "post-link"
    evidence_dir.mkdir(parents=True, exist_ok=True)
    (evidence_dir / "teensy-size.txt").write_text(
        teensy_size_output, encoding="utf-8"
    )
    (evidence_dir / "sections.txt").write_text(section_metadata, encoding="utf-8")
    (evidence_dir / "symbols.txt").write_text(symbol_metadata, encoding="utf-8")
    report = {
        "schemaVersion": 1,
        "gateInterfaceVersion": POST_LINK_GATE_INTERFACE_VERSION,
        "profileSha256": hashlib.sha256(profile_path.read_bytes()).hexdigest(),
        "policy": asdict(result.policy),
        "usage": asdict(result.usage) if result.usage is not None else None,
        "physicalItcmBytes": result.physical_itcm_bytes,
        "copyItcmBytes": result.copy_itcm_bytes,
        "itcmBanks": result.itcm_banks,
        "passed": result.passed,
        "violations": list(result.violations),
        "advisories": list(result.advisories),
    }
    (evidence_dir / "report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def check_memory_budget(target, source, env) -> None:
    del source
    elf_path = Path(str(target[0]))
    result = subprocess.run(
        [str(teensy_size_path(env)), str(elf_path)],
        check=False,
        capture_output=True,
        text=True,
    )
    output = result.stdout + result.stderr
    if result.returncode != 0:
        raise RuntimeError(f"teensy_size failed for {elf_path}:\n{output}")

    section_metadata, symbol_metadata = elf_topology_metadata(env, elf_path)
    profile_path = project_path(env, "custom_post_link_profile")
    if profile_path is None:
        raise RuntimeError("custom_post_link_profile is required")
    vector = project_text(env, "custom_post_link_vector") or None
    policy = load_product_policy(profile_path, vector)
    post_link_result = evaluate_post_link(
        policy,
        output,
        section_metadata,
        symbol_metadata,
    )
    write_post_link_evidence(
        elf_path,
        profile_path,
        output,
        section_metadata,
        symbol_metadata,
        post_link_result,
    )
    print(format_post_link_summary(post_link_result))
    for advisory in post_link_result.advisories:
        print(f"  ! {advisory}")
    violations = elf_placement_violations(env, elf_path)
    violations += post_link_result.violations
    map_path = elf_path.with_suffix(".map")
    if not map_path.is_file():
        violations += (f"post-link map was not produced: {map_path}",)
    if violations:
        details = "\n".join(f"  - {item}" for item in violations)
        raise RuntimeError(
            f"Teensy memory, placement, or ELF topology gate failed:\n{details}"
        )


configured_profile = project_path(pio_env, "custom_post_link_profile")
if configured_profile is None:
    raise RuntimeError("custom_post_link_profile is required")
map_path = Path(pio_env.subst("$BUILD_DIR/${PROGNAME}.map")).resolve()
pio_env.Append(LINKFLAGS=[f"-Wl,-Map={map_path}"])


pio_env.AddPostAction(
    "$BUILD_DIR/${PROGNAME}.elf",
    env.VerboseAction(check_memory_budget, "Checking Teensy memory budget"),
)
