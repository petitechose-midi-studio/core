#!/usr/bin/env python3
"""Portable UX workflow runner for the native SDL simulator."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


CORE_ROOT = Path(__file__).resolve().parents[1]
WORKSPACE_ROOT = CORE_ROOT.parents[1]
DEFAULT_WORKFLOW_DIR = CORE_ROOT / "sdl" / "integration" / "workflows"
DEFAULT_OUTPUT_ROOT = CORE_ROOT / ".captures" / "ux" / "workflows"


@dataclass(frozen=True)
class WorkflowDoc:
    title: str
    purpose: str
    expectations: tuple[str, ...]


@dataclass
class WorkflowResult:
    name: str
    ok: bool
    exit_code: int
    capture_count: int
    expected_capture_count: int
    run_end: bool
    dispatch: bool
    expectation_failures: list[str]
    output_dir: Path


def default_exe() -> Path:
    suffix = ".exe" if platform.system().lower() == "windows" else ""
    primary = WORKSPACE_ROOT / "bin" / "core" / "native" / f"midi_studio_core{suffix}"
    fallback = WORKSPACE_ROOT / "bin" / "core" / "native" / "midi_studio_core.exe"
    return primary if primary.exists() or not fallback.exists() else fallback


def resolve_path(path: str | Path, base: Path = CORE_ROOT) -> Path:
    candidate = Path(path)
    if not candidate.is_absolute():
        candidate = base / candidate
    return candidate.resolve()


def read_lines(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8").splitlines()


def workflow_doc(path: Path) -> WorkflowDoc:
    title = path.stem
    purpose: list[str] = []
    expectations: set[str] = set()
    for line in read_lines(path):
        match = re.match(r"^\s*#\s?(.*)$", line)
        if not match:
            if line.strip() or purpose:
                break
            continue
        text = match.group(1).strip()
        if text.lower().startswith("ux workflow:"):
            title = text.split(":", 1)[1].strip().rstrip(".")
        elif text.lower().startswith("purpose:"):
            purpose.append(text.split(":", 1)[1].strip())
        elif purpose and text and not re.match(r"^[A-Z][A-Za-z_-]+:", text):
            purpose.append(text)
        elif text.lower().startswith("expect:"):
            for item in text.split(":", 1)[1].split(","):
                expectation = item.strip().lower()
                if expectation:
                    expectations.add(expectation)
    return WorkflowDoc(title=title, purpose=" ".join(purpose), expectations=tuple(sorted(expectations)))


def capture_declarations(path: Path) -> list[str]:
    captures: list[str] = []
    for line in read_lines(path):
        clean = re.sub(r"\s+(#|//).*$", "", line)
        match = re.match(r"^\s*\d+\s+capture\s+(screen|controller)\s+([A-Za-z0-9_-]+)\s*$", clean, re.I)
        if match:
            captures.append(match.group(2))
    return captures


def read_ndjson(path: Path) -> list[dict]:
    if not path.exists():
        return []
    rows: list[dict] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip():
            rows.append(json.loads(line))
    return rows


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture_by_id(trace_rows: list[dict], capture_id: str) -> Path | None:
    for row in trace_rows:
        if row.get("event") == "action" and row.get("action") == "capture" and row.get("id") == capture_id:
            capture = row.get("capture")
            if capture:
                return resolve_path(capture)
    return None


def has_playhead_progress(trace_rows: list[dict]) -> bool:
    steps = {
        int(row["playhead_step"])
        for row in trace_rows
        if row.get("event") == "action"
        and row.get("playing") is True
        and row.get("playhead_step") is not None
        and int(row["playhead_step"]) >= 0
    }
    return len(steps) > 1


def has_overlay_exclusive_stability(trace_rows: list[dict]) -> bool:
    early = capture_by_id(trace_rows, "selector_open_early")
    late = capture_by_id(trace_rows, "selector_open_late")
    return bool(early and late and early.exists() and late.exists() and file_hash(early) == file_hash(late))


def list_workflows(workflow_dir: Path) -> list[Path]:
    return sorted(workflow_dir.glob("*.ux"), key=lambda path: path.name)


def select_workflows(workflow_dir: Path, names: list[str]) -> list[Path]:
    workflows = list_workflows(workflow_dir)
    if not names:
        return workflows
    by_stem = {path.stem: path for path in workflows}
    by_name = {path.name: path for path in workflows}
    selected: list[Path] = []
    missing: list[str] = []
    for name in names:
        match = by_stem.get(name) or by_name.get(name)
        if match:
            selected.append(match)
        else:
            missing.append(name)
    if missing:
        raise SystemExit(f"Unknown workflow(s): {', '.join(missing)}")
    return selected


def interactive_selection(workflow_dir: Path) -> list[Path]:
    workflows = list_workflows(workflow_dir)
    print("UX workflows:")
    print("  0. all")
    for index, workflow in enumerate(workflows, start=1):
        doc = workflow_doc(workflow)
        print(f"  {index}. {workflow.stem} - {doc.title}")
    choice = input("Selection (numbers or names, comma-separated) [all]: ").strip()
    if not choice or choice.lower() in {"0", "all", "*"}:
        return workflows
    names: list[str] = []
    for token in re.split(r"[\s,]+", choice):
        if not token:
            continue
        if token.isdigit():
            index = int(token)
            if index < 1 or index > len(workflows):
                raise SystemExit(f"Selection out of range: {token}")
            names.append(workflows[index - 1].stem)
        else:
            names.append(token)
    return select_workflows(workflow_dir, names)


def ensure_inside(child: Path, parent: Path) -> None:
    try:
        child.relative_to(parent)
    except ValueError as exc:
        raise SystemExit(f"Refusing to write outside output root: {child}") from exc


def build_native() -> None:
    subprocess.run(["ms", "build", "core", "--target", "native"], cwd=CORE_ROOT, check=True)


def run_one(exe: Path, workflow: Path, output_root: Path) -> WorkflowResult:
    out_dir = (output_root / workflow.stem).resolve()
    ensure_inside(out_dir, output_root)
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(workflow, out_dir / workflow.name)

    completed = subprocess.run(
        [str(exe), "--ux-script", str(workflow), "--ux-output", str(out_dir)],
        cwd=CORE_ROOT,
        check=False,
    )

    trace_path = out_dir / "trace.ndjson"
    binding_trace_path = out_dir / "binding-trace.ndjson"
    trace_rows = read_ndjson(trace_path)
    binding_rows = read_ndjson(binding_trace_path)
    expectations = workflow_doc(workflow).expectations
    failures: list[str] = []

    if "playhead_progress" in expectations and not has_playhead_progress(trace_rows):
        failures.append("playhead_progress")
    if "overlay_exclusive" in expectations and not has_overlay_exclusive_stability(trace_rows):
        failures.append("overlay_exclusive")

    run_end = any(row.get("event") == "run_end" for row in trace_rows)
    dispatch = any(row.get("stage") == "dispatch" for row in binding_rows)
    capture_count = len(list(out_dir.glob("*.bmp")))
    expected_capture_count = len(capture_declarations(workflow))
    ok = (
        completed.returncode == 0
        and trace_path.exists()
        and binding_trace_path.exists()
        and run_end
        and dispatch
        and capture_count >= expected_capture_count
        and not failures
    )

    return WorkflowResult(
        name=workflow.name,
        ok=ok,
        exit_code=completed.returncode,
        capture_count=capture_count,
        expected_capture_count=expected_capture_count,
        run_end=run_end,
        dispatch=dispatch,
        expectation_failures=failures,
        output_dir=out_dir,
    )


def write_report(
    output_root: Path,
    workflow_dir: Path,
    report_path: Path | None = None,
    workflows: list[Path] | None = None,
) -> Path:
    report = (report_path or (output_root / "report.md")).resolve()
    report.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# UX Workflow Report",
        "",
        "Generated from `.ux` scripts, replay traces, binding traces, and BMP captures.",
        "",
    ]

    for workflow in workflows or list_workflows(workflow_dir):
        out_dir = output_root / workflow.stem
        doc = workflow_doc(workflow)
        trace_rows = read_ndjson(out_dir / "trace.ndjson")
        binding_rows = read_ndjson(out_dir / "binding-trace.ndjson")
        captures = [row for row in trace_rows if row.get("event") == "action" and row.get("action") == "capture"]
        dispatches = [row for row in binding_rows if row.get("stage") == "dispatch"]
        lines += [
            f"## {workflow.stem}",
            "",
            f"- Title: {doc.title}",
            f"- Purpose: {doc.purpose or '-'}",
            f"- Expectations: {', '.join(doc.expectations) if doc.expectations else '-'}",
            f"- Captures: {len(captures)}/{len(capture_declarations(workflow))}",
            f"- Dispatches: {len(dispatches)}",
            "",
        ]
        for capture in captures:
            capture_path = resolve_path(capture.get("capture", ""))
            rel = os.path.relpath(capture_path, report.parent).replace(os.sep, "/")
            digest = file_hash(capture_path)[:12] if capture_path.exists() else "missing"
            lines.append(f"![{capture.get('id', 'capture')}]({rel})")
            lines.append("")
            lines.append(f"`{capture.get('id')}` `{digest}`")
            lines.append("")

    report.write_text("\n".join(lines), encoding="utf-8")
    return report


def command_list(args: argparse.Namespace) -> int:
    workflow_dir = resolve_path(args.workflow_dir)
    for workflow in list_workflows(workflow_dir):
        doc = workflow_doc(workflow)
        expects = f" [{', '.join(doc.expectations)}]" if doc.expectations else ""
        print(f"{workflow.stem}: {doc.title}{expects}")
    return 0


def command_run(args: argparse.Namespace) -> int:
    workflow_dir = resolve_path(args.workflow_dir)
    output_root = resolve_path(args.output_root)
    output_root.mkdir(parents=True, exist_ok=True)
    exe = resolve_path(args.exe) if args.exe else default_exe()

    workflows = interactive_selection(workflow_dir) if args.interactive else select_workflows(workflow_dir, args.workflows)
    if not workflows:
        raise SystemExit(f"No UX workflows found in {workflow_dir}")

    if not args.skip_build:
        build_native()
    if not exe.exists():
        raise SystemExit(f"Native executable not found: {exe}")

    failures: list[WorkflowResult] = []
    for workflow in workflows:
        result = run_one(exe, workflow, output_root)
        expects = workflow_doc(workflow).expectations
        suffix = f" expects={','.join(expects)}" if expects else ""
        if result.ok:
            print(f"OK   {result.name} captures={result.capture_count}/{result.expected_capture_count}{suffix}")
        else:
            failures.append(result)
            print(
                "FAIL "
                f"{result.name} exit={result.exit_code} "
                f"captures={result.capture_count}/{result.expected_capture_count} "
                f"run_end={result.run_end} dispatch={result.dispatch} "
                f"expectation_failures={','.join(result.expectation_failures)}"
            )

    if args.report:
        report = write_report(
            output_root,
            workflow_dir,
            resolve_path(args.report_path) if args.report_path else None,
            workflows,
        )
        print(f"Report: {report}")

    if failures:
        print(f"UX workflow verification failed: {len(failures)}/{len(workflows)}", file=sys.stderr)
        return 1
    print(f"UX workflow verification OK: {len(workflows)}/{len(workflows)}")
    return 0


def command_report(args: argparse.Namespace) -> int:
    output_root = resolve_path(args.output_root)
    workflow_dir = resolve_path(args.workflow_dir)
    report = write_report(output_root, workflow_dir, resolve_path(args.report_path) if args.report_path else None)
    print(f"Report: {report}")
    return 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run native SDL UX workflows.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="List available workflows.")
    list_parser.add_argument("--workflow-dir", default=str(DEFAULT_WORKFLOW_DIR))
    list_parser.set_defaults(func=command_list)

    run_parser = subparsers.add_parser("run", help="Run workflows directly or interactively.")
    run_parser.add_argument("workflows", nargs="*", help="Workflow stems or .ux filenames. Empty means all.")
    run_parser.add_argument("--workflow-dir", default=str(DEFAULT_WORKFLOW_DIR))
    run_parser.add_argument("--output-root", default=str(DEFAULT_OUTPUT_ROOT))
    run_parser.add_argument("--exe", help="Native simulator executable.")
    run_parser.add_argument("--skip-build", action="store_true")
    run_parser.add_argument("--interactive", "-i", action="store_true")
    run_parser.add_argument("--report", action="store_true")
    run_parser.add_argument("--report-path")
    run_parser.set_defaults(func=command_run)

    report_parser = subparsers.add_parser("report", help="Regenerate a report from existing outputs.")
    report_parser.add_argument("--workflow-dir", default=str(DEFAULT_WORKFLOW_DIR))
    report_parser.add_argument("--output-root", default=str(DEFAULT_OUTPUT_ROOT))
    report_parser.add_argument("--report-path")
    report_parser.set_defaults(func=command_report)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
