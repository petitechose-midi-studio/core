#!/usr/bin/env python3

"""Deterministic inventory for the supported MIDI Studio build topology."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
from typing import Any, Iterable, Sequence


CORE_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SNAPSHOT = Path(__file__).with_name("build-topology-snapshot.json")
TOOL_VERSION = 1
HISTORICAL_F07 = {
    "coreImplementations": 382,
    "coreHeaders": 426,
    "cmakeImplementations": 296,
    "teensyImplementations": 381,
    "bitwigCoreImplementations": 382,
    "sdlWasmCoreImplementations": 381,
}
IMPLEMENTATION_SUFFIXES = frozenset((".cc", ".cpp"))
C_SOURCE_SUFFIXES = frozenset((".c",))
COMPILABLE_SUFFIXES = IMPLEMENTATION_SUFFIXES | C_SOURCE_SUFFIXES
HEADER_SUFFIXES = frozenset((".h", ".hh", ".hpp"))
ALLOWED_WRITE_DIRTY_PATHS = frozenset(
    (
        "script/dev/build-topology-snapshot.json",
    )
)


class InventoryError(RuntimeError):
    pass


def run_git(repo: Path, *args: str) -> str:
    result = subprocess.run(
        ("git", "-C", str(repo), *args),
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise InventoryError(f"git {' '.join(args)} failed for {repo}: {detail}")
    return result.stdout


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise InventoryError(f"{label} not found: {path}")


def workspace_repositories(workspace_root: Path) -> dict[str, Path]:
    root = workspace_root.resolve()
    repositories = {
        "core": root / "midi-studio" / "core",
        "bitwig": root / "midi-studio" / "plugin-bitwig",
        "ui": root / "midi-studio" / "ui",
        "framework": root / "open-control" / "framework",
        "halCommon": root / "open-control" / "hal-common",
        "halTeensy": root / "open-control" / "hal-teensy",
        "halSdl": root / "open-control" / "hal-sdl",
        "halMidi": root / "open-control" / "hal-midi",
        "halNet": root / "open-control" / "hal-net",
        "note": root / "open-control" / "note",
        "uiLvgl": root / "open-control" / "ui-lvgl",
        "uiLvglComponents": root / "open-control" / "ui-lvgl-components",
    }
    sentinels = {
        "core": "platformio.ini",
        "bitwig": "platformio.ini",
        "ui": "library.json",
        "framework": "src/oc/Config.hpp",
        "halCommon": "library.json",
        "halTeensy": "library.json",
        "halSdl": "src/oc/hal/sdl/Sdl.hpp",
        "halMidi": "src/oc/hal/midi/LibreMidiTransport.hpp",
        "halNet": "src/oc/hal/net/UdpTransport.hpp",
        "note": "library.json",
        "uiLvgl": "library.json",
        "uiLvglComponents": "library.json",
    }
    for name, repo in repositories.items():
        require_file(repo / sentinels[name], f"{name} sentinel")
        if not (repo / ".git").exists():
            raise InventoryError(f"{name} is not a Git worktree: {repo}")
    if repositories["core"].resolve() != CORE_ROOT.resolve():
        raise InventoryError(
            "--workspace-root resolves a different Core checkout than this checker"
        )
    return repositories


def tracked_paths(repo: Path) -> list[str]:
    output = subprocess.run(
        ("git", "-C", str(repo), "ls-files", "-z"),
        check=False,
        capture_output=True,
    )
    if output.returncode != 0:
        raise InventoryError(f"git ls-files failed for {repo}")
    return sorted(
        entry.decode("utf-8").replace("\\", "/")
        for entry in output.stdout.split(b"\0")
        if entry
    )


def dirty_paths(repo: Path) -> list[str]:
    output = run_git(repo, "status", "--porcelain=v1", "--untracked-files=all")
    paths: list[str] = []
    for line in output.splitlines():
        if len(line) < 4:
            continue
        path = line[3:]
        if " -> " in path:
            path = path.split(" -> ", 1)[1]
        paths.append(path.replace("\\", "/"))
    return sorted(paths)


def git_identity(repo: Path) -> tuple[str, str]:
    values = run_git(repo, "show", "-s", "--format=%H%n%T", "HEAD").splitlines()
    if len(values) != 2:
        raise InventoryError(f"unable to read commit/tree identity for {repo}")
    return values[0], values[1]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def sha256_git_file(repo: Path, path: Path) -> str:
    try:
        relative_path = path.resolve().relative_to(repo.resolve()).as_posix()
    except ValueError as error:
        raise InventoryError(f"tracked input is outside {repo}: {path}") from error
    result = subprocess.run(
        ("git", "-C", str(repo), "show", f"HEAD:{relative_path}"),
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise InventoryError(
            f"unable to read tracked input {relative_path} from {repo}: {detail}"
        )
    return sha256_bytes(result.stdout)


def path_set_record(paths: Iterable[str]) -> dict[str, Any]:
    normalized = sorted(set(path.replace("\\", "/") for path in paths))
    payload = "".join(f"{path}\n" for path in normalized).encode("utf-8")
    return {
        "count": len(normalized),
        "sha256": sha256_bytes(payload),
        "paths": normalized,
    }


def filtered_paths(
    paths: Iterable[str], prefix: str, suffixes: frozenset[str]
) -> list[str]:
    normalized_prefix = prefix.rstrip("/") + "/"
    return [
        path
        for path in paths
        if path.startswith(normalized_prefix)
        and PurePosixPath(path).suffix.lower() in suffixes
    ]


def extract_cmake_command_bodies(
    text: str, command: str, variable: str, *, append: bool = False
) -> list[str]:
    if command == "set":
        start_pattern = re.compile(rf"\bset\s*\(\s*{re.escape(variable)}\b")
    elif command == "list" and append:
        start_pattern = re.compile(
            rf"\blist\s*\(\s*APPEND\s+{re.escape(variable)}\b"
        )
    else:
        raise ValueError("unsupported CMake command")

    bodies: list[str] = []
    for match in start_pattern.finditer(text):
        index = match.end()
        depth = 1
        in_quote = False
        escaped = False
        while index < len(text) and depth:
            char = text[index]
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_quote = not in_quote
            elif not in_quote:
                if char == "(":
                    depth += 1
                elif char == ")":
                    depth -= 1
            index += 1
        if depth:
            raise InventoryError(f"unterminated CMake command for {variable}")
        bodies.append(text[match.end() : index - 1])
    return bodies


def quoted_cmake_values(body: str) -> list[str]:
    return re.findall(r'"([^"\r\n]+)"', body)


def expand_core_native_sources(core: Path, tracked: set[str]) -> list[str]:
    recipe = (core / "cmake" / "MsCoreSources.cmake").read_text(encoding="utf-8")
    pattern_bodies = extract_cmake_command_bodies(
        recipe, "set", "MS_CORE_NATIVE_SOURCE_PATTERNS"
    )
    extra_bodies = extract_cmake_command_bodies(
        recipe, "set", "MS_CORE_NATIVE_EXTRA_SOURCES"
    ) + extract_cmake_command_bodies(
        recipe, "list", "MS_CORE_NATIVE_EXTRA_SOURCES", append=True
    )
    if len(pattern_bodies) != 1 or not extra_bodies:
        raise InventoryError("unable to identify the Core native source declarations")

    source_specs = []
    for value in quoted_cmake_values(pattern_bodies[0]):
        prefix = "${MS_CORE_SOURCE_ROOT}/"
        if value.startswith(prefix):
            source_specs.append(value[len(prefix) :])

    selected: set[str] = set()
    for spec in source_specs:
        if "*" not in spec:
            selected.add(f"src/{spec}")
            continue
        first_wildcard = spec.index("*")
        base_text = spec[:first_wildcard].rstrip("/")
        base_dir = core / "src" / PurePosixPath(base_text)
        suffix = PurePosixPath(spec).suffix or ".cpp"
        if not base_dir.is_dir():
            raise InventoryError(f"Core native glob base is missing: {base_text}")
        for path in base_dir.rglob(f"*{suffix}"):
            selected.add(path.relative_to(core).as_posix())

    for body in extra_bodies:
        for value in quoted_cmake_values(body):
            prefix = "${MS_CORE_SOURCE_ROOT}/"
            if value.startswith(prefix):
                selected.add(f"src/{value[len(prefix):]}")

    missing = sorted(path for path in selected if path not in tracked)
    if missing:
        raise InventoryError(f"Core native declarations reference missing files: {missing}")
    return sorted(selected)


def parse_platformio(text: str) -> dict[str, dict[str, list[str]]]:
    sections: dict[str, dict[str, list[str]]] = {}
    section: str | None = None
    key: str | None = None
    for raw_line in text.splitlines():
        stripped = raw_line.strip()
        if not stripped or stripped.startswith((";", "#")):
            continue
        section_match = re.fullmatch(r"\[([^]]+)\]", stripped)
        if section_match:
            section = section_match.group(1)
            sections.setdefault(section, {})
            key = None
            continue
        key_match = re.match(r"^([A-Za-z0-9_.:-]+)\s*=\s*(.*)$", raw_line)
        if key_match and section is not None:
            key = key_match.group(1)
            sections[section][key] = []
            value = key_match.group(2).strip()
            if value:
                sections[section][key].append(value)
            continue
        if section is not None and key is not None and raw_line[:1].isspace():
            sections[section][key].append(stripped)
    return sections


def pio_values(
    sections: dict[str, dict[str, list[str]]], section: str, key: str
) -> list[str]:
    try:
        return sections[section][key]
    except KeyError as error:
        raise InventoryError(f"missing PlatformIO declaration [{section}] {key}") from error


def pio_first(
    sections: dict[str, dict[str, list[str]]], section: str, key: str
) -> str:
    values = pio_values(sections, section, key)
    if len(values) != 1:
        raise InventoryError(f"expected one PlatformIO value for [{section}] {key}")
    return values[0]


def pio_pattern_matches(path: str, pattern: str) -> bool:
    normalized = pattern.replace("\\", "/")
    if normalized == "*":
        return True
    if normalized.endswith("/"):
        return path.startswith(normalized)
    if "*" not in normalized and "?" not in normalized:
        return path == normalized or path.startswith(normalized.rstrip("/") + "/")
    return PurePosixPath(path).match(normalized)


def apply_pio_filters(paths: Sequence[str], rules: Sequence[str]) -> list[str]:
    parsed_rules: list[tuple[bool, str]] = []
    for value in rules:
        for match in re.finditer(r"([+-])<([^>]+)>", value):
            parsed_rules.append((match.group(1) == "+", match.group(2)))
    if not parsed_rules:
        raise InventoryError("no PlatformIO source filter rules found")
    include_rules = [pattern for include, pattern in parsed_rules if include]
    exclude_rules = [pattern for include, pattern in parsed_rules if not include]
    selected = []
    for path in paths:
        included = not include_rules or any(
            pio_pattern_matches(path, pattern) for pattern in include_rules
        )
        excluded = any(pio_pattern_matches(path, pattern) for pattern in exclude_rules)
        if included and not excluded:
            selected.append(path)
    return sorted(selected)


def extract_defines(values: Sequence[str]) -> dict[str, str | bool]:
    definitions: dict[str, str | bool] = {}
    for value in values:
        match = re.fullmatch(r"-D\s*([A-Za-z_][A-Za-z0-9_]*)(?:=(.+))?", value)
        if match:
            definitions[match.group(1)] = match.group(2) or True
    return definitions


def extract_cpp_standard(values: Sequence[str]) -> str | None:
    for value in values:
        match = re.fullmatch(r"-std=(.+)", value)
        if match:
            return match.group(1)
    return None


def cmake_scalar(text: str, variable: str) -> str | None:
    match = re.search(
        rf"\bset\s*\(\s*{re.escape(variable)}\s+\"?([^\s\")]+)", text
    )
    return match.group(1) if match else None


def cmake_target_cxx_standard(text: str, target: str) -> int:
    match = re.search(
        rf"\btarget_compile_features\s*\(\s*{re.escape(target)}\s+"
        rf"(?:PUBLIC|PRIVATE|INTERFACE)\s+cxx_std_(\d+)\s*\)",
        text,
        re.DOTALL,
    )
    if match is None:
        raise InventoryError(f"CMake C++ standard not found for target: {target}")
    return int(match.group(1))


def macro_defaults(framework: Path) -> dict[str, int]:
    config = (framework / "src" / "oc" / "Config.hpp").read_text(encoding="utf-8")
    cobs = (framework / "src" / "oc" / "codec" / "CobsCodec.hpp").read_text(
        encoding="utf-8"
    )
    names = (
        "OC_MAX_PENDING_NOTIFICATIONS",
        "OC_MAX_BUTTONS",
        "OC_MAX_BUTTON_BINDINGS",
        "OC_MAX_ENCODER_BINDINGS",
    )
    defaults: dict[str, int] = {}
    for name in names:
        match = re.search(rf"#define\s+{name}\s+(\d+)", config)
        if not match:
            raise InventoryError(f"framework default not found: {name}")
        defaults[name] = int(match.group(1))
    cobs_match = re.search(r"#define\s+OC_COBS_MAX_FRAME_SIZE\s+(\d+)", cobs)
    if not cobs_match:
        raise InventoryError("framework default not found: OC_COBS_MAX_FRAME_SIZE")
    defaults["OC_COBS_MAX_FRAME_SIZE"] = int(cobs_match.group(1))
    return defaults


def effective_capacity(
    defaults: dict[str, int], definitions: dict[str, str | bool], name: str
) -> int:
    value = definitions.get(name)
    return int(value) if isinstance(value, str) else defaults[name]


def pin_map(ini_text: str) -> dict[str, str]:
    pins: dict[str, str] = {}
    for name, commit in re.findall(
        r"^\s*([A-Za-z0-9_.-]+)=https?://[^#\s]+#([0-9a-f]{40})\s*$",
        ini_text,
        flags=re.MULTILINE,
    ):
        pins[name] = commit
    return dict(sorted(pins.items()))


def yaml_scalar(text: str, name: str) -> str | None:
    match = re.search(
        rf"^\s*{re.escape(name)}:\s*[\"']?([^\"'\s#]+)",
        text,
        flags=re.MULTILINE,
    )
    return match.group(1) if match else None


def workflow_sha_map(text: str) -> dict[str, str]:
    return dict(
        sorted(
            re.findall(
                r"^\s*([A-Z][A-Z0-9_]*_SHA):\s*[\"']?([0-9a-f]{40})",
                text,
                flags=re.MULTILINE,
            )
        )
    )


def cmake_minimum(text: str) -> str | None:
    match = re.search(
        r"\bcmake_minimum_required\s*\(\s*VERSION\s+([^\s\)]+)", text
    )
    return match.group(1) if match else None


def source_metadata(repo: Path, tracked: list[str]) -> dict[str, Any]:
    sources = [
        path
        for path in tracked
        if path.startswith("src/") and PurePosixPath(path).suffix in COMPILABLE_SUFFIXES
    ]
    cmake_path = repo / "CMakeLists.txt"
    cmake_text = cmake_path.read_text(encoding="utf-8") if cmake_path.is_file() else ""
    manifest_path = repo / "library.json"
    manifest = (
        json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest_path.is_file()
        else None
    )
    library_targets = sorted(set(re.findall(r"\badd_library\s*\(\s*([A-Za-z0-9_:.-]+)", cmake_text)))
    has_recursive_glob = "GLOB_RECURSE" in cmake_text
    has_explicit_source_list = bool(library_targets) and not has_recursive_glob
    src_filter = None
    if isinstance(manifest, dict):
        src_filter = manifest.get("build", {}).get("srcFilter")
    return {
        "sources": path_set_record(sources),
        "cmakeLibraryTargets": library_targets,
        "cmakeRecursiveGlob": has_recursive_glob,
        "cmakeExplicitSourceList": has_explicit_source_list,
        "platformioSourceFilter": src_filter,
    }


def classify_supplier_lots(metadata: dict[str, dict[str, Any]]) -> dict[str, Any]:
    framework = metadata["framework"]
    note = metadata["note"]
    ui_lvgl = metadata["uiLvgl"]
    hal_members = [metadata[name] for name in ("halCommon", "halTeensy", "halSdl", "halMidi", "halNet")]
    return {
        "L-R18-02F": (
            "activate-explicit-owned-list"
            if not framework["cmakeExplicitSourceList"]
            else "evidence-only-existing-list"
        ),
        "L-R18-02H": (
            "activate-targets-and-explicit-owned-lists"
            if any(not member["cmakeExplicitSourceList"] for member in hal_members)
            else "evidence-only-existing-lists"
        ),
        "L-R18-02N": (
            "activate-explicit-owned-list"
            if not note["cmakeExplicitSourceList"]
            else "evidence-only-existing-list"
        ),
        "L-R18-02U": (
            "activate-target-and-explicit-owned-list"
            if not ui_lvgl["cmakeExplicitSourceList"]
            else "evidence-only-existing-list"
        ),
    }


def input_hashes(repositories: dict[str, Path]) -> dict[str, str]:
    inputs = {
        "core/CMakeLists.txt": repositories["core"] / "CMakeLists.txt",
        "core/cmake/MsCoreProjectFileTool.cmake": (
            repositories["core"] / "cmake" / "MsCoreProjectFileTool.cmake"
        ),
        "core/cmake/MsCoreSources.cmake": repositories["core"] / "cmake" / "MsCoreSources.cmake",
        "core/platformio.ini": repositories["core"] / "platformio.ini",
        "core/library.json": repositories["core"] / "library.json",
        "core/oc-sdk.ini": repositories["core"] / "oc-sdk.ini",
        "core/oc-native-sdk.ini": repositories["core"] / "oc-native-sdk.ini",
        "core/sdl/CMakeLists.txt": repositories["core"] / "sdl" / "CMakeLists.txt",
        "core/sdl/app.cmake": repositories["core"] / "sdl" / "app.cmake",
        "core/sdl/cmake/libremidi.cmake": repositories["core"] / "sdl" / "cmake" / "libremidi.cmake",
        "core/.github/release-tooling.json": repositories["core"] / ".github" / "release-tooling.json",
        "core/.github/workflows/ci.yml": repositories["core"] / ".github" / "workflows" / "ci.yml",
        "core/.github/workflows/candidate.yml": repositories["core"] / ".github" / "workflows" / "candidate.yml",
        "core/.github/workflows/candidate-host-tools.yml": repositories["core"] / ".github" / "workflows" / "candidate-host-tools.yml",
        "core/script/dev/check-build-topology.py": Path(__file__).resolve(),
        "bitwig/CMakeLists.txt": repositories["bitwig"] / "CMakeLists.txt",
        "bitwig/platformio.ini": repositories["bitwig"] / "platformio.ini",
        "bitwig/sdl/app.cmake": repositories["bitwig"] / "sdl" / "app.cmake",
        "bitwig/.github/release-tooling.json": repositories["bitwig"] / ".github" / "release-tooling.json",
        "bitwig/.github/workflows/ci.yml": repositories["bitwig"] / ".github" / "workflows" / "ci.yml",
        "bitwig/.github/workflows/candidate-firmware.yml": repositories["bitwig"] / ".github" / "workflows" / "candidate-firmware.yml",
        "ui/CMakeLists.txt": repositories["ui"] / "CMakeLists.txt",
        "ui/library.json": repositories["ui"] / "library.json",
    }
    for label, path in inputs.items():
        require_file(path, label)
    return {
        label: sha256_git_file(repositories[label.split("/", 1)[0]], path)
        for label, path in sorted(inputs.items())
    }


def build_inventory(workspace_root: Path) -> dict[str, Any]:
    repositories = workspace_repositories(workspace_root)
    with ThreadPoolExecutor(max_workers=min(12, len(repositories))) as executor:
        tracked_values = executor.map(tracked_paths, repositories.values())
        tracked = dict(zip(repositories, tracked_values))

    core = repositories["core"]
    bitwig = repositories["bitwig"]
    framework = repositories["framework"]
    core_sources = filtered_paths(tracked["core"], "src", IMPLEMENTATION_SUFFIXES)
    core_c_sources = filtered_paths(tracked["core"], "src", C_SOURCE_SUFFIXES)
    core_headers = filtered_paths(tracked["core"], "src", HEADER_SUFFIXES)
    device_support_sources = filtered_paths(
        tracked["core"], "device-support/src", IMPLEMENTATION_SUFFIXES
    )
    device_support_headers = filtered_paths(
        tracked["core"], "device-support/src", HEADER_SUFFIXES
    )
    bitwig_sources = filtered_paths(
        tracked["bitwig"], "src", IMPLEMENTATION_SUFFIXES
    )
    bitwig_c_sources = filtered_paths(tracked["bitwig"], "src", C_SOURCE_SUFFIXES)
    ui_sources = filtered_paths(tracked["ui"], "src", IMPLEMENTATION_SUFFIXES)
    ui_c_sources = filtered_paths(tracked["ui"], "src", C_SOURCE_SUFFIXES)

    core_pio_text = (core / "platformio.ini").read_text(encoding="utf-8")
    core_pio = parse_platformio(core_pio_text)
    bitwig_pio_text = (bitwig / "platformio.ini").read_text(encoding="utf-8")
    bitwig_pio = parse_platformio(bitwig_pio_text)
    core_native_sources = expand_core_native_sources(core, set(tracked["core"]))
    core_teensy_sources = apply_pio_filters(
        core_sources, pio_values(core_pio, "env:teensy_base", "build_src_filter")
    )
    core_sdl_sources = sorted(
        path
        for path in core_sources
        if "/platform-teensy/" not in f"/{path}/"
        and PurePosixPath(path).name not in ("main.cpp", "name.c")
    )

    bitwig_ignored = set(pio_values(bitwig_pio, "env", "lib_ignore"))
    bitwig_core_sources: list[str] = [] if "ms-core" in bitwig_ignored else core_sources

    defaults = macro_defaults(framework)
    core_cmake_text = (core / "CMakeLists.txt").read_text(encoding="utf-8")
    project_file_tool_cmake_text = (
        core / "cmake" / "MsCoreProjectFileTool.cmake"
    ).read_text(encoding="utf-8")
    sdl_cmake_text = (core / "sdl" / "CMakeLists.txt").read_text(encoding="utf-8")
    libremidi_cmake_text = (
        core / "sdl" / "cmake" / "libremidi.cmake"
    ).read_text(encoding="utf-8")
    libremidi_match = re.search(
        r"GIT_TAG\s+([^\s\)]+)",
        libremidi_cmake_text,
    )
    if libremidi_match is None:
        raise InventoryError("libremidi FetchContent revision is missing")
    libremidi_revision = libremidi_match.group(1)
    bitwig_cmake_text = (bitwig / "CMakeLists.txt").read_text(encoding="utf-8")
    core_teensy_flags = pio_values(core_pio, "env:teensy_base", "build_flags")
    core_native_flags = pio_values(core_pio, "env:native", "build_flags")
    bitwig_flags = pio_values(bitwig_pio, "env", "build_flags")
    core_teensy_defines = extract_defines(core_teensy_flags)
    core_native_defines = extract_defines(core_native_flags)
    bitwig_defines = extract_defines(bitwig_flags)
    # CMake definitions are written without -D, so normalize them separately.
    sdl_defines = {}
    for name, value in re.findall(r"\b(OC_[A-Z0-9_]+)=(\d+)\b", sdl_cmake_text):
        sdl_defines[name] = value

    external_identities: dict[str, Any] = {}
    external_repositories = {
        name: repo for name, repo in repositories.items() if name != "core"
    }
    with ThreadPoolExecutor(max_workers=min(11, len(external_repositories))) as executor:
        identity_values = dict(
            zip(external_repositories, executor.map(git_identity, external_repositories.values()))
        )
    for name, repo in repositories.items():
        if name == "core":
            external_identities[name] = {
                "identityPolicy": "tracked-input-and-source-set-content",
                "cleanExpected": True,
            }
        else:
            commit, tree = identity_values[name]
            external_identities[name] = {
                "commit": commit,
                "tree": tree,
                "cleanExpected": True,
            }

    supplier_names = (
        "framework",
        "halCommon",
        "halTeensy",
        "halSdl",
        "halMidi",
        "halNet",
        "note",
        "uiLvgl",
    )
    supplier_metadata = {
        name: source_metadata(repositories[name], tracked[name])
        for name in supplier_names
    }

    core_oc_sdk = (core / "oc-sdk.ini").read_text(encoding="utf-8")
    core_native_sdk = (core / "oc-native-sdk.ini").read_text(encoding="utf-8")
    core_ci = (core / ".github" / "workflows" / "ci.yml").read_text(
        encoding="utf-8"
    )
    core_candidate = (
        core / ".github" / "workflows" / "candidate.yml"
    ).read_text(encoding="utf-8")
    bitwig_ci = (
        bitwig / ".github" / "workflows" / "ci.yml"
    ).read_text(encoding="utf-8")
    bitwig_candidate = (
        bitwig / ".github" / "workflows" / "candidate-firmware.yml"
    ).read_text(encoding="utf-8")
    core_release_tooling = json.loads(
        (core / ".github" / "release-tooling.json").read_text(encoding="utf-8")
    )
    bitwig_release_tooling = json.loads(
        (bitwig / ".github" / "release-tooling.json").read_text(encoding="utf-8")
    )
    bitwig_platform = pio_first(bitwig_pio, "env", "platform")
    bitwig_has_memory_gate = "check_memory_budget.py" in bitwig_pio_text
    bitwig_has_linker = bool(re.search(r"^\s*board_build\.ldscript\s*=", bitwig_pio_text, re.MULTILINE))

    app_extra_include_declarations = len(
        re.findall(r"\bset\s*\(\s*APP_EXTRA_INCLUDES\b", sdl_cmake_text)
    ) + sum(
        len(re.findall(r"\bset\s*\(\s*APP_EXTRA_INCLUDES\b", (repo / "sdl" / "app.cmake").read_text(encoding="utf-8")))
        for repo in (core, bitwig)
    )
    app_extra_include_target_uses = len(
        re.findall(r"target_include_directories\s*\([^)]*\$\{APP_EXTRA_INCLUDES\}", sdl_cmake_text, re.DOTALL)
    )
    app_extra_source_append_uses = len(
        re.findall(r"list\s*\(\s*APPEND\s+SRC_APP\s+\$\{APP_EXTRA_SOURCES\}", sdl_cmake_text)
    )

    inventory = {
        "schema": 1,
        "toolVersion": TOOL_VERSION,
        "historicalF07": HISTORICAL_F07,
        "repositories": external_identities,
        "inputSha256": input_hashes(repositories),
        "sourceSets": {
            "coreTotalImplementations": path_set_record(core_sources),
            "coreCSources": path_set_record(core_c_sources),
            "coreTotalHeaders": path_set_record(core_headers),
            "coreNativeCmakeImplementations": path_set_record(core_native_sources),
            "coreTeensyImplementations": path_set_record(core_teensy_sources),
            "coreSdlWasmImplementations": path_set_record(core_sdl_sources),
            "deviceSupportImplementations": path_set_record(device_support_sources),
            "deviceSupportHeaders": path_set_record(device_support_headers),
            "bitwigImplementations": path_set_record(bitwig_sources),
            "bitwigCSources": path_set_record(bitwig_c_sources),
            "bitwigCoreImplementations": path_set_record(bitwig_core_sources),
            "bitwigDeviceSupportImplementations": path_set_record(device_support_sources),
            "uiImplementations": path_set_record(ui_sources),
            "uiCSources": path_set_record(ui_c_sources),
        },
        "profiles": {
            "frameworkDefaults": defaults,
            "coreNativeCmake": {
                "cxxStandard": int(cmake_scalar(core_cmake_text, "CMAKE_CXX_STANDARD") or 0),
                "pendingNotifications": int(
                    cmake_scalar(core_cmake_text, "MS_CORE_NOTIFICATION_QUEUE_CAPACITY") or 0
                ),
                "buttons": defaults["OC_MAX_BUTTONS"],
                "buttonBindings": defaults["OC_MAX_BUTTON_BINDINGS"],
                "encoderBindings": defaults["OC_MAX_ENCODER_BINDINGS"],
                "cobsFrame": defaults["OC_COBS_MAX_FRAME_SIZE"],
            },
            "coreProjectFileTool": {
                "cxxStandard": cmake_target_cxx_standard(
                    project_file_tool_cmake_text,
                    "ms_core_project_file_open_control_native",
                ),
            },
            "deviceSupportNative": {
                "cxxStandard": 17,
                "extensions": False,
            },
            "coreTeensy": {
                "cxxStandard": extract_cpp_standard(core_teensy_flags),
                "platform": pio_first(core_pio, "env:teensy_base", "platform"),
                "board": pio_first(core_pio, "env:teensy_base", "board"),
                "cpuHz": int(pio_first(core_pio, "env:teensy_base", "board_build.f_cpu").rstrip("L")),
                "linker": pio_first(core_pio, "env:teensy_base", "board_build.ldscript"),
                "pendingNotifications": effective_capacity(defaults, core_teensy_defines, "OC_MAX_PENDING_NOTIFICATIONS"),
                "buttons": effective_capacity(defaults, core_teensy_defines, "OC_MAX_BUTTONS"),
                "buttonBindings": effective_capacity(defaults, core_teensy_defines, "OC_MAX_BUTTON_BINDINGS"),
                "encoderBindings": effective_capacity(defaults, core_teensy_defines, "OC_MAX_ENCODER_BINDINGS"),
                "cobsFrame": effective_capacity(defaults, core_teensy_defines, "OC_COBS_MAX_FRAME_SIZE"),
                "ram1MinFree": int(pio_first(core_pio, "env:teensy_base", "custom_ram1_min_free")),
                "itcmMax": int(pio_first(core_pio, "env:teensy_base", "custom_ram1_code_max")),
                "ram2MinFree": int(pio_first(core_pio, "env:teensy_base", "custom_ram2_min_free")),
                "psramCapacity": int(pio_first(core_pio, "env:teensy_base", "custom_extram_capacity")),
                "psramMinFree": int(pio_first(core_pio, "env:teensy_base", "custom_extram_min_free")),
                "postLinkMemoryGate": "script/pio/check_memory_budget.py",
            },
            "corePioNative": {
                "cxxStandard": extract_cpp_standard(core_native_flags),
                "pendingNotifications": effective_capacity(defaults, core_native_defines, "OC_MAX_PENDING_NOTIFICATIONS"),
                "buttons": effective_capacity(defaults, core_native_defines, "OC_MAX_BUTTONS"),
                "buttonBindings": effective_capacity(defaults, core_native_defines, "OC_MAX_BUTTON_BINDINGS"),
                "encoderBindings": effective_capacity(defaults, core_native_defines, "OC_MAX_ENCODER_BINDINGS"),
                "cobsFrame": effective_capacity(defaults, core_native_defines, "OC_COBS_MAX_FRAME_SIZE"),
            },
            "bitwigCmakeTests": {
                "cxxStandard": int(cmake_scalar(bitwig_cmake_text, "CMAKE_CXX_STANDARD") or 0),
            },
            "bitwigTeensy": {
                "declaredCxxStandard": extract_cpp_standard(bitwig_flags),
                "platform": bitwig_platform,
                "platformPinned": "@" in bitwig_platform,
                "board": pio_first(bitwig_pio, "env", "board"),
                "cpuHz": int(pio_first(bitwig_pio, "env", "board_build.f_cpu").rstrip("L")),
                "linker": None,
                "postLinkMemoryGate": bitwig_has_memory_gate,
                "pendingNotifications": effective_capacity(defaults, bitwig_defines, "OC_MAX_PENDING_NOTIFICATIONS"),
                "buttons": effective_capacity(defaults, bitwig_defines, "OC_MAX_BUTTONS"),
                "buttonBindings": effective_capacity(defaults, bitwig_defines, "OC_MAX_BUTTON_BINDINGS"),
                "encoderBindings": effective_capacity(defaults, bitwig_defines, "OC_MAX_ENCODER_BINDINGS"),
                "cobsFrame": effective_capacity(defaults, bitwig_defines, "OC_COBS_MAX_FRAME_SIZE"),
                "explicitLinkerDeclared": bitwig_has_linker,
            },
            "sdlWasm": {
                "cxxStandard": int(cmake_scalar(sdl_cmake_text, "CMAKE_CXX_STANDARD") or 0),
                "pendingNotifications": effective_capacity(defaults, sdl_defines, "OC_MAX_PENDING_NOTIFICATIONS"),
                "buttons": effective_capacity(defaults, sdl_defines, "OC_MAX_BUTTONS"),
                "buttonBindings": effective_capacity(defaults, sdl_defines, "OC_MAX_BUTTON_BINDINGS"),
                "encoderBindings": effective_capacity(defaults, sdl_defines, "OC_MAX_ENCODER_BINDINGS"),
                "cobsFrame": effective_capacity(defaults, sdl_defines, "OC_COBS_MAX_FRAME_SIZE"),
            },
        },
        "dependencies": {
            "coreReleasePins": pin_map(core_oc_sdk),
            "coreNativePins": pin_map(core_native_sdk),
            "bitwigReleaseImportsCoreOcSdk": "${oc_sdk_deps.lib_deps}" in bitwig_pio_text,
            "bitwigDevUsesLocalSymlinks": "symlink://" in "\n".join(pio_values(bitwig_pio, "env:dev", "lib_deps")),
            "libremidi": {
                "repository": "https://github.com/celtera/libremidi.git",
                "revision": libremidi_revision,
                "immutableCommit": bool(
                    re.fullmatch(r"[0-9a-f]{40}", libremidi_revision)
                ),
            },
        },
        "tooling": {
            "cmakeMinimum": {
                "core": cmake_minimum(core_cmake_text),
                "bitwig": cmake_minimum(bitwig_cmake_text),
                "ui": cmake_minimum(
                    (repositories["ui"] / "CMakeLists.txt").read_text(encoding="utf-8")
                ),
                "sdlWasm": cmake_minimum(sdl_cmake_text),
            },
            "coreCi": {
                "platformio": yaml_scalar(core_ci, "PLATFORMIO_VERSION"),
                "msDevEnv": yaml_scalar(core_ci, "MS_DEV_ENV_SHA"),
                "declaredRepositoryPins": workflow_sha_map(core_ci),
            },
            "coreCandidate": {
                "platformio": yaml_scalar(core_candidate, "PLATFORMIO_VERSION"),
                "releaseTooling": core_release_tooling,
            },
            "bitwigCi": {
                "delegatesToMsDevEnv": "uv run ms build bitwig" in bitwig_ci,
                "releaseTooling": bitwig_release_tooling,
            },
            "bitwigCandidate": {
                "platformio": yaml_scalar(bitwig_candidate, "PLATFORMIO_VERSION"),
                "requiresCoreShaInput": "core_sha:" in bitwig_candidate,
                "requiresToolingShaInput": "tooling_sha:" in bitwig_candidate,
            },
        },
        "sdlTopology": {
            "explicitDependencyRoots": False,
            "implicitSiblingRoots": True,
            "pioLibdepsFallback": ".pio/libdeps/dev" in sdl_cmake_text,
            "consumerRecursiveSourceGlobs": len(re.findall(r"file\s*\(\s*GLOB_RECURSE", sdl_cmake_text)),
            "appExtraIncludesDeclarations": app_extra_include_declarations,
            "appExtraIncludesTargetUses": app_extra_include_target_uses,
            "appExtraSourcesAppendUses": app_extra_source_append_uses,
            "bitwigDeviceSupportIncludeRootExposed": "device-support/src" in sdl_cmake_text,
        },
        "suppliers": supplier_metadata,
        "conditionalSupplierLots": classify_supplier_lots(supplier_metadata),
    }
    serialized = json.dumps(inventory, sort_keys=True)
    if str(workspace_root.resolve()).replace("\\", "/") in serialized.replace("\\", "/"):
        raise InventoryError("inventory contains an absolute workspace path")
    return inventory


def structural_diff(expected: Any, actual: Any, path: str = "$") -> list[str]:
    differences: list[str] = []
    if type(expected) is not type(actual):
        return [f"{path}: type {type(expected).__name__} != {type(actual).__name__}"]
    if isinstance(expected, dict):
        for key in sorted(set(expected) | set(actual)):
            child = f"{path}.{key}"
            if key not in expected:
                differences.append(f"{child}: unexpected")
            elif key not in actual:
                differences.append(f"{child}: missing")
            else:
                differences.extend(structural_diff(expected[key], actual[key], child))
        return differences
    if isinstance(expected, list):
        if expected != actual:
            expected_set = set(expected) if all(isinstance(item, str) for item in expected) else None
            actual_set = set(actual) if all(isinstance(item, str) for item in actual) else None
            if expected_set is not None and actual_set is not None:
                added = sorted(actual_set - expected_set)
                removed = sorted(expected_set - actual_set)
                differences.append(
                    f"{path}: added={added[:5]} removed={removed[:5]}"
                )
            else:
                differences.append(f"{path}: list differs")
        return differences
    if expected != actual:
        differences.append(f"{path}: expected={expected!r} actual={actual!r}")
    return differences


def validate_worktrees(
    repositories: dict[str, Path], *, writing_snapshot: bool
) -> list[str]:
    errors: list[str] = []
    with ThreadPoolExecutor(max_workers=min(12, len(repositories))) as executor:
        dirty_values = dict(zip(repositories, executor.map(dirty_paths, repositories.values())))
    for name, dirty in dirty_values.items():
        if not dirty:
            continue
        if writing_snapshot and name == "core" and set(dirty).issubset(ALLOWED_WRITE_DIRTY_PATHS):
            continue
        errors.append(f"{name} worktree is dirty: {dirty}")
    return errors


def run_self_test() -> None:
    assert path_set_record(["b.cpp", "a.cpp", "a.cpp"])["paths"] == [
        "a.cpp",
        "b.cpp",
    ]
    assert path_set_record(["a.cpp", "b.cpp"])["sha256"] == path_set_record(
        ["b.cpp", "a.cpp"]
    )["sha256"]

    pio = parse_platformio(
        """
[env:test]
build_src_filter =
    +<*>
    -<src/excluded/>
build_flags =
    -std=gnu++17
    -D OC_MAX_BUTTONS=48
"""
    )
    selected = apply_pio_filters(
        ["src/a.cpp", "src/excluded/b.cpp"],
        pio_values(pio, "env:test", "build_src_filter"),
    )
    assert selected == ["src/a.cpp"]
    assert apply_pio_filters(
        ["src/a.cpp", "src/excluded/b.cpp"],
        ["+<*> -<src/excluded/>"]
    ) == ["src/a.cpp"]
    assert extract_cpp_standard(pio_values(pio, "env:test", "build_flags")) == "gnu++17"
    assert extract_defines(pio_values(pio, "env:test", "build_flags")) == {
        "OC_MAX_BUTTONS": "48"
    }

    cmake = 'set(TEST_VALUES "${ROOT}/a.cpp" "${ROOT}/b.cpp")\nlist(APPEND TEST_VALUES "${ROOT}/c.cpp")'
    set_bodies = extract_cmake_command_bodies(cmake, "set", "TEST_VALUES")
    append_bodies = extract_cmake_command_bodies(
        cmake, "list", "TEST_VALUES", append=True
    )
    assert quoted_cmake_values(set_bodies[0]) == ["${ROOT}/a.cpp", "${ROOT}/b.cpp"]
    assert quoted_cmake_values(append_bodies[0]) == ["${ROOT}/c.cpp"]

    differences = structural_diff(
        {"source": {"count": 1, "paths": ["a.cpp"]}},
        {"source": {"count": 2, "paths": ["a.cpp", "b.cpp"]}},
    )
    assert any("count" in difference for difference in differences)
    assert any("b.cpp" in difference for difference in differences)

    workflow = """
env:
  PLATFORMIO_VERSION: "6.1.18"
  DEPENDENCY_SHA: 0123456789abcdef0123456789abcdef01234567
"""
    assert yaml_scalar(workflow, "PLATFORMIO_VERSION") == "6.1.18"
    assert workflow_sha_map(workflow) == {
        "DEPENDENCY_SHA": "0123456789abcdef0123456789abcdef01234567"
    }
    assert cmake_minimum("cmake_minimum_required(VERSION 3.29)") == "3.29"

    with tempfile.TemporaryDirectory() as temp_dir:
        path = Path(temp_dir) / "payload"
        path.write_bytes(b"topology\n")
        assert sha256_file(path) == sha256_bytes(b"topology\n")

    print("PASS build-topology self-test (8 contracts)")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_mutually_exclusive_group(required=True)
    actions.add_argument("--check", action="store_true", help="compare with the committed snapshot")
    actions.add_argument("--write-snapshot", action="store_true", help="write the deterministic snapshot")
    actions.add_argument("--print", dest="print_snapshot", action="store_true", help="print the current inventory")
    actions.add_argument("--self-test", action="store_true", help="run embedded parser/hash tests")
    parser.add_argument(
        "--workspace-root",
        type=Path,
        help="explicit root containing midi-studio/ and open-control/",
    )
    parser.add_argument(
        "--snapshot", type=Path, default=DEFAULT_SNAPSHOT, help="snapshot JSON path"
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    if args.self_test:
        run_self_test()
        return 0
    if args.workspace_root is None:
        raise InventoryError("--workspace-root is required outside --self-test")

    repositories = workspace_repositories(args.workspace_root)
    worktree_errors = validate_worktrees(
        repositories, writing_snapshot=args.write_snapshot
    )
    if worktree_errors:
        raise InventoryError("; ".join(worktree_errors))
    inventory = build_inventory(args.workspace_root)

    if args.print_snapshot:
        print(json.dumps(inventory, indent=2, sort_keys=True))
        return 0
    if args.write_snapshot:
        args.snapshot.parent.mkdir(parents=True, exist_ok=True)
        args.snapshot.write_text(
            json.dumps(inventory, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(f"WROTE {args.snapshot} ({len(json.dumps(inventory))} JSON bytes)")
        return 0

    require_file(args.snapshot, "topology snapshot")
    expected = json.loads(args.snapshot.read_text(encoding="utf-8"))
    differences = structural_diff(expected, inventory)
    if differences:
        print(f"FAIL build-topology snapshot ({len(differences)} differences)")
        for difference in differences[:40]:
            print(f"  {difference}")
        if len(differences) > 40:
            print(f"  ... {len(differences) - 40} more")
        return 1
    print(
        "PASS build-topology snapshot "
        f"(Core {inventory['sourceSets']['coreTotalImplementations']['count']} cpp, "
        f"{inventory['sourceSets']['coreTotalHeaders']['count']} headers)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except InventoryError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(2)
