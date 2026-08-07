"""Apply the fork's verified-wolfSSL patch to the FreeInk SDK submodule.

The v1.5.0 SDK transport supports a caller-provided CA but does not fail when
loading it fails and does not configure hostname verification. PlatformIO uses
the SDK through symlink dependencies, so patch it before compilation and restore
the submodule after the ELF has linked (with an atexit fallback for failures).
"""

Import("env")  # noqa: F821 (SCons-injected global)

import atexit
import os
import subprocess
import sys


PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
SDK_DIR = os.path.join(PROJECT_DIR, "freeink-sdk")
PATCH_PATH = os.path.join(
    PROJECT_DIR,
    "scripts",
    "freeink_sdk_patches",
    "0001-verify-wolfssl-peer.patch",
)


def git_apply_check(*, reverse):
    command = ["git", "apply", "--check"]
    if reverse:
        command.append("--reverse")
    command.append(PATCH_PATH)
    return subprocess.run(
        command,
        cwd=SDK_DIR,
        capture_output=True,
        text=True,
    ).returncode == 0


def restore_patch(*_args, **_kwargs):
    if not git_apply_check(reverse=True):
        return
    subprocess.run(
        ["git", "apply", "--reverse", PATCH_PATH],
        cwd=SDK_DIR,
        check=True,
    )
    print("Restored FreeInk SDK after verified-wolfSSL build patch")


if not os.path.isdir(SDK_DIR):
    sys.stderr.write("ERROR: freeink-sdk submodule is not initialized\n")
    raise SystemExit(1)
if not os.path.isfile(PATCH_PATH):
    sys.stderr.write("ERROR: verified-wolfSSL SDK patch is missing\n")
    raise SystemExit(1)

if not git_apply_check(reverse=True):
    if not git_apply_check(reverse=False):
        result = subprocess.run(
            ["git", "apply", "--check", PATCH_PATH],
            cwd=SDK_DIR,
            capture_output=True,
            text=True,
        )
        sys.stderr.write(
            "ERROR: verified-wolfSSL SDK patch does not apply cleanly:\n%s%s\n"
            % (result.stdout, result.stderr)
        )
        raise SystemExit(1)
    subprocess.run(["git", "apply", PATCH_PATH], cwd=SDK_DIR, check=True)
    print("Applied FreeInk SDK verified-wolfSSL patch")

atexit.register(restore_patch)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", restore_patch)  # noqa: F821
