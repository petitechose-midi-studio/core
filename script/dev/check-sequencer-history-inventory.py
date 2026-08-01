#!/usr/bin/env python3

import argparse
from collections import Counter
import copy
import json
from pathlib import Path
import re
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = Path(__file__).with_name("sequencer-history-inventory.json")
SOURCE_SUFFIXES = frozenset((".c", ".cc", ".cpp", ".h", ".hpp"))
BUILD_SUFFIXES = frozenset(
    (".cmake", ".ini", ".py", ".ps1", ".sh", ".toml", ".yaml", ".yml")
)
CANONICAL_RECORDING_METHODS = (
    "recordPattern",
    "recordFlatPattern",
    "recordFullBank",
    "recordStructure",
    "recordPreparedPattern",
    "recordPreparedSynchronizedPattern",
    "recordPreparedFullBank",
    "recordPreparedStructure",
)
ENTRY_RECORDING_CALL_TOTAL = 44
EXPECTED_MIGRATED_REMOVAL_TOTAL = 11
EXPECTED_PROVIDER_FORWARD_TOTAL = 5
EXPECTED_COALESCED_PREPARED_TOTAL = 1
EXPECTED_MEMBER_DISPATCH_TOTAL = 31
EXPECTED_CORE_STATE_ADAPTER_TOTAL = 1
EXPECTED_INTERNAL_FORWARD_TOTAL = 7
EXPECTED_RECORDING_CALL_TOTAL = 39
EXPECTED_SINK_TOTAL = 11
EXPECTED_SERVICE_ADAPTER_TOTAL = 20
EXPECTED_SINK_CLASSIFICATIONS = {
    "post-fallible": 4,
    "prepared": 5,
    "rollback-aware": 2,
}
PREPARED_PATTERN_LIFECYCLE_METHODS = (
    "beginPreparedPatternEdit",
    "sealPreparedPatternEdit",
    "commitPreparedPatternEdit",
)
PREPARED_PATTERN_OWNERS = (
    "PatternPitch",
    "PropertySelector",
    "StepContent",
    "StepEditSession",
    "StepToggle",
    "PatternEditor",
)
PREPARED_PATTERN_OWNER_PATH = "src/state/sequencer/SequencerHistory.hpp"
PREPARED_PATTERN_OWNER_ENUM = "SequencerPreparedPatternEditOwner"
PREPARED_PATTERN_SURFACES = (
    "src/handler/sequencer/PatternPitchSettingsHandler.cpp",
    "src/handler/sequencer/SequencerPropertySelectorHandler.cpp",
    "src/handler/sequencer/SequencerStepContentActions.cpp",
    "src/handler/sequencer/SequencerStepEditHandler.cpp",
    "src/handler/sequencer/SequencerStepEditSessionWorkflow.cpp",
    "src/handler/sequencer/SequencerStepHandler.cpp",
    "src/handler/sequencer/SequencerPatternEditorHandler.cpp",
)
PREPARED_PATTERN_CENTRAL_METHODS = (
    "beginOrContinueSequencerPreparedPatternEdit",
    "sealSequencerPreparedPatternEdit",
    "commitSequencerPreparedPatternEdit",
)
PREPARED_PATTERN_CENTRAL_PATH = "src/state/CoreStateSequencerHistoryRecording.cpp"
PREPARED_PATTERN_CENTRAL_QUALIFIER = "CoreState"
PREPARED_PATTERN_ADAPTER_PATH = (
    "src/handler/sequencer/SequencerHistoryDomainServices.cpp"
)
PREPARED_PATTERN_FORBIDDEN_RAW_METHODS = (
    "captureHistorySnapshot",
    "captureFlatHistorySnapshot",
    "recordPattern",
    "recordFlatPattern",
)
EXPECTED_PREPARED_SURFACE_CALL_TOTALS = {
    "beginPreparedPatternEdit": 8,
    "sealPreparedPatternEdit": 9,
    "commitPreparedPatternEdit": 9,
}
EXPECTED_PREPARED_CALL_TOTALS = {
    "beginPreparedPatternEdit": 9,
    "sealPreparedPatternEdit": 10,
    "commitPreparedPatternEdit": 10,
}


def sanitize_cpp(source: str) -> str:
    """Remove comments and literals while preserving offsets and newlines."""
    result = list(source)
    index = 0
    size = len(source)
    while index < size:
        if source.startswith("//", index):
            end = source.find("\n", index + 2)
            if end < 0:
                end = size
            for cursor in range(index, end):
                result[cursor] = " "
            index = end
            continue

        if source.startswith("/*", index):
            end = source.find("*/", index + 2)
            end = size if end < 0 else end + 2
            for cursor in range(index, end):
                if result[cursor] != "\n":
                    result[cursor] = " "
            index = end
            continue

        if (
            source[index] == "'"
            and index > 0
            and index + 1 < size
            and source[index - 1].isdigit()
            and source[index + 1].isalnum()
        ):
            result[index] = " "
            index += 1
            continue

        if source[index] in ('"', "'"):
            quote = source[index]
            cursor = index
            while cursor < size:
                if result[cursor] != "\n":
                    result[cursor] = " "
                if cursor > index and source[cursor] == quote:
                    backslashes = 0
                    previous = cursor - 1
                    while previous >= index and source[previous] == "\\":
                        backslashes += 1
                        previous -= 1
                    if backslashes % 2 == 0:
                        cursor += 1
                        break
                cursor += 1
            index = cursor
            continue

        index += 1
    return "".join(result)


def source_files(root: Path):
    source_root = root / "src"
    for path in sorted(source_root.rglob("*")):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
            yield path


def relative(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix()


def member_call_pattern(methods) -> re.Pattern[str]:
    alternatives = "|".join(re.escape(method) for method in sorted(methods))
    return re.compile(r"(?:\.|->)\s*(" + alternatives + r")\s*\(")


def count_member_dispatch(root: Path, methods) -> Counter:
    pattern = member_call_pattern(methods)
    observed = Counter()
    for path in source_files(root):
        text = sanitize_cpp(path.read_text(encoding="utf-8"))
        rel = relative(root, path)
        for match in pattern.finditer(text):
            observed[(rel, match.group(1))] += 1
    return observed


def count_member_method(path: Path, method: str) -> int:
    text = sanitize_cpp(path.read_text(encoding="utf-8"))
    return len(member_call_pattern((method,)).findall(text))


def unqualified_call_pattern(methods) -> re.Pattern[str]:
    alternatives = "|".join(re.escape(method) for method in sorted(methods))
    return re.compile(r"\b(" + alternatives + r")\s*\(")


def count_unqualified_calls(path: Path, methods) -> Counter:
    text = sanitize_cpp(path.read_text(encoding="utf-8"))
    pattern = unqualified_call_pattern(methods)
    observed = Counter()
    for match in pattern.finditer(text):
        prefix = text[max(0, match.start() - 32):match.start()]
        if re.search(r"(?:\.|->|::)\s*$", prefix):
            continue
        observed[match.group(1)] += 1
    return observed


def count_any_calls(path: Path, methods) -> Counter:
    text = sanitize_cpp(path.read_text(encoding="utf-8"))
    pattern = unqualified_call_pattern(methods)
    return Counter(match.group(1) for match in pattern.finditer(text))


def count_qualified_calls(path: Path, qualifier: str, methods) -> Counter:
    text = sanitize_cpp(path.read_text(encoding="utf-8"))
    alternatives = "|".join(re.escape(method) for method in sorted(methods))
    pattern = re.compile(
        r"\b" + re.escape(qualifier) + r"\s*::\s*(" + alternatives + r")\s*\("
    )
    return Counter(match.group(1) for match in pattern.finditer(text))


def enum_members(path: Path, enum_name: str) -> tuple[str, ...]:
    text = sanitize_cpp(path.read_text(encoding="utf-8"))
    declaration = re.compile(
        r"\benum\s+class\s+" + re.escape(enum_name) +
        r"\b[^\{;]*\{(?P<body>.*?)\}\s*;",
        re.DOTALL,
    )
    matches = list(declaration.finditer(text))
    if len(matches) != 1:
        return ()
    members = []
    for item in matches[0].group("body").split(","):
        name = item.split("=", 1)[0].strip()
        if name:
            members.append(name)
    return tuple(members)


def count_prepared_owner_references(
    path: Path,
    enum_name: str,
    owners,
) -> Counter:
    text = sanitize_cpp(path.read_text(encoding="utf-8"))
    aliases = set(
        re.findall(
            r"\busing\s+(\w+)\s*=\s*(?:\w+\s*::\s*)*" +
            re.escape(enum_name) + r"\s*;",
            text,
        )
    )
    qualifiers = {enum_name, *aliases}
    qualifier_alternatives = "|".join(
        re.escape(item) for item in sorted(qualifiers)
    )
    owner_alternatives = "|".join(re.escape(owner) for owner in sorted(owners))
    pattern = re.compile(
        r"\b(?:" + qualifier_alternatives + r")\s*::\s*(" +
        owner_alternatives + r")\b"
    )
    return Counter(match.group(1) for match in pattern.finditer(text))


def expected_member_counter(manifest) -> Counter:
    groups = manifest["recordingBoundary"]["memberDispatch"]["groups"]
    return Counter(
        {
            (group["path"], group["method"]): group["count"]
            for group in groups
        }
    )


def expected_forward_counter(manifest) -> Counter:
    forwards = manifest["recordingBoundary"]["internalForwards"]
    return Counter(
        {group["method"]: group["count"] for group in forwards["groups"]}
    )


def expected_begin_counter(manifest) -> Counter:
    begins = manifest["coalescedBegins"]
    return Counter({group["path"]: group["count"] for group in begins["groups"]})


def expected_business_counter(section) -> Counter:
    return Counter(
        {
            group["path"]: group["count"]
            for group in section["groups"]
            if group.get("role") == "business"
        }
    )


def expected_call_counter(section) -> Counter:
    return Counter({group["path"]: group["count"] for group in section["groups"]})


def expected_prepared_lifecycle_counter(manifest) -> Counter:
    lifecycle = manifest["preparedPatternLifecycle"]
    expected = Counter()
    adapter = lifecycle["serviceAdapter"]
    for method, count in adapter["calls"].items():
        expected[(adapter["path"], method)] = count
    for surface in lifecycle["surfaces"]:
        for method, count in surface["calls"].items():
            expected[(surface["path"], method)] = count
    return expected


def expected_prepared_owner_reference_counter(manifest) -> Counter:
    expected = Counter()
    for surface in manifest["preparedPatternLifecycle"]["surfaces"]:
        for owner, count in surface["ownerReferences"].items():
            expected[(surface["path"], owner)] = count
    return expected


def expected_prepared_central_counter(manifest) -> Counter:
    central = manifest["preparedPatternLifecycle"]["centralAuthority"]
    return Counter(central["definitions"])


def walk_manifest(value, path="root"):
    if isinstance(value, dict):
        for key, child in value.items():
            yield path, key, child
            yield from walk_manifest(child, f"{path}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from walk_manifest(child, f"{path}[{index}]")


def manifest_errors(manifest) -> list[str]:
    errors = []
    if manifest.get("schemaVersion") != 1:
        errors.append("manifest schemaVersion must be 1")

    forbidden_anchor_keys = {"line", "lineno", "linenumber", "lineNumber"}
    for path, key, _ in walk_manifest(manifest):
        if key in forbidden_anchor_keys or key.casefold() in {
            item.casefold() for item in forbidden_anchor_keys
        }:
            errors.append(f"{path}: line-number anchor '{key}' is forbidden")

    try:
        boundary = manifest["recordingBoundary"]
        members = boundary["memberDispatch"]
        groups = members["groups"]
    except (KeyError, TypeError):
        return errors + ["manifest is missing the recordingBoundary member inventory"]

    methods = members.get("methods", [])
    if tuple(methods) != CANONICAL_RECORDING_METHODS:
        errors.append("member-dispatch methods must match the canonical eight-method boundary")
    if members.get("expectedMethodCount") != len(CANONICAL_RECORDING_METHODS):
        errors.append("member-dispatch expectedMethodCount must remain 8")

    seen = set()
    sink_total = 0
    service_total = 0
    provider_forward_total = 0
    coalesced_prepared_total = 0
    classifications = Counter()
    for group in groups:
        key = (group.get("path"), group.get("method"))
        if key in seen:
            errors.append(f"duplicate member-dispatch anchor: {key[0]}:{key[1]}")
        seen.add(key)
        if group.get("method") not in methods:
            errors.append(f"unknown recording method in {key[0]}: {key[1]}")
        count = group.get("count")
        if not isinstance(count, int) or count <= 0:
            errors.append(f"{key[0]}:{key[1]} must have a positive integer count")
            continue
        provider_count = group.get("providerForwardCount", 0)
        if (type(provider_count) is not int or
                provider_count < 0 or provider_count > count):
            errors.append(
                f"{key[0]}:{key[1]} has invalid providerForwardCount"
            )
            provider_count = 0
        provider_forward_total += provider_count
        coalesced_count = group.get("coalescedPreparedCount", 0)
        if (type(coalesced_count) is not int or
                coalesced_count < 0 or coalesced_count > count):
            errors.append(
                f"{key[0]}:{key[1]} has invalid coalescedPreparedCount"
            )
            coalesced_count = 0
        coalesced_prepared_total += coalesced_count
        role = group.get("role")
        if role == "sink":
            classification = group.get("classification")
            if classification not in {"post-fallible", "prepared", "rollback-aware"}:
                errors.append(f"{key[0]}:{key[1]} has invalid sink classification")
            sink_total += count
            classifications[classification] += count
        elif role == "service-adapter":
            if "classification" in group:
                errors.append(f"{key[0]}:{key[1]} service adapter must not be classified as a sink")
            service_total += count
        else:
            errors.append(f"{key[0]}:{key[1]} has invalid role {role!r}")
        if provider_count and role != "service-adapter":
            errors.append(
                f"{key[0]}:{key[1]} provider forwards must be service adapters"
            )
        if coalesced_count and role != "service-adapter":
            errors.append(
                f"{key[0]}:{key[1]} coalesced prepared calls must be service adapters"
            )

    member_total = sum(group.get("count", 0) for group in groups)
    if member_total != members.get("expectedTotal"):
        errors.append(
            f"member manifest total is {member_total}, expected {members.get('expectedTotal')}"
        )
    if members.get("expectedTotal") != EXPECTED_MEMBER_DISPATCH_TOTAL:
        errors.append("member-dispatch total must remain 31 after L-R08-04")
    if sink_total != members.get("expectedSinkTotal"):
        errors.append(
            f"sink manifest total is {sink_total}, expected {members.get('expectedSinkTotal')}"
        )
    if members.get("expectedSinkTotal") != EXPECTED_SINK_TOTAL:
        errors.append("mutation-sink total must remain 11 after L-R08-04")
    if service_total != members.get("expectedServiceAdapterTotal"):
        errors.append(
            "service/adapter manifest total is "
            f"{service_total}, expected {members.get('expectedServiceAdapterTotal')}"
        )
    if members.get("expectedServiceAdapterTotal") != EXPECTED_SERVICE_ADAPTER_TOTAL:
        errors.append("service/adapter total must remain 20 after L-R08-04")
    expected_classifications = Counter(members.get("expectedSinkClassifications", {}))
    if classifications != expected_classifications:
        errors.append(
            "sink classification totals differ: "
            f"expected {dict(expected_classifications)}, observed {dict(classifications)}"
        )
    if dict(expected_classifications) != EXPECTED_SINK_CLASSIFICATIONS:
        errors.append("sink classifications must remain 4 post-fallible, 5 prepared and 2 rollback-aware")

    forwards = boundary.get("internalForwards", {})
    forward_groups = forwards.get("groups", [])
    forward_total = sum(group.get("count", 0) for group in forward_groups)
    if forward_total != forwards.get("expectedTotal"):
        errors.append(
            f"internal-forward manifest total is {forward_total}, "
            f"expected {forwards.get('expectedTotal')}"
        )
    if forwards.get("expectedTotal") != EXPECTED_INTERNAL_FORWARD_TOTAL:
        errors.append("internal-forward total must remain 7")
    forward_methods = set()
    for group in forward_groups:
        method = group.get("method")
        count = group.get("count")
        if method in forward_methods:
            errors.append(f"duplicate internal-forward method: {method}")
        forward_methods.add(method)
        if method not in CANONICAL_RECORDING_METHODS:
            errors.append("internal forwards must use the canonical eight-method boundary")
        if not isinstance(count, int) or count <= 0:
            errors.append(f"internal forward {method} must have a positive integer count")
            continue
        provider_count = group.get("providerForwardCount", 0)
        if (type(provider_count) is not int or
                provider_count < 0 or provider_count > count):
            errors.append(f"internal forward {method} has invalid providerForwardCount")
        else:
            provider_forward_total += provider_count

    if boundary.get("entryRecordingCallTotal") != ENTRY_RECORDING_CALL_TOTAL:
        errors.append("entry recording-call total must remain 44")
    if boundary.get("expectedMigratedRemovalTotal") != EXPECTED_MIGRATED_REMOVAL_TOTAL:
        errors.append("migrated recording-call removal total must remain 11")
    if boundary.get("expectedProviderForwardTotal") != EXPECTED_PROVIDER_FORWARD_TOTAL:
        errors.append("expected provider-forward total must remain 5")
    if provider_forward_total != EXPECTED_PROVIDER_FORWARD_TOTAL:
        errors.append(
            f"provider-forward manifest total is {provider_forward_total}, "
            f"expected {EXPECTED_PROVIDER_FORWARD_TOTAL}"
        )
    if boundary.get("expectedCoalescedPreparedTotal") != EXPECTED_COALESCED_PREPARED_TOTAL:
        errors.append("expected coalesced-prepared total must remain 1")
    if coalesced_prepared_total != EXPECTED_COALESCED_PREPARED_TOTAL:
        errors.append(
            f"coalesced-prepared manifest total is {coalesced_prepared_total}, "
            f"expected {EXPECTED_COALESCED_PREPARED_TOTAL}"
        )

    adapter = boundary.get("coreStateAdapter", {})
    if adapter.get("expectedCount") != EXPECTED_CORE_STATE_ADAPTER_TOTAL:
        errors.append("CoreState adapter total must remain 1")
    recording_total = (
        members.get("expectedTotal", 0)
        + forwards.get("expectedTotal", 0)
        + adapter.get("expectedCount", 0)
    )
    if recording_total != boundary.get("expectedTotal"):
        errors.append(
            f"recording boundary total is {recording_total}, "
            f"expected {boundary.get('expectedTotal')}"
        )
    if boundary.get("expectedTotal") != EXPECTED_RECORDING_CALL_TOTAL:
        errors.append("recording boundary total must remain 39 after L-R08-04")
    if recording_total != (
        ENTRY_RECORDING_CALL_TOTAL -
        EXPECTED_MIGRATED_REMOVAL_TOTAL +
        provider_forward_total +
        coalesced_prepared_total
    ):
        errors.append(
            "recording boundary must equal the 44-call entry minus eleven "
            "migrated calls plus five provider forwards and one prepared "
            "coalesced boundary"
        )

    exclusions = boundary.get("explicitExclusions", [])
    if exclusions:
        errors.append("the Sequencer history boundary must leave no recording-call exclusion")

    begins = manifest.get("coalescedBegins", {})
    begin_groups = begins.get("groups", [])
    begin_total = sum(group.get("count", 0) for group in begin_groups)
    business_total = sum(
        group.get("count", 0) for group in begin_groups if group.get("role") == "business"
    )
    adapter_total = sum(
        group.get("count", 0) for group in begin_groups if group.get("role") == "adapter"
    )
    if begin_total != begins.get("expectedTotal"):
        errors.append(f"coalesced-begin total is {begin_total}, expected {begins.get('expectedTotal')}")
    if business_total != begins.get("expectedBusinessTotal"):
        errors.append(
            f"business begin total is {business_total}, expected {begins.get('expectedBusinessTotal')}"
        )
    if adapter_total != begins.get("expectedAdapterTotal"):
        errors.append(
            f"adapter begin total is {adapter_total}, expected {begins.get('expectedAdapterTotal')}"
        )

    seals = manifest.get("coalescedSeals", {})
    seal_groups = seals.get("groups", [])
    seal_total = sum(group.get("count", 0) for group in seal_groups)
    seal_business_total = sum(
        group.get("count", 0) for group in seal_groups if group.get("role") == "business"
    )
    seal_adapter_total = sum(
        group.get("count", 0) for group in seal_groups if group.get("role") == "adapter"
    )
    if seal_total != seals.get("expectedTotal"):
        errors.append(f"coalesced-seal total is {seal_total}, expected {seals.get('expectedTotal')}")
    if seal_business_total != seals.get("expectedBusinessTotal"):
        errors.append(
            f"business seal total is {seal_business_total}, "
            f"expected {seals.get('expectedBusinessTotal')}"
        )
    if seal_adapter_total != seals.get("expectedAdapterTotal"):
        errors.append(
            f"adapter seal total is {seal_adapter_total}, "
            f"expected {seals.get('expectedAdapterTotal')}"
        )

    lifecycle = manifest.get("preparedPatternLifecycle")
    if not isinstance(lifecycle, dict):
        errors.append("manifest is missing the prepared Pattern lifecycle ratchet")
    else:
        declaration = lifecycle.get("ownerDeclaration", {})
        if declaration.get("path") != PREPARED_PATTERN_OWNER_PATH:
            errors.append("prepared owner declaration path must remain canonical")
        if declaration.get("enum") != PREPARED_PATTERN_OWNER_ENUM:
            errors.append("prepared owner enum name must remain canonical")
        declared_owners = tuple(declaration.get("owners", []))
        if declared_owners != PREPARED_PATTERN_OWNERS:
            errors.append("prepared Pattern owners must enumerate the canonical six")
        if declaration.get("expectedCount") != len(PREPARED_PATTERN_OWNERS):
            errors.append("prepared Pattern owner count must remain 6")

        methods = tuple(lifecycle.get("methods", []))
        if methods != PREPARED_PATTERN_LIFECYCLE_METHODS:
            errors.append("prepared Pattern lifecycle methods must remain begin/seal/commit")

        central = lifecycle.get("centralAuthority", {})
        if central.get("path") != PREPARED_PATTERN_CENTRAL_PATH:
            errors.append("prepared Pattern lifecycle must remain CoreState-owned")
        if central.get("qualifier") != PREPARED_PATTERN_CENTRAL_QUALIFIER:
            errors.append("prepared Pattern central qualifier must remain CoreState")
        definitions = central.get("definitions", {})
        if tuple(definitions) != PREPARED_PATTERN_CENTRAL_METHODS:
            errors.append("prepared Pattern central definitions must remain exact")
        elif any(type(count) is not int or count != 1 for count in definitions.values()):
            errors.append("each prepared Pattern central definition must occur exactly once")

        adapter = lifecycle.get("serviceAdapter", {})
        if adapter.get("path") != PREPARED_PATTERN_ADAPTER_PATH:
            errors.append("prepared Pattern lifecycle service adapter path must remain canonical")
        adapter_calls = adapter.get("calls", {})
        if tuple(adapter_calls) != PREPARED_PATTERN_LIFECYCLE_METHODS:
            errors.append("prepared Pattern service adapter must expose begin/seal/commit")
        elif any(type(count) is not int or count != 1 for count in adapter_calls.values()):
            errors.append("prepared Pattern service adapter calls must each remain exact at one")

        surfaces = lifecycle.get("surfaces", [])
        surface_paths = tuple(
            surface.get("path") for surface in surfaces if isinstance(surface, dict)
        )
        if lifecycle.get("expectedSurfaceCount") != len(PREPARED_PATTERN_SURFACES):
            errors.append("prepared Pattern migrated surface count must remain 7")
        if surface_paths != PREPARED_PATTERN_SURFACES:
            errors.append("prepared Pattern lifecycle must enumerate the canonical seven surfaces")

        surface_call_totals = Counter()
        manifest_owner_set = set()
        for surface in surfaces:
            if not isinstance(surface, dict):
                errors.append("prepared Pattern surface entries must be objects")
                continue
            path = surface.get("path")
            calls = surface.get("calls", {})
            if tuple(calls) != PREPARED_PATTERN_LIFECYCLE_METHODS:
                errors.append(f"{path}: prepared lifecycle calls must enumerate begin/seal/commit")
            for method, count in calls.items():
                if type(count) is not int or count < 0:
                    errors.append(f"{path}:{method} must have a non-negative integer count")
                else:
                    surface_call_totals[method] += count

            owner_references = surface.get("ownerReferences", {})
            if not owner_references:
                errors.append(f"{path}: prepared lifecycle surface must name an owner")
            for owner, count in owner_references.items():
                manifest_owner_set.add(owner)
                if owner not in PREPARED_PATTERN_OWNERS:
                    errors.append(f"{path}: unknown prepared Pattern owner {owner}")
                if type(count) is not int or count <= 0:
                    errors.append(f"{path}:{owner} owner reference count must be positive")

        expected_surface_totals = lifecycle.get("expectedSurfaceCallTotals", {})
        if dict(surface_call_totals) != expected_surface_totals:
            errors.append(
                "prepared Pattern surface call totals differ: "
                f"expected {expected_surface_totals}, observed {dict(surface_call_totals)}"
            )
        if expected_surface_totals != EXPECTED_PREPARED_SURFACE_CALL_TOTALS:
            errors.append("prepared Pattern surface lifecycle totals must remain 8/9/9")
        if manifest_owner_set != set(PREPARED_PATTERN_OWNERS):
            errors.append("prepared Pattern surfaces must cover all six owners")

        expected_call_totals = lifecycle.get("expectedCallTotals", {})
        combined_call_totals = Counter(surface_call_totals)
        for method, count in adapter_calls.items():
            if type(count) is int:
                combined_call_totals[method] += count
        if dict(combined_call_totals) != expected_call_totals:
            errors.append(
                "prepared Pattern global call totals differ: "
                f"expected {expected_call_totals}, observed {dict(combined_call_totals)}"
            )
        if expected_call_totals != EXPECTED_PREPARED_CALL_TOTALS:
            errors.append("prepared Pattern global lifecycle totals must remain 9/10/10")

        forbidden_methods = tuple(lifecycle.get("forbiddenRawMethods", []))
        if forbidden_methods != PREPARED_PATTERN_FORBIDDEN_RAW_METHODS:
            errors.append("prepared Pattern raw capture/record denylist must remain exact")
        if lifecycle.get("expectedForbiddenRawCallTotal") != 0:
            errors.append("migrated surfaces must retain zero raw capture/record calls")

    seam = manifest.get("failureInjection", {})
    helpers = seam.get("guardedHelpers", [])
    if len(helpers) != 4 or len(set(helpers)) != 4:
        errors.append("failure injection must name four unique guarded helpers")
    if seam.get("expectedAllocatorGuardCount") != len(helpers) + 1:
        errors.append("allocator guard count must be one seam guard plus four helper guards")
    return errors


def collect_observation(root: Path, manifest):
    boundary = manifest["recordingBoundary"]
    members = boundary["memberDispatch"]
    methods = members["methods"]

    forwards = boundary["internalForwards"]
    forward_path = root / forwards["path"]

    adapter = boundary["coreStateAdapter"]
    adapter_path = root / adapter["path"]

    exclusions = Counter()
    for exclusion in boundary["explicitExclusions"]:
        path = root / exclusion["path"]
        count = count_unqualified_calls(path, (exclusion["method"],))[exclusion["method"]]
        exclusions[(exclusion["path"], exclusion["method"])] = count

    begin_method = manifest["coalescedBegins"]["method"]
    begins = Counter()
    authoritative_begins = Counter()
    begin_pattern = member_call_pattern((begin_method,))
    guarded_begin_pattern = re.compile(
        r"\bif\s*\(\s*!\s*history_\s*\.\s*" +
        re.escape(begin_method) + r"\s*\("
    )
    seal_method = manifest["coalescedSeals"]["method"]
    seals = Counter()
    authoritative_seals = Counter()
    seal_pattern = member_call_pattern((seal_method,))
    guarded_seal_pattern = re.compile(
        r"\bif\s*\(\s*!\s*history_\s*\.\s*" +
        re.escape(seal_method) + r"\s*\("
    )
    for path in source_files(root):
        text = sanitize_cpp(path.read_text(encoding="utf-8"))
        rel = relative(root, path)
        count = len(begin_pattern.findall(text))
        if count:
            begins[rel] = count
        authoritative_count = len(guarded_begin_pattern.findall(text))
        if authoritative_count:
            authoritative_begins[rel] = authoritative_count
        count = len(seal_pattern.findall(text))
        if count:
            seals[rel] = count
        authoritative_count = len(guarded_seal_pattern.findall(text))
        if authoritative_count:
            authoritative_seals[rel] = authoritative_count

    lifecycle = manifest["preparedPatternLifecycle"]
    owner_declaration = lifecycle["ownerDeclaration"]
    central = lifecycle["centralAuthority"]
    prepared_owner_references = Counter()
    prepared_forbidden_raw_calls = Counter()
    for surface in lifecycle["surfaces"]:
        surface_path = root / surface["path"]
        owner_counts = count_prepared_owner_references(
            surface_path,
            owner_declaration["enum"],
            owner_declaration["owners"],
        )
        for owner, count in owner_counts.items():
            prepared_owner_references[(surface["path"], owner)] = count
        forbidden_counts = count_any_calls(
            surface_path,
            lifecycle["forbiddenRawMethods"],
        )
        for method, count in forbidden_counts.items():
            prepared_forbidden_raw_calls[(surface["path"], method)] = count

    return {
        "members": count_member_dispatch(root, methods),
        "forwards": count_unqualified_calls(forward_path, methods),
        "adapter": Counter(
            {
                (adapter["path"], adapter["method"]): count_member_method(
                    adapter_path,
                    adapter["method"],
                )
            }
        ),
        "exclusions": exclusions,
        "begins": begins,
        "authoritativeBegins": authoritative_begins,
        "seals": seals,
        "authoritativeSeals": authoritative_seals,
        "preparedOwners": enum_members(
            root / owner_declaration["path"],
            owner_declaration["enum"],
        ),
        "preparedLifecycleCalls": count_member_dispatch(
            root,
            lifecycle["methods"],
        ),
        "preparedCentralDefinitions": count_qualified_calls(
            root / central["path"],
            central["qualifier"],
            central["definitions"],
        ),
        "preparedOwnerReferences": prepared_owner_references,
        "preparedForbiddenRawCalls": prepared_forbidden_raw_calls,
    }


def counter_errors(label: str, expected: Counter, observed: Counter) -> list[str]:
    errors = []
    for key in sorted(set(expected) | set(observed), key=str):
        expected_count = expected.get(key, 0)
        observed_count = observed.get(key, 0)
        if expected_count != observed_count:
            errors.append(
                f"{label} mismatch for {key}: expected {expected_count}, observed {observed_count}"
            )
    return errors


def observation_errors(manifest, observed) -> list[str]:
    boundary = manifest["recordingBoundary"]
    adapter = boundary["coreStateAdapter"]
    expected_adapter = Counter(
        {(adapter["path"], adapter["method"]): adapter["expectedCount"]}
    )
    expected_exclusions = Counter(
        {
            (item["path"], item["method"]): item["expectedUnqualifiedCallCount"]
            for item in boundary["explicitExclusions"]
        }
    )

    errors = []
    errors += counter_errors(
        "member dispatch",
        expected_member_counter(manifest),
        observed["members"],
    )
    errors += counter_errors(
        "internal forward",
        expected_forward_counter(manifest),
        observed["forwards"],
    )
    errors += counter_errors("CoreState adapter", expected_adapter, observed["adapter"])
    errors += counter_errors(
        "explicit exclusion",
        expected_exclusions,
        observed["exclusions"],
    )
    errors += counter_errors(
        "coalesced begin",
        expected_begin_counter(manifest),
        observed["begins"],
    )
    errors += counter_errors(
        "authoritative business begin",
        expected_business_counter(manifest["coalescedBegins"]),
        observed["authoritativeBegins"],
    )
    errors += counter_errors(
        "coalesced seal",
        expected_call_counter(manifest["coalescedSeals"]),
        observed["seals"],
    )
    errors += counter_errors(
        "authoritative business seal",
        expected_business_counter(manifest["coalescedSeals"]),
        observed["authoritativeSeals"],
    )
    lifecycle = manifest["preparedPatternLifecycle"]
    expected_owners = tuple(lifecycle["ownerDeclaration"]["owners"])
    if observed["preparedOwners"] != expected_owners:
        errors.append(
            "prepared Pattern owner declaration mismatch: "
            f"expected {expected_owners}, observed {observed['preparedOwners']}"
        )
    errors += counter_errors(
        "prepared lifecycle call",
        expected_prepared_lifecycle_counter(manifest),
        observed["preparedLifecycleCalls"],
    )
    errors += counter_errors(
        "prepared central definition",
        expected_prepared_central_counter(manifest),
        observed["preparedCentralDefinitions"],
    )
    errors += counter_errors(
        "prepared owner reference",
        expected_prepared_owner_reference_counter(manifest),
        observed["preparedOwnerReferences"],
    )
    errors += counter_errors(
        "forbidden prepared-surface raw call",
        Counter(),
        observed["preparedForbiddenRawCalls"],
    )

    member_total = sum(observed["members"].values())
    forward_total = sum(observed["forwards"].values())
    adapter_total = sum(observed["adapter"].values())
    recording_total = member_total + forward_total + adapter_total
    if member_total != boundary["memberDispatch"]["expectedTotal"]:
        errors.append(
            f"member-dispatch total is {member_total}, "
            f"expected {boundary['memberDispatch']['expectedTotal']}"
        )
    if forward_total != boundary["internalForwards"]["expectedTotal"]:
        errors.append(
            f"internal-forward total is {forward_total}, "
            f"expected {boundary['internalForwards']['expectedTotal']}"
        )
    if recording_total != boundary["expectedTotal"]:
        errors.append(
            f"recording-call total is {recording_total}, expected {boundary['expectedTotal']}"
        )
    return errors


def find_matching(text: str, start: int, opening: str, closing: str) -> int | None:
    depth = 0
    for index in range(start, len(text)):
        if text[index] == opening:
            depth += 1
        elif text[index] == closing:
            depth -= 1
            if depth == 0:
                return index
    return None


def helper_body(text: str, helper: str) -> str | None:
    for match in re.finditer(r"\b" + re.escape(helper) + r"\s*\(", text):
        open_paren = text.find("(", match.start())
        close_paren = find_matching(text, open_paren, "(", ")")
        if close_paren is None:
            continue
        cursor = close_paren + 1
        while cursor < len(text) and text[cursor].isspace():
            cursor += 1
        if cursor >= len(text) or text[cursor] != "{":
            continue
        close_brace = find_matching(text, cursor, "{", "}")
        if close_brace is not None:
            return text[cursor:close_brace + 1]
    return None


def build_configuration_files(root: Path):
    ignored_roots = {".cache", ".git", ".pio", ".venv", "build", "venv"}
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        rel_parts = path.relative_to(root).parts
        if any(part in ignored_roots for part in rel_parts):
            continue
        if path.name == "CMakeLists.txt" or path.suffix.lower() in BUILD_SUFFIXES:
            yield path


def seam_errors(root: Path, manifest) -> list[str]:
    seam = manifest["failureInjection"]
    macro = seam["macro"]
    errors = []

    build_uses = Counter()
    for path in build_configuration_files(root):
        count = path.read_text(encoding="utf-8", errors="replace").count(macro)
        if count:
            build_uses[relative(root, path)] = count
    expected_build_uses = Counter({seam["cmakePath"]: 1})
    errors += counter_errors("build macro use", expected_build_uses, build_uses)

    cmake = (root / seam["cmakePath"]).read_text(encoding="utf-8")
    guard_expression = r"if\s*\(\s*" + r"\s+AND\s+".join(
        re.escape(guard) for guard in seam["requiredCmakeGuards"]
    ) + r"\s*\)"
    guarded_block = re.search(guard_expression + r"(?P<body>.*?)endif\s*\(\s*\)", cmake, re.DOTALL)
    if guarded_block is None or macro not in guarded_block.group("body"):
        errors.append("failure-injection compile definition is not inside the test-only CMake guard")

    target_definition = re.compile(
        r"target_compile_definitions\s*\(\s*"
        + re.escape(seam["nativeTarget"])
        + r"\s+PUBLIC\s+"
        + re.escape(macro)
        + r"\s*=\s*1\s*\)",
        re.DOTALL,
    )
    if len(target_definition.findall(cmake)) != 1:
        errors.append("expected one test-native failure-injection compile definition")

    allocator_path = root / seam["allocatorPath"]
    allocator = sanitize_cpp(allocator_path.read_text(encoding="utf-8"))
    guard_pattern = re.compile(
        r"#\s*if\s+defined\s*\(\s*" + re.escape(macro) + r"\s*\)"
    )
    guard_count = len(guard_pattern.findall(allocator))
    if guard_count != seam["expectedAllocatorGuardCount"]:
        errors.append(
            f"allocator macro guard count is {guard_count}, "
            f"expected {seam['expectedAllocatorGuardCount']}"
        )
    namespace_count = len(re.findall(r"\bnamespace\s+testing\s*\{", allocator))
    if namespace_count != seam["expectedSeamNamespaceCount"]:
        errors.append(
            f"failure-injection seam namespace count is {namespace_count}, "
            f"expected {seam['expectedSeamNamespaceCount']}"
        )
    guarded_blocks = re.findall(
        guard_pattern.pattern + r"(?P<body>.*?)#\s*endif\b",
        allocator,
        flags=re.DOTALL,
    )
    guarded_namespace_count = sum(
        1 for body in guarded_blocks if re.search(r"\bnamespace\s+testing\s*\{", body)
    )
    if guarded_namespace_count != seam["expectedSeamNamespaceCount"]:
        errors.append("the testing seam namespace must be inside its sole release guard")

    for helper in seam["guardedHelpers"]:
        body = helper_body(allocator, helper)
        if body is None:
            errors.append(f"guarded allocator helper not found: {helper}")
            continue
        if len(guard_pattern.findall(body)) != 1:
            errors.append(f"{helper} must contain exactly one failure-injection guard")
        if len(re.findall(r"\btesting::consumeExtmemAllocationFailure\s*\(\s*\)", body)) != 1:
            errors.append(f"{helper} must consume the failure seam exactly once")

    source_uses = Counter()
    for path in source_files(root):
        count = path.read_text(encoding="utf-8", errors="replace").count(macro)
        if count:
            source_uses[relative(root, path)] = count
    expected_source_uses = Counter(
        {seam["allocatorPath"]: seam["expectedAllocatorGuardCount"]}
    )
    errors += counter_errors("product-source macro use", expected_source_uses, source_uses)

    test_uses = Counter()
    test_root = root / "test"
    for path in sorted(test_root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        count = path.read_text(encoding="utf-8", errors="replace").count(macro)
        if count:
            test_uses[relative(root, path)] = count
    errors += counter_errors(
        "test macro use",
        Counter(seam["allowedTestUses"]),
        test_uses,
    )

    define_pattern = re.compile(r"#\s*define\s+" + re.escape(macro) + r"\b")
    define_count = 0
    for path in list(source_files(root)) + [
        path
        for path in test_root.rglob("*")
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
    ]:
        define_count += len(define_pattern.findall(path.read_text(encoding="utf-8", errors="replace")))
    if define_count != 0:
        errors.append(f"{macro} must not be defined directly in product or test source")
    return errors


def synthetic_observation(manifest):
    boundary = manifest["recordingBoundary"]
    adapter = boundary["coreStateAdapter"]
    lifecycle = manifest["preparedPatternLifecycle"]
    return {
        "members": expected_member_counter(manifest),
        "forwards": expected_forward_counter(manifest),
        "adapter": Counter(
            {(adapter["path"], adapter["method"]): adapter["expectedCount"]}
        ),
        "exclusions": Counter(
            {
                (item["path"], item["method"]): item["expectedUnqualifiedCallCount"]
                for item in boundary["explicitExclusions"]
            }
        ),
        "begins": expected_begin_counter(manifest),
        "authoritativeBegins": expected_business_counter(
            manifest["coalescedBegins"]
        ),
        "seals": expected_call_counter(manifest["coalescedSeals"]),
        "authoritativeSeals": expected_business_counter(
            manifest["coalescedSeals"]
        ),
        "preparedOwners": tuple(lifecycle["ownerDeclaration"]["owners"]),
        "preparedLifecycleCalls": expected_prepared_lifecycle_counter(manifest),
        "preparedCentralDefinitions": expected_prepared_central_counter(manifest),
        "preparedOwnerReferences": expected_prepared_owner_reference_counter(manifest),
        "preparedForbiddenRawCalls": Counter(),
    }

def scanner_self_test(manifest) -> bool:
    methods = manifest["recordingBoundary"]["memberDispatch"]["methods"]
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        member_path = root / "src/handler/sequencer/Scanner.cpp"
        member_path.parent.mkdir(parents=True)
        member_path.write_text(
            "void scan() {\n"
            "  history.recordPattern();\n"
            "  pointer->recordFlatPattern();\n"
            "  // history.recordFullBank();\n"
            "  const char* literal = \"history.recordStructure(\";\n"
            "}\n",
            encoding="utf-8",
        )
        internal_path = root / "src/state/sequencer/Internal.cpp"
        internal_path.parent.mkdir(parents=True)
        internal_path.write_text(
            "void scan() {\n"
            "  recordPattern();\n"
            "  recordPreparedStructure();\n"
            "  Type::recordFlatPattern();\n"
            "  // recordFullBank();\n"
            "}\n",
            encoding="utf-8",
        )
        members = count_member_dispatch(root, methods)
        forwards = count_unqualified_calls(internal_path, methods)
        expected_members = Counter({
            ("src/handler/sequencer/Scanner.cpp", "recordPattern"): 1,
            ("src/handler/sequencer/Scanner.cpp", "recordFlatPattern"): 1,
        })
        expected_forwards = Counter(
            {"recordPattern": 1, "recordPreparedStructure": 1}
        )
        return members == expected_members and forwards == expected_forwards

def non_member_topology_self_test(manifest) -> bool:
    drift = synthetic_observation(manifest)
    adapter_key = next(iter(drift["adapter"]))
    drift["forwards"]["recordPattern"] += 1
    drift["adapter"][adapter_key] += 1
    drift["begins"]["src/handler/sequencer/UnexpectedBegin.cpp"] += 1
    drift["authoritativeBegins"][
        "src/handler/sequencer/SequencerMacroPropertyHandler.cpp"
    ] -= 1
    drift["seals"]["src/handler/sequencer/UnexpectedSeal.cpp"] += 1
    drift["authoritativeSeals"][
        "src/handler/sequencer/SequencerPatternQuickControlsHandler.cpp"
    ] -= 1
    errors = observation_errors(manifest, drift)
    labels = (
        "internal forward mismatch", "CoreState adapter mismatch",
        "coalesced begin mismatch", "authoritative business begin mismatch",
        "coalesced seal mismatch", "authoritative business seal mismatch",
    )
    return all(any(label in error for error in errors) for label in labels)


def prepared_lifecycle_self_test(manifest) -> bool:
    lifecycle = manifest["preparedPatternLifecycle"]
    declaration = lifecycle["ownerDeclaration"]
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        owner_path = root / "Owner.hpp"
        owner_path.write_text(
            "enum class SequencerPreparedPatternEditOwner : unsigned char {\n"
            "  PatternPitch = 0, PropertySelector, StepContent,\n"
            "  StepEditSession, StepToggle, PatternEditor,\n"
            "};\n",
            encoding="utf-8",
        )
        surface_path = root / "Surface.cpp"
        surface_path.write_text(
            "using PreparedOwner = ns::SequencerPreparedPatternEditOwner;\n"
            "void scan() {\n"
            "  history.beginPreparedPatternEdit(PreparedOwner::PatternPitch);\n"
            "  history.sealPreparedPatternEdit(\n"
            "      PreparedOwner::PatternPitch);\n"
            "  history.commitPreparedPatternEdit(PreparedOwner::PatternPitch);\n"
            "  // history.recordPattern();\n"
            "  const char* raw = \"captureHistorySnapshot(\";\n"
            "}\n",
            encoding="utf-8",
        )
        central_path = root / "Central.cpp"
        central_path.write_text(
            "void CoreState::beginOrContinueSequencerPreparedPatternEdit() {}\n"
            "void CoreState::sealSequencerPreparedPatternEdit() {}\n"
            "void CoreState::commitSequencerPreparedPatternEdit() {}\n",
            encoding="utf-8",
        )
        if enum_members(owner_path, declaration["enum"]) != PREPARED_PATTERN_OWNERS:
            return False
        if count_member_method(surface_path, "beginPreparedPatternEdit") != 1:
            return False
        if count_prepared_owner_references(
            surface_path,
            declaration["enum"],
            declaration["owners"],
        ) != Counter({"PatternPitch": 3}):
            return False
        if count_any_calls(surface_path, lifecycle["forbiddenRawMethods"]):
            return False
        if count_qualified_calls(
            central_path,
            lifecycle["centralAuthority"]["qualifier"],
            lifecycle["centralAuthority"]["definitions"],
        ) != Counter({method: 1 for method in PREPARED_PATTERN_CENTRAL_METHODS}):
            return False

        surface_path.write_text(
            surface_path.read_text(encoding="utf-8") +
            "void regression() { history.recordPattern(); }\n",
            encoding="utf-8",
        )
        if count_any_calls(
            surface_path,
            lifecycle["forbiddenRawMethods"],
        ) != Counter({"recordPattern": 1}):
            return False

    drift = synthetic_observation(manifest)
    drift["preparedOwners"] = drift["preparedOwners"][:-1]
    drift["preparedLifecycleCalls"][
        ("src/handler/sequencer/UnexpectedPreparedSurface.cpp",
         "beginPreparedPatternEdit")
    ] += 1
    central_method = PREPARED_PATTERN_CENTRAL_METHODS[0]
    drift["preparedCentralDefinitions"][central_method] -= 1
    owner_key = next(iter(drift["preparedOwnerReferences"]))
    drift["preparedOwnerReferences"][owner_key] -= 1
    drift["preparedForbiddenRawCalls"][
        (PREPARED_PATTERN_SURFACES[0], "captureHistorySnapshot")
    ] += 1
    errors = observation_errors(manifest, drift)
    labels = (
        "prepared Pattern owner declaration mismatch",
        "prepared lifecycle call mismatch",
        "prepared central definition mismatch",
        "prepared owner reference mismatch",
        "forbidden prepared-surface raw call mismatch",
    )
    return all(any(label in error for error in errors) for label in labels)

def seam_self_test(manifest) -> bool:
    seam = manifest["failureInjection"]
    macro = seam["macro"]
    guard = f"#if defined({macro})"
    helpers = "\n".join(
        f"void* {helper}() {{\n"
        f"{guard}\n"
        "  if (testing::consumeExtmemAllocationFailure()) return nullptr;\n"
        "#endif\n"
        "  return nullptr;\n"
        "}"
        for helper in seam["guardedHelpers"]
    )
    allocator = (
        f"{guard}\n"
        "namespace testing {\n"
        "bool consumeExtmemAllocationFailure() { return false; }\n"
        "}\n"
        "#endif\n"
        f"{helpers}\n"
    )
    cmake = (
        "if(MS_CORE_BUILD_TESTS AND BUILD_TESTING)\n"
        "  target_compile_definitions(\n"
        f"    {seam['nativeTarget']} PUBLIC {macro}=1)\n"
        "endif()\n"
    )

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        allocator_path = root / seam["allocatorPath"]
        allocator_path.parent.mkdir(parents=True)
        allocator_path.write_text(allocator, encoding="utf-8")
        (root / seam["cmakePath"]).write_text(cmake, encoding="utf-8")
        for rel, expected_count in seam["allowedTestUses"].items():
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                (f"#if defined({macro})\n#endif\n" * expected_count),
                encoding="utf-8",
            )
        if seam_errors(root, manifest):
            return False
        allocator_path.write_text(
            allocator.replace(
                "if (testing::consumeExtmemAllocationFailure()) return nullptr;",
                "if (false) return nullptr;",
                1,
            ),
            encoding="utf-8",
        )
        return any(
            "makeExtmemUnique must consume the failure seam exactly once" in error
            for error in seam_errors(root, manifest)
        )

def run_self_tests(manifest) -> list[str]:
    failures = []
    baseline_errors = manifest_errors(manifest)
    baseline_observation = synthetic_observation(manifest)
    baseline_errors += observation_errors(manifest, baseline_observation)
    if baseline_errors:
        return ["baseline manifest is invalid: " + "; ".join(baseline_errors)]

    added = copy.deepcopy(baseline_observation)
    added["members"][("src/handler/sequencer/UnexpectedHistorySink.cpp", "recordPattern")] += 1
    added_errors = observation_errors(manifest, added)
    if not any("member dispatch mismatch" in error for error in added_errors):
        failures.append("added member-dispatch call was not rejected")

    removed = copy.deepcopy(baseline_observation)
    removed_key = sorted(removed["members"], key=str)[0]
    removed["members"][removed_key] -= 1
    if removed["members"][removed_key] == 0:
        del removed["members"][removed_key]
    removed_errors = observation_errors(manifest, removed)
    if not any("member dispatch mismatch" in error for error in removed_errors):
        failures.append("removed member-dispatch call was not rejected")

    misclassified = copy.deepcopy(manifest)
    for group in misclassified["recordingBoundary"]["memberDispatch"]["groups"]:
        if group.get("role") == "sink" and group.get("classification") == "post-fallible":
            group["classification"] = "prepared"
            break
    classification_errors = manifest_errors(misclassified)
    if not any("sink classification totals differ" in error for error in classification_errors):
        failures.append("wrong sink classification was not rejected")

    method_drift = copy.deepcopy(manifest)
    method_drift["recordingBoundary"]["memberDispatch"]["methods"][-1] = (
        "recordUnexpected"
    )
    method_errors = manifest_errors(method_drift)
    if not any("canonical eight-method boundary" in error for error in method_errors):
        failures.append("recording-method set drift was not rejected")

    provider_drift = copy.deepcopy(manifest)
    for group in provider_drift["recordingBoundary"]["memberDispatch"]["groups"]:
        if group.get("providerForwardCount", 0) > 0:
            group["providerForwardCount"] -= 1
            break
    provider_errors = manifest_errors(provider_drift)
    if not any("provider-forward manifest total" in error for error in provider_errors):
        failures.append("unclassified provider-forward drift was not rejected")

    removal_drift = copy.deepcopy(manifest)
    removal_drift["recordingBoundary"]["expectedMigratedRemovalTotal"] -= 1
    removal_errors = manifest_errors(removal_drift)
    if not any("removal total must remain 11" in error for error in removal_errors):
        failures.append("L-R08-04 migrated-removal drift was not rejected")

    if not scanner_self_test(manifest):
        failures.append("source scanner/sanitizer fixture was not classified exactly")
    if not non_member_topology_self_test(manifest):
        failures.append("forward/adapter/exclusion/begin drift was not rejected")
    if not prepared_lifecycle_self_test(manifest):
        failures.append("prepared lifecycle scanner or topology drift was not rejected")
    if not seam_self_test(manifest):
        failures.append("failure-seam fixture or broken-helper mutation was not detected")
    return failures


def load_manifest(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Check the L-R08-04 Sequencer history inventory ratchet."
    )
    parser.add_argument("--root", type=Path, default=ROOT, help="Core repository root")
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help="Inventory manifest",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Run inventory, scanner and failure-seam mutation tests",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        manifest = load_manifest(args.manifest)
    except (OSError, json.JSONDecodeError) as error:
        print(f"Sequencer history inventory: ERROR: {error}", file=sys.stderr)
        return 1

    if args.self_test:
        failures = run_self_tests(manifest)
        if failures:
            for failure in failures:
                print(f"[FAIL] {failure}", file=sys.stderr)
            return 1
        print("Sequencer history inventory self-tests: OK (10/10)")
        return 0

    root = args.root.resolve()
    errors = manifest_errors(manifest)
    if not errors:
        try:
            observed = collect_observation(root, manifest)
            errors += observation_errors(manifest, observed)
            errors += seam_errors(root, manifest)
        except OSError as error:
            errors.append(str(error))

    if errors:
        for error in errors:
            print(f"[FAIL] {error}", file=sys.stderr)
        return 1

    boundary = manifest["recordingBoundary"]
    members = boundary["memberDispatch"]
    classifications = members["expectedSinkClassifications"]
    begins = manifest["coalescedBegins"]
    print("Sequencer history inventory: OK")
    print(
        "  recording calls: "
        f"{boundary['expectedTotal']} "
        f"({members['expectedTotal']} member-dispatch + "
        f"{boundary['coreStateAdapter']['expectedCount']} CoreState adapter + "
        f"{boundary['internalForwards']['expectedTotal']} internal forwards)"
    )
    print(
        "  mutation sinks: "
        f"{members['expectedSinkTotal']} "
        f"({classifications['post-fallible']} post-fallible + "
        f"{classifications['prepared']} prepared + "
        f"{classifications['rollback-aware']} rollback-aware)"
    )
    lifecycle = manifest["preparedPatternLifecycle"]
    print(
        "  prepared Pattern lifecycle: "
        f"{lifecycle['ownerDeclaration']['expectedCount']} owners / "
        f"{lifecycle['expectedSurfaceCount']} migrated surfaces; "
        "zero raw capture/record calls"
    )
    print(
        "  coalesced begins: "
        f"{begins['expectedBusinessTotal']} business + "
        f"{begins['expectedAdapterTotal']} adapter"
    )
    seals = manifest["coalescedSeals"]
    print(
        "  coalesced seals: "
        f"{seals['expectedBusinessTotal']} business + "
        f"{seals['expectedAdapterTotal']} adapter; all business returns authoritative"
    )
    print("  EXTMEM failure seam: test-only; 4/4 allocator helpers guarded")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
