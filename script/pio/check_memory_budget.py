"""Reject Teensy firmware images that exceed the configured memory budgets."""

# pyright: reportUndefinedVariable=false

from pathlib import Path
import subprocess
import sys

Import("env")

pio_env = env

DEV_SCRIPT_DIR = Path(pio_env["PROJECT_DIR"]) / "script" / "dev"
sys.path.insert(0, str(DEV_SCRIPT_DIR))

from teensy_memory_budget import (  # noqa: E402
    TeensyMemoryBudget,
    budget_violations,
    parse_teensy_size,
    summary,
)
from teensy_diagnostics_placement import (  # noqa: E402
    diagnostics_placement_violations,
    normal_build_diagnostics_violations,
)
from teensy_product_placement import product_placement_violations  # noqa: E402


def project_option(action_env, name: str, default: int) -> int:
    return int(action_env.GetProjectOption(name, str(default)))


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
    if str(action_env.get("PIOENV", "")) == "dev_diagnostics":
        violations += diagnostics_placement_violations(result.stdout)
    else:
        violations += normal_build_diagnostics_violations(result.stdout)
    return violations


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

    usage = parse_teensy_size(output)
    budget = TeensyMemoryBudget(
        ram1_min_free=project_option(env, "custom_ram1_min_free", 114688),
        ram1_code_max=project_option(env, "custom_ram1_code_max", 294912),
        ram2_min_free=project_option(env, "custom_ram2_min_free", 196608),
        extram_capacity=project_option(env, "custom_extram_capacity", 8388608),
        extram_min_free=project_option(env, "custom_extram_min_free", 2097152),
    )
    print(summary(usage, budget))
    violations = budget_violations(usage, budget) + elf_placement_violations(
        env,
        elf_path,
    )
    if violations:
        details = "\n".join(f"  - {item}" for item in violations)
        raise RuntimeError(f"Teensy memory or placement gate failed:\n{details}")


pio_env.AddPostAction(
    "$BUILD_DIR/${PROGNAME}.elf",
    env.VerboseAction(check_memory_budget, "Checking Teensy memory budget"),
)
