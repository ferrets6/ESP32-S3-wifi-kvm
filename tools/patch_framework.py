"""
Patches the arduino-esp32 core (endpoint reservation + HID descriptor shape,
see patches/framework/patched/*) into the globally installed
framework-arduinoespressif32 package, but ONLY for the duration of the
esp32-s3-supermini-kbonly build -- see README for what the patch does and
why (BIOS/UEFI/GRUB boot-keyboard compatibility).

We tried PlatformIO's documented local-package-override mechanism
(platform_packages @ file://) first, to keep the modification fully
isolated to this one environment. It doesn't work for this platform: the
espressif32 Arduino framework builder script resolves the package directory
by name and compiles straight from the real global install regardless of
the override, so it doesn't help. Falling back to what's actually
achievable: patch the global files right before this env's build, and
restore them right after -- both files are only ever changed for the
kbonly environment, and are restored to pristine every time it finishes.
The default (keyboard+mouse) environment never touches them: the patch's
non-KVM_KEYBOARD_ONLY branch was verified byte-identical to upstream, so
there is nothing to patch for it.

Self-healing: every invocation, for every environment, first checks the
installed files' content against known hashes (manifest.json). If a file
still carries our patch (e.g. a previous kbonly build crashed before its
restore step ran), it's silently restored to pristine before anything else
happens. If a file matches neither known state, the build aborts instead
of guessing -- something else modified it and it needs a human look.
"""

import atexit
import hashlib
import json
import shutil
from pathlib import Path

Import("env")

PATCH_DIR_NAME = "patches/framework"
MANIFEST_NAME = "manifest.json"


def sha256_of(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fail(message: str):
    raise SystemExit(f"[patch_framework] BLOCKED: {message}")


def main():
    project_dir = Path(env["PROJECT_DIR"])
    patch_dir = project_dir / PATCH_DIR_NAME
    framework_dir = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))

    manifest = json.loads((patch_dir / MANIFEST_NAME).read_text(encoding="utf-8"))

    installed_pkg = json.loads((framework_dir / "package.json").read_text(encoding="utf-8"))
    installed_version = installed_pkg.get("version")
    expected_version = manifest["framework_version"]
    if installed_version != expected_version:
        fail(
            f"installed framework-arduinoespressif32 is {installed_version}, "
            f"but these patches were only verified against {expected_version}. "
            f"Re-check patches/framework/patched/*.c/.cpp against the new "
            f"version's upstream source, then update patches/framework/manifest.json."
        )

    targets = []
    for filename, info in manifest["files"].items():
        pristine_src = patch_dir / "pristine" / filename
        patched_src = patch_dir / "patched" / filename
        dst = framework_dir / info["installed_path"]
        targets.append((filename, pristine_src, patched_src, dst, info))

        if not dst.is_file():
            fail(f"expected installed file not found: {dst}")
        current_hash = sha256_of(dst)
        if current_hash == info["patched_sha256"]:
            # Leftover from a build that never reached its restore step
            # (crash / Ctrl+C). Heal it before doing anything else.
            shutil.copyfile(pristine_src, dst)
            print(f"[patch_framework] {filename} was left patched from a previous run -- restored to pristine")
        elif current_hash != info["pristine_sha256"]:
            fail(
                f"{dst} matches neither the known pristine nor patched content "
                f"for framework {expected_version}. Refusing to touch it -- "
                f"inspect it manually first."
            )

    is_kbonly_build = env["PIOENV"] == manifest["kbonly_env"]
    if not is_kbonly_build:
        return

    for filename, pristine_src, patched_src, dst, info in targets:
        shutil.copyfile(patched_src, dst)
        print(f"[patch_framework] applied {filename} -> {dst}")

    def restore_pristine():
        # atexit, not AddPostAction: an AddPostAction tied to a SCons build
        # target only fires if that target actually re-runs. On a build
        # where nothing changed (cached/up-to-date -- the common case for a
        # second `pio run` in a row), SCons skips it entirely and the patch
        # would stay applied despite the build reporting SUCCESS. atexit
        # fires unconditionally when this process exits, cache or not.
        for filename, pristine_src, patched_src, dst, info in targets:
            shutil.copyfile(pristine_src, dst)
            print(f"[patch_framework] restored {filename} -> {dst}")

    atexit.register(restore_pristine)


main()
