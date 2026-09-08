#!/usr/bin/env python3
"""Compile recovery regressions against a temporary copy of real RadioLib 7.7.1.

SPI/GPIO are simulated; this does not verify hardware or real RF timing.
The installed dependency and build caches are left unchanged.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile


def main() -> int:
    firmware = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--lib-dir", type=Path,
        default=firmware / ".pio/libdeps/fanlink_tx_esp8285/RadioLib",
        help="installed RadioLib 7.7.1 directory (default: TX environment)",
    )
    parser.add_argument(
        "--reproduce-tx-dio-high", action="store_true",
        help="run only the missing-TX_DONE/stuck-high-DIO regression (also in the default suite)",
    )
    args = parser.parse_args()
    if not (args.lib_dir / "library.json").is_file():
        parser.error(
            f"RadioLib dependency is missing at {args.lib_dir}; first build a named "
            "PlatformIO environment, or pass its RadioLib directory via --lib-dir"
        )

    with tempfile.TemporaryDirectory(prefix="fanlink-radio-tests-") as temporary:
        work = Path(temporary)
        dependency = work / "RadioLib"
        shutil.copytree(args.lib_dir, dependency)

        class PatchEnvironment:
            def subst(self, key: str) -> str:
                return {"$PROJECT_LIBDEPS_DIR": str(work), "$PIOENV": "."}[key]

        patch = runpy.run_path(
            str(firmware / "scripts/platformio_patch_radiolib.py"),
            init_globals={"Import": lambda name: None, "env": PatchEnvironment()},
        )
        source = dependency / "src/modules/SX128x/SX128x.cpp"
        first = source.read_bytes()
        patch["patch_radiolib"](dependency)
        if source.read_bytes() != first:
            raise RuntimeError("RadioLib integration patch is not idempotent")

        # Also prove a fresh upstream source gets the same complete patch; the
        # installed cache may already contain an earlier FanLink integration.
        upstream_text = first.decode("utf-8")
        for prefix in (
            "REGULATOR", "BUSY", "TX_DONE", "TX_VERIFY", "LAUNCH_RX", "LAUNCH_TX",
            "FINISH_TX", "RESET_VERIFY", "FIND_CHIP",
        ):
            upstream_text = upstream_text.replace(
                patch[f"{prefix}_PATCHED"], patch[f"{prefix}_UPSTREAM"], 1
            )
        source.write_text(upstream_text, encoding="utf-8")
        patch["patch_radiolib"](dependency)
        if source.read_bytes() != first:
            raise RuntimeError("Fresh and previously patched RadioLib sources differ")

        # Changed RX and TX verification contexts must fail without partial writes.
        for old, new in (
            ("state = setRx(this->rxTimeout);", "state = changedRx();"),
            (patch["TX_VERIFY_UPSTREAM"], patch["TX_VERIFY_UPSTREAM"].replace(
                "return(finishTransmit());", "return(changedFinishTransmit());", 1
            )),
        ):
            if upstream_text.count(old) != 1:
                raise RuntimeError("Fail-closed test mutation is not unique")
            altered = upstream_text.replace(old, new, 1)
            source.write_text(altered, encoding="utf-8")
            try:
                patch["patch_radiolib"](dependency)
            except RuntimeError:
                if source.read_text(encoding="utf-8") != altered:
                    raise RuntimeError("Rejected RadioLib source was partially modified")
            else:
                raise RuntimeError("Changed RadioLib source was not rejected")
        source.write_bytes(first)

        root = dependency / "src"
        source_names = (
            "Hal.cpp", "Module.cpp", "protocols/PhysicalLayer/PhysicalLayer.cpp",
            "modules/SX128x/SX128x.cpp", "modules/SX128x/SX1280.cpp",
            "modules/SX128x/SX1281.cpp", "utils/Utils.cpp",
        )
        binary = work / "radiolib_recovery_tests"
        command = [
            os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra",
            "-DRADIOLIB_STATIC_ONLY=1", "-DRADIOLIB_STATIC_ARRAY_SIZE=64",
            "-DFANLINK_RADIO_USE_DCDC=0", "-DFANLINK_RADIO_BUSY_TIMEOUT_MS=25UL",
            f"-I{root}", f"-I{firmware / 'tests/stubs'}",
            f"-I{firmware / 'include'}",
            f"-I{firmware / 'lib/fanlink_platform_esp8266/src'}",
            str(firmware / "tests/radiolib_recovery_tests.cpp"),
            *(str(root / name) for name in source_names), "-o", str(binary),
        ]
        subprocess.run(command, check=True)
        invocation = [str(binary)]
        if args.reproduce_tx_dio_high:
            invocation.append("--reproduce-tx-dio-high")
        result = subprocess.run(invocation, check=False, timeout=15)
        if result.returncode != 0:
            return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
