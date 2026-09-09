"""Exercise the PlatformIO script with a tiny workspace and standard-library fakes."""
import hashlib
import json
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "pio/hardware_benchmark_identity.py"


class IdentityTest(unittest.TestCase):
    def test_identity_tracks_inputs_without_global_compiler_definitions(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core = root / "midi-studio/core"
            canonical_core = core
            core.mkdir(parents=True)
            source = core / "main.cpp"
            source.write_text("int main() {}\n", encoding="utf-8")
            libdeps = root / "libdeps/benchmark/lvgl"
            libdeps.mkdir(parents=True)
            library = libdeps / "draw.c"
            library.write_text("void draw(void) {}\n", encoding="utf-8")
            package = root / "package"
            package.mkdir()
            (package / "package.json").write_text('{"version":"1.0"}', encoding="utf-8")
            teensy = package / "cores/teensy4"
            teensy.mkdir(parents=True)
            (teensy / "usb.c").write_text("void usb(void) {}\n", encoding="utf-8")
            build = root / "build"

            class Environment:
                def __init__(self):
                    self.appended = {}

                def subst(self, name):
                    return {"$PROJECT_DIR": str(core), "$PIOENV": "benchmark",
                            "$PROJECT_LIBDEPS_DIR": str(root / "libdeps"),
                            "$BUILD_DIR": str(build)}[name]

                def GetProjectOption(self, name, default):
                    return "test-recipe"

                def PioPlatform(self):
                    return self

                def get_package_dir(self, name):
                    return str(package)

                def Append(self, **kwargs):
                    self.appended.update(kwargs)

            def git_files(command):
                return b"main.cpp\0" if Path(command[2]) in (core, canonical_core) else b""

            def generate():
                env = Environment()
                with patch("subprocess.check_output", side_effect=git_files):
                    runpy.run_path(str(SCRIPT), init_globals={"env": env, "Import": lambda _: None})
                self.assertEqual(env.appended, {"CPPPATH": [str(build)]})
                manifest = json.loads((build / "hardware-benchmark-manifest.json").read_text())
                identity = manifest.pop("build_id")
                canonical = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
                self.assertEqual(identity, hashlib.sha256(canonical).hexdigest())
                self.assertIn(f'"{identity}"', header.read_text())
                return identity

            header = build / "HardwareBenchmarkBuildIdentity.hpp"
            initial = generate()
            modified = header.stat().st_mtime_ns
            self.assertEqual(generate(), initial)
            self.assertEqual(header.stat().st_mtime_ns, modified)
            source.write_text("int main() { return 1; }\n", encoding="utf-8")
            source_changed = generate()
            self.assertNotEqual(source_changed, initial)
            library.write_text("void draw(void) { /* patched */ }\n", encoding="utf-8")
            library_changed = generate()
            self.assertNotEqual(library_changed, source_changed)
            (package / "package.json").write_text('{"version":"2.0"}', encoding="utf-8")
            self.assertNotEqual(generate(), library_changed)
            # A sibling worktree must identify the compiled checkout, even when
            # a different canonical Core checkout exists in the same workspace.
            core = root / "midi-studio/core-worktree"
            core.mkdir()
            worktree_source = core / "main.cpp"
            worktree_source.write_text("int main() { return 2; }\n", encoding="utf-8")
            worktree_identity = generate()
            manifest = json.loads((build / "hardware-benchmark-manifest.json").read_text())
            self.assertEqual(manifest["files"]["midi-studio/core/main.cpp"],
                             hashlib.sha256(worktree_source.read_bytes()).hexdigest())
            source.write_text("int main() { return 3; }\n", encoding="utf-8")
            self.assertEqual(generate(), worktree_identity)
            worktree_source.write_text("int main() { return 4; }\n", encoding="utf-8")
            self.assertNotEqual(generate(), worktree_identity)
            with patch.object(Environment, "get_package_dir", return_value=None):
                with self.assertRaisesRegex(RuntimeError, "missing package"):
                    generate()


if __name__ == "__main__":
    unittest.main()
