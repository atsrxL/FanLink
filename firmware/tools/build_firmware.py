#!/usr/bin/env python3
"""Clean-build selected PlatformIO environments and publish evidenced binaries."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import uuid
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = ROOT.parent
DIST = ROOT / "dist"


def environment_for_rx(node: int) -> str:
    return f"fanlink_rx_node{node:02d}"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def platformio_command() -> list[str]:
    executable = shutil.which("pio") or shutil.which("platformio")
    if executable:
        return [executable]
    if importlib.util.find_spec("platformio") is not None:
        return [sys.executable, "-m", "platformio"]
    raise RuntimeError(
        "PlatformIO CLI not found; install platformio==6.1.19 or run with "
        "`uv run --with platformio==6.1.19 python3 tools/build_firmware.py ...`"
    )


def run_logged(command: list[str], log) -> None:
    rendered = shlex.join(command)
    print(f"$ {rendered}")
    log.write(f"$ {rendered}\n")
    log.flush()
    process = subprocess.Popen(
        command,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
    )
    if process.stdout is None:
        process.kill()
        raise RuntimeError("failed to capture PlatformIO output")
    for line in process.stdout:
        print(line, end="")
        log.write(line)
    return_code = process.wait()
    log.write(f"\nexit_code={return_code}\n")
    log.flush()
    if return_code != 0:
        raise subprocess.CalledProcessError(return_code, command)


def target_for_environment(name: str) -> str:
    if name == "fanlink_tx_esp8285":
        return "JHEMCU 2.4GHz Lite RX hardware, repurposed as FanLink TX"
    return "BETAFPV 2.4GHz Nano RX"


def build_environment(pio: list[str], name: str, staging: Path) -> dict[str, object]:
    log_path = staging / f"{name}.build.log"
    with log_path.open("w", encoding="utf-8") as log:
        # A pre-build hook validates and patches the pinned RadioLib sources.
        # Install dependencies explicitly so a first-ever `-t clean` does not
        # run that hook before `.pio/libdeps/<environment>` exists.
        run_logged([*pio, "pkg", "install", "-e", name], log)
        run_logged([*pio, "run", "-e", name, "-t", "clean"], log)
        run_logged([*pio, "run", "-e", name], log)

    source = ROOT / ".pio" / "build" / name / "firmware.bin"
    if not source.is_file():
        raise RuntimeError(f"build succeeded but {source} was not produced")

    destination = staging / f"{name}.bin"
    shutil.copy2(source, destination)
    digest = sha256(destination)
    (staging / f"{name}.bin.sha256").write_text(
        f"{digest}  {destination.name}\n", encoding="utf-8"
    )
    return {
        "environment": name,
        "target": target_for_environment(name),
        "file": destination.name,
        "bytes": destination.stat().st_size,
        "sha256": digest,
        "build_log": log_path.name,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tx", action="store_true", help="build the TX image")
    parser.add_argument(
        "--rx",
        type=int,
        action="append",
        choices=range(1, 17),
        metavar="NODE",
        help="build one RX node; may be repeated",
    )
    parser.add_argument("--all-rx", action="store_true", help="build RX nodes 1..16")
    parser.add_argument(
        "--run-id",
        help=(
            "optional stable output directory name under dist/; must contain only "
            "letters, digits, dot, underscore, or dash"
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.run_id and re.fullmatch(r"[A-Za-z0-9._-]+", args.run_id) is None:
        print("error: invalid --run-id", file=sys.stderr)
        return 2
    run_id = args.run_id or (
        datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        + "-"
        + uuid.uuid4().hex[:8]
    )

    environments: list[str] = []
    if args.tx:
        environments.append("fanlink_tx_esp8285")
    nodes = list(range(1, 17)) if args.all_rx else (args.rx or [])
    environments.extend(environment_for_rx(node) for node in nodes)
    environments = list(dict.fromkeys(environments))
    if not environments:
        print("Nothing selected; use --tx, --rx N, or --all-rx", file=sys.stderr)
        return 2

    try:
        subprocess.run(
            ["bash", str(REPO_ROOT / "scripts" / "source_manifest.sh"), "--check"],
            cwd=REPO_ROOT,
            check=True,
        )
        pio = platformio_command()
        version = subprocess.run(
            [*pio, "--version"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    DIST.mkdir(exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".staging-", dir=DIST))
    published = DIST / run_id

    if published.exists():
        staging.rmdir()
        print(f"error: output already exists: {published.relative_to(ROOT)}", file=sys.stderr)
        return 1

    try:
        artifacts = [build_environment(pio, name, staging) for name in environments]
        build_info = {
            "run_id": run_id,
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "clean_build": True,
            "platformio_core": version,
            "platformio_platform": "espressif8266@4.2.1",
            "framework": "Arduino ESP8266 Core 3.1.2",
            "radiolib": "7.7.1",
            "radiolib_integration_patch_sha256": sha256(
                ROOT / "scripts" / "platformio_patch_radiolib.py"
            ),
            "source_manifest_sha256": sha256(REPO_ROOT / "SOURCE_MANIFEST.sha256"),
            "hardware_verified": False,
            "artifacts": artifacts,
        }
        (staging / "BUILD_INFO.json").write_text(
            json.dumps(build_info, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )

        checksum_files = sorted(
            path for path in staging.iterdir() if path.is_file() and path.suffix != ".sha256"
        )
        (staging / "SHA256SUMS").write_text(
            "".join(f"{sha256(path)}  {path.name}\n" for path in checksum_files),
            encoding="utf-8",
        )
        staging.rename(published)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        failed = DIST / f"failed-{run_id}"
        if staging.exists():
            staging.rename(failed)
            print(f"failure logs retained: {failed.relative_to(ROOT)}", file=sys.stderr)
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"published: {published.relative_to(ROOT)}")
    for artifact in artifacts:
        print(
            f"built: {published.relative_to(ROOT) / str(artifact['file'])} "
            f"sha256={artifact['sha256']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
