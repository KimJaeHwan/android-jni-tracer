from __future__ import annotations

import hashlib
import shutil
import subprocess
import time
from pathlib import Path
from typing import Any

from .log import load_log, summary
from .store import ensure_run_dir, make_run_id, write_json


def run_cmd(cmd: list[str], check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, check=check, text=True, capture_output=True)


def sha256_file(path: str | Path) -> str:
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def adb_run(
    harness: str | Path,
    libs_dir: str | Path,
    so_name: str,
    runs_root: str | Path,
    label: str | None = None,
    device_dir: str = "/data/local/tmp/jni-tracer-run",
    mock: str | Path | None = None,
    invoke: str | None = None,
    args: list[str] | None = None,
    invoke_plan: str | Path | None = None,
) -> Path:
    harness = Path(harness)
    libs_dir = Path(libs_dir)
    if not harness.exists():
        raise FileNotFoundError(f"harness not found: {harness}")
    if not libs_dir.is_dir():
        raise FileNotFoundError(f"libs dir not found: {libs_dir}")
    if not (libs_dir / so_name).exists():
        raise FileNotFoundError(f"target so not found: {libs_dir / so_name}")

    run_id = make_run_id(label)
    run_dir = ensure_run_dir(runs_root, run_id)

    started = time.time()
    device_logs = f"{device_dir}/logs"

    run_cmd(["adb", "shell", f"rm -rf {device_dir} && mkdir -p {device_logs}"])
    run_cmd(["adb", "push", str(harness), f"{device_dir}/jni_harness_arm64_android"])
    run_cmd(["adb", "push", f"{libs_dir}/.", f"{device_dir}/"])

    remote_mock = None
    if mock:
        remote_mock = f"{device_dir}/mock.json"
        run_cmd(["adb", "push", str(mock), remote_mock])

    remote_plan = None
    if invoke_plan:
        remote_plan = f"{device_dir}/invoke_plan.json"
        run_cmd(["adb", "push", str(invoke_plan), remote_plan])

    shell_parts = [
        f"cd {device_dir}",
        "chmod +x jni_harness_arm64_android",
        "rm -rf logs",
        "mkdir logs",
        "LD_LIBRARY_PATH=. ./jni_harness_arm64_android",
    ]
    if remote_mock:
        shell_parts.append(f"--mock {Path(remote_mock).name}")
    shell_parts.append(f"./{so_name}")
    if invoke:
        shell_parts.append(f"--invoke '{invoke}'")
        for arg in args or []:
            shell_parts.append(f"--arg '{arg}'")
    if remote_plan:
        shell_parts.append(f"--invoke-plan {Path(remote_plan).name}")

    command = " && ".join(shell_parts[:4]) + " && " + " ".join(shell_parts[4:])
    proc = run_cmd(["adb", "shell", command], check=False)

    pull_dir = run_dir / "logs"
    pull_dir.mkdir(parents=True, exist_ok=True)
    run_cmd(["adb", "pull", f"{device_logs}/.", str(pull_dir)], check=False)

    log_json = pull_dir / "jni_hook.json"
    log_summary: dict[str, Any] = {}
    if log_json.exists():
        log_summary = summary(load_log(log_json))
        write_json(run_dir / "summary.json", log_summary)

    manifest = {
        "run_id": run_id,
        "label": label,
        "status": "ok" if proc.returncode == 0 else "failed",
        "returncode": proc.returncode,
        "started_at": int(started),
        "duration_sec": round(time.time() - started, 3),
        "mode": "android-adb",
        "device_dir": device_dir,
        "harness": str(harness),
        "harness_sha256": sha256_file(harness),
        "libs_dir": str(libs_dir),
        "so_name": so_name,
        "mock": str(mock) if mock else None,
        "invoke": invoke,
        "args": args or [],
        "invoke_plan": str(invoke_plan) if invoke_plan else None,
        "shell_command": command,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
        "log_json_path": str(log_json) if log_json.exists() else None,
        "summary_path": str(run_dir / "summary.json") if log_summary else None,
    }
    write_json(run_dir / "manifest.json", manifest)
    if invoke_plan:
        shutil.copy2(invoke_plan, run_dir / "invoke_plan.json")
    if mock:
        shutil.copy2(mock, run_dir / "mock.json")

    return run_dir
