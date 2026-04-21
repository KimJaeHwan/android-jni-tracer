from __future__ import annotations

import argparse
import json
import subprocess
import sys
from typing import Any

from .diff import diff_logs
from .log import filter_calls, load_log, registered_natives, summary, validate_log
from .mock import template as mock_template
from .mock import validate_mock
from .mcp.execution import ExecutionConfig
from .runner import adb_run
from .store import list_runs, read_json, run_log_path, run_manifest, run_summary


def print_json(data: Any) -> None:
    print(json.dumps(data, indent=2, ensure_ascii=False))


def cmd_log(args: argparse.Namespace) -> int:
    if args.log_cmd == "validate":
        errors = validate_log(args.path)
        if errors:
            print_json({"status": "invalid", "errors": errors})
            return 1
        print_json({"status": "ok", "path": args.path})
        return 0

    data = load_log(args.path)
    if args.log_cmd == "summary":
        print_json(summary(data))
        return 0
    if args.log_cmd == "natives":
        print_json(registered_natives(data))
        return 0
    if args.log_cmd == "classes":
        print_json(summary(data)["classes"])
        return 0
    if args.log_cmd == "calls":
        print_json(
            filter_calls(
                data,
                function=args.function,
                class_name=args.class_name,
                method_name=args.method_name,
            )
        )
        return 0
    raise SystemExit(f"unknown log command: {args.log_cmd}")


def cmd_run(args: argparse.Namespace) -> int:
    run_dir = adb_run(
        harness=args.harness,
        libs_dir=args.libs_dir,
        so_name=args.so,
        runs_root=args.runs_root,
        label=args.label,
        device_dir=args.device_dir,
        mock=args.mock,
        invoke=args.invoke,
        args=args.arg,
        invoke_plan=args.invoke_plan,
        timeout_sec=args.timeout_sec,
    )
    manifest_path = run_dir / "manifest.json"
    manifest = read_json(manifest_path)
    print_json(
        {
            "status": manifest.get("status", "unknown"),
            "run_dir": str(run_dir),
            "manifest": str(manifest_path),
            "returncode": manifest.get("returncode"),
            "termination_kind": manifest.get("termination_kind"),
            "summary_source": manifest.get("summary_source"),
            "log_parse_error": manifest.get("log_parse_error"),
            "partial_log_available": manifest.get("partial_log_available"),
        }
    )
    return 0 if manifest.get("status") == "ok" else 1


def cmd_runs(args: argparse.Namespace) -> int:
    if args.runs_cmd == "list":
        print_json(list_runs(args.runs_root))
        return 0
    if args.runs_cmd == "show":
        print_json(run_manifest(args.runs_root, args.run_id))
        return 0
    if args.runs_cmd == "summary":
        print_json(run_summary(args.runs_root, args.run_id))
        return 0
    if args.runs_cmd == "calls":
        data = load_log(run_log_path(args.runs_root, args.run_id))
        print_json(
            filter_calls(
                data,
                function=args.function,
                class_name=args.class_name,
                method_name=args.method_name,
            )
        )
        return 0
    if args.runs_cmd == "natives":
        print_json(registered_natives(load_log(run_log_path(args.runs_root, args.run_id))))
        return 0
    if args.runs_cmd == "classes":
        print_json(summary(load_log(run_log_path(args.runs_root, args.run_id)))["classes"])
        return 0
    if args.runs_cmd == "diff":
        a = load_log(run_log_path(args.runs_root, args.run_a))
        b = load_log(run_log_path(args.runs_root, args.run_b))
        print_json(diff_logs(a, b))
        return 0
    raise SystemExit(f"unknown runs command: {args.runs_cmd}")


def cmd_mock(args: argparse.Namespace) -> int:
    if args.mock_cmd == "template":
        print_json(mock_template())
        return 0
    if args.mock_cmd == "validate":
        errors = validate_mock(args.path)
        if errors:
            print_json({"status": "invalid", "errors": errors})
            return 1
        print_json({"status": "ok", "path": args.path})
        return 0
    raise SystemExit(f"unknown mock command: {args.mock_cmd}")


def cmd_diff(args: argparse.Namespace) -> int:
    print_json(diff_logs(load_log(args.a), load_log(args.b)))
    return 0


def cmd_mcp(args: argparse.Namespace) -> int:
    from .mcp.server import serve as mcp_serve

    execution_config = ExecutionConfig(
        allow_execute=args.allow_execute,
        harness=args.harness,
        libs_dir=args.libs_dir,
        allowed_so_dir=args.allowed_so_dir,
        device_dir=args.device_dir,
        timeout_sec=args.timeout_sec,
    )
    mcp_serve(args.runs_root, execution_config)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="jni-tracer")
    sub = parser.add_subparsers(dest="cmd", required=True)

    log = sub.add_parser("log", help="Inspect jni_hook.json files")
    log_sub = log.add_subparsers(dest="log_cmd", required=True)
    for name in ("validate", "summary", "natives", "classes"):
        p = log_sub.add_parser(name)
        p.add_argument("path")
        p.set_defaults(func=cmd_log)
    calls = log_sub.add_parser("calls")
    calls.add_argument("path")
    calls.add_argument("--function")
    calls.add_argument("--class-name")
    calls.add_argument("--method-name")
    calls.set_defaults(func=cmd_log)

    run = sub.add_parser("run", help="Run harness on an Android device via adb")
    run.add_argument("--harness", required=True)
    run.add_argument("--libs-dir", required=True)
    run.add_argument("--so", required=True, help="SO filename inside --libs-dir")
    run.add_argument("--runs-root", default="runs")
    run.add_argument("--label")
    run.add_argument("--device-dir", default="/data/local/tmp/jni-tracer-run")
    run.add_argument("--mock")
    run.add_argument("--invoke")
    run.add_argument("--arg", action="append", default=[])
    run.add_argument("--invoke-plan")
    run.add_argument("--timeout-sec", type=int)
    run.set_defaults(func=cmd_run)

    runs = sub.add_parser("runs", help="Inspect run store")
    runs_sub = runs.add_subparsers(dest="runs_cmd", required=True)
    for name in ("list",):
        p = runs_sub.add_parser(name)
        p.add_argument("--runs-root", default="runs")
        p.set_defaults(func=cmd_runs)
    for name in ("show", "summary"):
        p = runs_sub.add_parser(name)
        p.add_argument("run_id")
        p.add_argument("--runs-root", default="runs")
        p.set_defaults(func=cmd_runs)
    p = runs_sub.add_parser("calls")
    p.add_argument("run_id")
    p.add_argument("--runs-root", default="runs")
    p.add_argument("--function")
    p.add_argument("--class-name")
    p.add_argument("--method-name")
    p.set_defaults(func=cmd_runs)
    for name in ("natives", "classes"):
        p = runs_sub.add_parser(name)
        p.add_argument("run_id")
        p.add_argument("--runs-root", default="runs")
        p.set_defaults(func=cmd_runs)
    p = runs_sub.add_parser("diff")
    p.add_argument("run_a")
    p.add_argument("run_b")
    p.add_argument("--runs-root", default="runs")
    p.set_defaults(func=cmd_runs)

    mock = sub.add_parser("mock", help="Create and validate mock config files")
    mock_sub = mock.add_subparsers(dest="mock_cmd", required=True)
    p = mock_sub.add_parser("template")
    p.set_defaults(func=cmd_mock)
    p = mock_sub.add_parser("validate")
    p.add_argument("path")
    p.set_defaults(func=cmd_mock)

    diff = sub.add_parser("diff", help="Diff two jni_hook.json files")
    diff.add_argument("a")
    diff.add_argument("b")
    diff.set_defaults(func=cmd_diff)

    mcp = sub.add_parser("mcp", help="Run MCP server")
    mcp_sub = mcp.add_subparsers(dest="mcp_cmd", required=True)
    serve = mcp_sub.add_parser("serve")
    serve.add_argument("--runs-root", default="runs")
    serve.add_argument("--allow-execute", action="store_true")
    serve.add_argument("--harness")
    serve.add_argument("--libs-dir")
    serve.add_argument("--allowed-so-dir")
    serve.add_argument("--device-dir", default="/data/local/tmp/jni-tracer-run")
    serve.add_argument("--timeout-sec", type=int, default=30)
    serve.set_defaults(func=cmd_mcp)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except subprocess.CalledProcessError as e:  # type: ignore[name-defined]
        print(e.stderr or str(e), file=sys.stderr)
        return e.returncode
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
