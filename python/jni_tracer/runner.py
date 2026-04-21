from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import time
from pathlib import Path
from typing import Any

from .log import load_log, load_ndjson_log, summary
from .store import ensure_run_dir, make_run_id, write_json


def run_cmd(
    cmd: list[str],
    check: bool = True,
    timeout: int | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, check=check, text=True, capture_output=True, timeout=timeout)


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
    timeout_sec: int | None = None,
    metadata: dict[str, Any] | None = None,
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
    shell_parts.append(f"./{so_name}")
    if remote_mock:
        shell_parts.append(f"--mock {Path(remote_mock).name}")
    if invoke:
        shell_parts.append(f"--invoke '{invoke}'")
        for arg in args or []:
            shell_parts.append(f"--arg '{arg}'")
    if remote_plan:
        shell_parts.append(f"--invoke-plan {Path(remote_plan).name}")

    command = " && ".join(shell_parts[:4]) + " && " + " ".join(shell_parts[4:])
    try:
        proc = run_cmd(["adb", "shell", command], check=False, timeout=timeout_sec)
    except subprocess.TimeoutExpired as exc:
        proc = subprocess.CompletedProcess(
            exc.cmd,
            124,
            stdout=exc.stdout or "",
            stderr=(exc.stderr or "") + f"\ntimeout after {timeout_sec} seconds",
        )

    pull_dir = run_dir / "logs"
    pull_dir.mkdir(parents=True, exist_ok=True)
    run_cmd(["adb", "pull", f"{device_logs}/.", str(pull_dir)], check=False)

    log_json = pull_dir / "jni_hook.json"
    log_ndjson = pull_dir / "jni_hook.ndjson"
    log_summary: dict[str, Any] = {}
    log_parse_error: str | None = None
    summary_source: str | None = None
    if log_json.exists():
        try:
            log_summary = summary(load_log(log_json))
            write_json(run_dir / "summary.json", log_summary)
            summary_source = "jni_hook.json"
        except (json.JSONDecodeError, ValueError) as exc:
            log_parse_error = f"{type(exc).__name__}: {exc}"
            if log_ndjson.exists():
                try:
                    recovered = load_ndjson_log(log_ndjson)
                    log_summary = summary(recovered)
                    log_summary["recovered_from"] = "jni_hook.ndjson"
                    log_summary["recovery_reason"] = log_parse_error
                    write_json(run_dir / "summary.json", log_summary)
                    summary_source = "jni_hook.ndjson"
                except (json.JSONDecodeError, ValueError) as ndjson_exc:
                    log_parse_error = (
                        f"{log_parse_error}; NDJSON fallback failed: "
                        f"{type(ndjson_exc).__name__}: {ndjson_exc}"
                    )

    archived_mock = run_dir / "mock.json" if mock else None
    archived_invoke_plan = run_dir / "invoke_plan.json" if invoke_plan else None
    status = "ok" if proc.returncode == 0 and log_parse_error is None else "failed"
    termination_kind = None
    if proc.returncode == 124:
        termination_kind = "timeout"
    elif proc.returncode >= 128:
        termination_kind = f"signal_{proc.returncode - 128}"
    elif proc.returncode != 0:
        termination_kind = "nonzero_exit"
    manifest = {
        "run_id": run_id,
        "label": label,
        "status": status,
        "returncode": proc.returncode,
        "termination_kind": termination_kind,
        "started_at": int(started),
        "duration_sec": round(time.time() - started, 3),
        "mode": "android-adb",
        "device_dir": device_dir,
        "harness": str(harness),
        "harness_sha256": sha256_file(harness),
        "libs_dir": str(libs_dir),
        "so_name": so_name,
        "mock": str(archived_mock) if archived_mock else None,
        "input_mock": str(mock) if mock else None,
        "invoke": invoke,
        "args": args or [],
        "invoke_plan": str(archived_invoke_plan) if archived_invoke_plan else None,
        "input_invoke_plan": str(invoke_plan) if invoke_plan else None,
        "timeout_sec": timeout_sec,
        "metadata": metadata or {},
        "shell_command": command,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
        "log_json_path": str(log_json) if log_json.exists() else None,
        "log_ndjson_path": str(log_ndjson) if log_ndjson.exists() else None,
        "summary_path": str(run_dir / "summary.json") if log_summary else None,
        "summary_source": summary_source,
        "log_parse_error": log_parse_error,
        "log_json_size": log_json.stat().st_size if log_json.exists() else None,
        "log_ndjson_size": log_ndjson.stat().st_size if log_ndjson.exists() else None,
        "partial_log_available": log_json.exists() or log_ndjson.exists(),
    }
    write_json(run_dir / "manifest.json", manifest)
    if invoke_plan and archived_invoke_plan:
        shutil.copy2(invoke_plan, archived_invoke_plan)
    if mock and archived_mock:
        shutil.copy2(mock, archived_mock)

    return run_dir
