"""PlatformIO pre-build script: apply AS923-TW band patch to RadioLib.

NCC Taiwan does not mandate the AS923 400 ms dwell-time cap and ChirpStack's
default LinkADRReq / TxParamSetupReq behavior conflicts with our deployment.
Upstream RadioLib's AS923 band entry needs five fields adjusted; see
21_XDA003B/firmware/docs/decision_log 2026-05-06 for the rationale.

Idempotent: safe to run repeatedly. Detects an already-patched file by the
sentinel comment and skips. Lives in the siliqs_ct62 library so the same
patch can be reused from any HT-CT62 project.

Usage in platformio.ini:

    extra_scripts =
        pre:lib/siliqs_ct62/scripts/patch_radiolib_as923tw.py

(Path is relative to the PlatformIO project root.)
"""

import os
import re
import sys

Import("env")  # type: ignore  # PlatformIO injects this

SENTINEL = "// siliqs_ct62 AS923-TW patch applied"

PATCH_FIELDS = [
    (
        re.compile(r"\.powerMax\s*=\s*\d+,"),
        ".powerMax = 30,        // siliqs_ct62 AS923-TW: SX1262 hw clamps to 22 dBm",
    ),
    (
        re.compile(r"\.powerNumSteps\s*=\s*\d+,"),
        ".powerNumSteps = 1,    // siliqs_ct62 AS923-TW: lock step 0, reject server LinkADRReq step-downs",
    ),
    (
        re.compile(r"\.dwellTimeUp\s*=\s*[A-Za-z0-9_]+,"),
        ".dwellTimeUp = 0,      // siliqs_ct62 AS923-TW: NCC Taiwan does not mandate 400 ms dwell",
    ),
    (
        re.compile(r"\.dwellTimeDn\s*=\s*[A-Za-z0-9_]+,"),
        ".dwellTimeDn = 0,      // siliqs_ct62 AS923-TW: same",
    ),
    (
        re.compile(r"\.txParamSupported\s*=\s*(true|false),"),
        ".txParamSupported = false,  // siliqs_ct62 AS923-TW: ignore TxParamSetupReq",
    ),
]

# Legacy sentinel from before this script lived in siliqs_ct62 (was inlined in
# 21_XDA003B/firmware/scripts/). Treat it as "already patched" so we don't
# double-patch projects that built once with the old script.
LEGACY_SENTINELS = ("// XDA003B AS923-TW patch applied",)


def find_bands_cpp(libdeps_dir: str) -> str:
    for root, _dirs, files in os.walk(libdeps_dir):
        if "LoRaWANBands.cpp" in files and root.endswith(
            os.path.join("RadioLib", "src", "protocols", "LoRaWAN")
        ):
            return os.path.join(root, "LoRaWANBands.cpp")
    return ""


def patch_as923_block(text: str) -> tuple[str, int]:
    if SENTINEL in text or any(s in text for s in LEGACY_SENTINELS):
        return text, 0

    # Locate the AS923 band block. RadioLib defines bands as
    # `const LoRaWANBand_t AS923 = { ... };` — patch the first AS923 block only.
    m = re.search(r"const\s+LoRaWANBand_t\s+AS923\s*=\s*\{", text)
    if not m:
        return text, -1
    start = m.start()
    depth = 0
    end = start
    for i in range(m.end() - 1, len(text)):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    block = text[start:end]
    new_block = block
    changed = 0
    for pattern, replacement in PATCH_FIELDS:
        new_block, n = pattern.subn(replacement, new_block, count=1)
        changed += n
    if changed == 0:
        return text, 0
    new_block = "  " + SENTINEL + "\n  " + new_block.lstrip()
    return text[:start] + new_block + text[end:], changed


def main() -> None:
    build_dir = env.subst("$PROJECT_LIBDEPS_DIR")  # type: ignore
    pioenv = env.subst("$PIOENV")  # type: ignore
    libdeps = os.path.join(build_dir, pioenv)
    if not os.path.isdir(libdeps):
        print(f"[as923-patch] libdeps not yet present at {libdeps}; will apply after first lib install")
        return

    bands_cpp = find_bands_cpp(libdeps)
    if not bands_cpp:
        print("[as923-patch] LoRaWANBands.cpp not found yet (RadioLib not installed); will retry next build")
        return

    with open(bands_cpp, "r", encoding="utf-8") as f:
        text = f.read()

    new_text, changed = patch_as923_block(text)
    if changed == 0:
        print(f"[as923-patch] already applied at {bands_cpp}")
        return
    if changed < 0:
        print(f"[as923-patch] WARNING: AS923 block not found in {bands_cpp}; aborting", file=sys.stderr)
        sys.exit(1)

    with open(bands_cpp, "w", encoding="utf-8") as f:
        f.write(new_text)
    print(f"[as923-patch] applied {changed} field updates to {bands_cpp}")


main()
