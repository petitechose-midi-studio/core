"""Load the selected HAL's USB integration before PlatformIO builds Arduino."""

import runpy
from pathlib import Path

from platformio.package.manager.library import LibraryPackageManager

Import("env")

# PRE hooks run before dependency installation. Use PlatformIO's package
# manager (including its symlink support), so clean builds work as well.
manager = LibraryPackageManager(env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV"))
spec = next(dep for dep in env.GetProjectOption("lib_deps")
            if dep.startswith("oc-hal-teensy="))
package = manager.install(spec)
hook = Path(package.path) / "script/usb_midi_sdk.py"
if hook.is_file():
    runpy.run_path(str(hook))["configure"](env)
# Older release pins predate the adapter and continue to compile their own
# original transport. A HAL using the new API cannot link without this hook.
