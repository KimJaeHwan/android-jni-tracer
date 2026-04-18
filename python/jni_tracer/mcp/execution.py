from __future__ import annotations

import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ..log import load_log, summary
from ..mock import validate_mock_data
from ..runner import adb_run
from ..store import read_json
from .safety import clamp_timeout, resolve_existing_file, validate_target_so


JSONDict = dict[str, Any]


@dataclass(frozen=True)
class ExecutionConfig:
    allow_execute: bool = False
    harness: str | None = None
    libs_dir: str | None = None
    allowed_so_dir: str | None = None
    device_dir: str = "/data/local/tmp/jni-tracer-run"
    timeout_sec: int = 30

    def require_enabled(self) -> None:
        if not self.allow_execute:
            raise PermissionError("execution tools are disabled; restart with --allow-execute")
        if not self.harness:
            raise ValueError("--harness is required when --allow-execute is used")
        if not self.libs_dir:
            raise ValueError("--libs-dir is required when --allow-execute is used")

    def checked_harness(self) -> Path:
        self.require_enabled()
        return resolve_existing_file(str(self.harness), "harness")

    def checked_so_name(self, so_name: object) -> str:
        self.require_enabled()
        allowed = self.allowed_so_dir or self.libs_dir
        if not allowed or not self.libs_dir:
            raise ValueError("libs_dir/allowed_so_dir are not configured")
        return validate_target_so(self.libs_dir, allowed, so_name)


def _write_json(path: Path, data: Any) -> None:
    import json

    with path.open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")


def _run_result(run_dir: Path) -> JSONDict:
    manifest = read_json(run_dir / "manifest.json")
    summary_path = run_dir / "summary.json"
    log_path = run_dir / "logs" / "jni_hook.json"
    log_summary: JSONDict = read_json(summary_path) if summary_path.exists() else {}
    if not log_summary and log_path.exists():
        log_summary = summary(load_log(log_path))
    return {
        "status": manifest.get("status"),
        "run_id": manifest.get("run_id"),
        "run_dir": str(run_dir),
        "returncode": manifest.get("returncode"),
        "duration_sec": manifest.get("duration_sec"),
        "summary": log_summary,
        "manifest": manifest,
        "next_tools": ["get_run", "get_summary", "get_calls", "diff_runs"],
    }


def run_harness_tool(config: ExecutionConfig, runs_root: str, args: JSONDict) -> JSONDict:
    so_name = config.checked_so_name(args.get("so_name"))
    timeout_sec = clamp_timeout(args.get("timeout_sec"), config.timeout_sec)
    label = args.get("label")
    if label is not None and not isinstance(label, str):
        raise ValueError("label must be a string")

    mock_config = args.get("mock_config")
    with tempfile.TemporaryDirectory(prefix="jni-tracer-mcp-") as tmp:
        mock_path: Path | None = None
        if mock_config is not None:
            errors = validate_mock_data(mock_config)
            if errors:
                return {"status": "invalid_mock", "errors": errors}
            mock_path = Path(tmp) / "mock.json"
            _write_json(mock_path, mock_config)

        run_dir = adb_run(
            harness=config.checked_harness(),
            libs_dir=str(config.libs_dir),
            so_name=so_name,
            runs_root=runs_root,
            label=label or "mcp_run",
            device_dir=config.device_dir,
            mock=mock_path,
            timeout_sec=timeout_sec,
            metadata={"mcp_tool": "run_harness", "timeout_sec": timeout_sec},
        )
    return _run_result(run_dir)


def run_invoke_plan_tool(config: ExecutionConfig, runs_root: str, args: JSONDict) -> JSONDict:
    so_name = config.checked_so_name(args.get("so_name"))
    timeout_sec = clamp_timeout(args.get("timeout_sec"), config.timeout_sec)
    label = args.get("label")
    if label is not None and not isinstance(label, str):
        raise ValueError("label must be a string")

    invoke_plan = args.get("invoke_plan")
    if not isinstance(invoke_plan, list):
        raise ValueError("invoke_plan must be an array")

    mock_config = args.get("mock_config")
    with tempfile.TemporaryDirectory(prefix="jni-tracer-mcp-") as tmp:
        tmp_dir = Path(tmp)
        plan_path = tmp_dir / "invoke_plan.json"
        _write_json(plan_path, invoke_plan)

        mock_path: Path | None = None
        if mock_config is not None:
            errors = validate_mock_data(mock_config)
            if errors:
                return {"status": "invalid_mock", "errors": errors}
            mock_path = tmp_dir / "mock.json"
            _write_json(mock_path, mock_config)

        run_dir = adb_run(
            harness=config.checked_harness(),
            libs_dir=str(config.libs_dir),
            so_name=so_name,
            runs_root=runs_root,
            label=label or "mcp_invoke_plan",
            device_dir=config.device_dir,
            mock=mock_path,
            invoke_plan=plan_path,
            timeout_sec=timeout_sec,
            metadata={
                "mcp_tool": "run_invoke_plan",
                "timeout_sec": timeout_sec,
                "invoke_plan": invoke_plan,
            },
        )
    return _run_result(run_dir)


def validate_mock_config_tool(args: JSONDict) -> JSONDict:
    mock_config = args.get("mock_config")
    errors = validate_mock_data(mock_config)
    return {"status": "invalid" if errors else "ok", "errors": errors}
