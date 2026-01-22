# pyright: reportUndefinedVariable=false, reportMissingImports=false
"""OC Bridge Service Manager for PlatformIO.

Stops the OC Bridge service before upload (to free the serial port), then
restarts it after.

This runs no matter how upload is invoked (pio directly, oc-upload, ms upload),
because it's attached to the PlatformIO upload target.
"""

from __future__ import annotations

import atexit
import os
import platform
import subprocess
import time


SERVICE_NAMES: dict[str, str] = {
    "Windows": "OpenControlBridge",
    "Linux": "open-control-bridge",
    "Darwin": "com.petitechose.open-control-bridge",
}

_system = platform.system()
_was_running = False
_needs_restart = False
_env_cache: dict[str, str] | None = None


def _get_env() -> dict[str, str]:
    """Get environment with minimal systemd user session fixes (Linux)."""

    global _env_cache
    if _env_cache is not None:
        return _env_cache

    env = os.environ.copy()

    if _system == "Linux":
        uid = os.getuid()
        env.setdefault("XDG_RUNTIME_DIR", f"/run/user/{uid}")
        env.setdefault("DBUS_SESSION_BUS_ADDRESS", f"unix:path=/run/user/{uid}/bus")

    _env_cache = env
    return env


def _run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, capture_output=True, text=True, env=_get_env())


def _service_name() -> str | None:
    return SERVICE_NAMES.get(_system)


def _is_running() -> bool:
    """Check if OC Bridge service is running."""

    name = _service_name()
    if not name:
        return False

    try:
        if _system == "Windows":
            r = _run(["sc", "query", name])
            return "RUNNING" in (r.stdout + r.stderr)

        if _system == "Linux":
            r = _run(["systemctl", "--user", "is-active", name])
            return r.returncode == 0

        if _system == "Darwin":
            # launchctl list <label> returns 0 if loaded, non-zero otherwise.
            r = _run(["launchctl", "list", name])
            return r.returncode == 0

    except FileNotFoundError:
        pass

    return False


def _hint_stop_cmd() -> str:
    name = _service_name() or "<service>"
    if _system == "Windows":
        return f"sc stop {name}"
    if _system == "Linux":
        return f"systemctl --user stop {name}"
    if _system == "Darwin":
        return f"launchctl stop {name}"
    return ""


def _hint_start_cmd() -> str:
    name = _service_name() or "<service>"
    if _system == "Windows":
        return f"sc start {name}"
    if _system == "Linux":
        return f"systemctl --user start {name}"
    if _system == "Darwin":
        return f"launchctl start {name}"
    return ""


def _wait_for_running(target_running: bool, timeout_s: float = 5.0) -> bool:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if _is_running() == target_running:
            return True
        time.sleep(0.2)
    return _is_running() == target_running


def _atexit_restart() -> None:
    """Best-effort restart if upload fails before post action runs."""

    global _needs_restart
    if not _needs_restart:
        return
    _needs_restart = False

    name = _service_name() or "service"
    print(f"[OC] Restarting {name} (cleanup)...")
    if _start_service():
        _wait_for_running(True)


def _stop_service() -> bool:
    name = _service_name()
    if not name:
        return False

    try:
        if _system == "Windows":
            r = _run(["sc", "stop", name])
            out = r.stdout + r.stderr
            return r.returncode == 0 or "STOP_PENDING" in out

        if _system == "Linux":
            r = _run(["systemctl", "--user", "stop", name])
            return r.returncode == 0

        if _system == "Darwin":
            r = _run(["launchctl", "stop", name])
            return r.returncode == 0

    except FileNotFoundError:
        pass

    return False


def _start_service() -> bool:
    name = _service_name()
    if not name:
        return False

    try:
        if _system == "Windows":
            r = _run(["sc", "start", name])
            out = r.stdout + r.stderr
            return r.returncode == 0 or "START_PENDING" in out

        if _system == "Linux":
            r = _run(["systemctl", "--user", "start", name])
            return r.returncode == 0

        if _system == "Darwin":
            r = _run(["launchctl", "start", name])
            return r.returncode == 0

    except FileNotFoundError:
        pass

    return False


def before_upload(source, target, env):
    """Stop service before upload to free serial port."""

    global _was_running, _needs_restart

    _was_running = _is_running()
    if not _was_running:
        return

    name = _service_name() or "service"
    print(f"[OC] Stopping {name}...")

    if not _stop_service():
        hint = _hint_stop_cmd()
        msg = "[OC] Warning: could not stop service"
        if hint:
            msg += f" (try: {hint})"
        print(msg)
        return

    # Ensure we restart even if upload fails mid-way.
    if not _needs_restart:
        _needs_restart = True
        atexit.register(_atexit_restart)

    if _wait_for_running(False):
        print("[OC] Service stopped")
    else:
        print("[OC] Warning: service stop timed out (serial port may still be busy)")


def after_upload(source, target, env):
    """Restart service after upload."""

    global _needs_restart

    if not _was_running:
        return

    name = _service_name() or "service"
    print(f"[OC] Restarting {name}...")

    if not _start_service():
        hint = _hint_start_cmd()
        msg = "[OC] Warning: could not restart service"
        if hint:
            msg += f" (try: {hint})"
        print(msg)
        return

    _needs_restart = False

    if _wait_for_running(True):
        print("[OC] Service restarted")
    else:
        print("[OC] Warning: service start timed out")


Import("env")
env.AddPreAction("upload", before_upload)
env.AddPostAction("upload", after_upload)
