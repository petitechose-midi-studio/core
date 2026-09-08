"""Content identity of local benchmark sources, dependencies and build recipe.

Generated metadata lives in the build directory, never in a source checkout.
The flashed artifact's SHA256 remains a separate, host-verifiable identity.
"""
import hashlib
import json
import subprocess
from pathlib import Path

Import("env")

core = Path(env.subst("$PROJECT_DIR")).resolve()
workspace = core.parents[1]
repositories = [
    "midi-studio/core", "midi-studio/device-support", "midi-studio/ui",
    "open-control/framework", "open-control/note", "open-control/hal-common",
    "open-control/hal-teensy", "open-control/ui-lvgl", "open-control/ui-lvgl-components",
]
extensions = {".c", ".cpp", ".h", ".hpp", ".S", ".ld", ".py", ".ini", ".json", ".cmake", ".inc", ".inl", ".tpp", ".ipp"}
manifest = {"schema": 1, "profile": env.subst("$PIOENV"), "files": {}}
manifest["recipe"] = {
    name: env.GetProjectOption(name, "") for name in (
        "platform", "board", "framework", "build_flags", "build_unflags",
        "build_src_filter", "board_build.f_cpu", "board_build.ldscript", "lib_deps",
    )
}
for relative in repositories:
    repo = workspace / relative
    names = subprocess.check_output(
        ["git", "-C", str(repo), "ls-files", "-z", "--cached", "--others", "--exclude-standard"]
    ).decode("utf-8").split("\0")
    for name in sorted(set(names)):
        path = repo / name
        if not name or not path.is_file() or (path.suffix not in extensions and path.name != "CMakeLists.txt"):
            continue
        manifest["files"][f"{relative}/{name}"] = hashlib.sha256(path.read_bytes()).hexdigest()

# Package metadata binds the versioned toolchain and Teensy core to the recipe.
for package in ("framework-arduinoteensy", "toolchain-gccarmnoneeabi-teensy"):
    package_dir = env.PioPlatform().get_package_dir(package)
    if not package_dir:
        raise RuntimeError(f"benchmark identity missing package: {package}")
    manifest[package] = json.loads((Path(package_dir) / "package.json").read_text(encoding="utf-8"))
    if package == "framework-arduinoteensy":
        for path in sorted((Path(package_dir) / "cores" / "teensy4").rglob("*")):
            if path.is_file() and path.suffix in extensions:
                manifest["files"][f"teensy4/{path.name}"] = hashlib.sha256(path.read_bytes()).hexdigest()

# Include resolved registry sources as well as the local Git repositories. A
# patched LVGL/driver cache must not masquerade as the unmodified package.
libdeps = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
local_roots = {(workspace / relative).resolve() for relative in repositories}
for library in sorted(libdeps.iterdir()):
    if not library.is_dir() or library.resolve() in local_roots:
        continue
    for path in sorted(library.rglob("*")):
        if path.is_file() and path.suffix in extensions:
            manifest["files"][f"libdeps/{path.relative_to(libdeps).as_posix()}"] = hashlib.sha256(path.read_bytes()).hexdigest()

canonical = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
identity = hashlib.sha256(canonical).hexdigest()
manifest["build_id"] = identity
destination = Path(env.subst("$BUILD_DIR")) / "hardware-benchmark-manifest.json"
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
# Only the RPC endpoint consumes this value. A global compiler definition
# changes every compilation command and rebuilds even unchanged dependencies.
header = destination.with_name("HardwareBenchmarkBuildIdentity.hpp")
content = '#pragma once\n#define MS_HARDWARE_BENCHMARK_BUILD_ID "' + identity + '"\n'
if not header.exists() or header.read_text(encoding="utf-8") != content:
    header.write_text(content, encoding="utf-8")
env.Append(CPPPATH=[str(destination.parent)])
