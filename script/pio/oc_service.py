# OC Bridge Service Manager for PlatformIO
#
# Stops OC Bridge service before upload, restarts after.
# Service retry loop will reconnect when Teensy is available.

import subprocess

SERVICE_NAME = "OpenControlBridge"
_was_running = False


def before_upload(source, target, env):
    """Stop service before upload."""
    global _was_running
    try:
        result = subprocess.run(["sc", "query", SERVICE_NAME], capture_output=True, text=True)
        _was_running = "RUNNING" in result.stdout
        if _was_running:
            print("[OC] Stopping service...")
            subprocess.run(["sc", "stop", SERVICE_NAME], capture_output=True)
    except Exception:
        pass


def after_upload(source, target, env):
    """Restart service after upload."""
    global _was_running
    if _was_running:
        print("[OC] Restarting service...")
        subprocess.run(["sc", "start", SERVICE_NAME], capture_output=True)


Import("env")
env.AddPreAction("upload", before_upload)
env.AddPostAction("upload", after_upload)
