from __future__ import annotations

import json
import sys
from typing import Any

from ..diff import diff_logs
from ..log import filter_calls, load_log, registered_natives, summary
from ..store import list_runs, run_log_path, run_manifest, run_summary
from .execution import ExecutionConfig, run_harness_tool, run_invoke_plan_tool, validate_mock_config_tool


JSONDict = dict[str, Any]


def content_response(data: Any) -> JSONDict:
    return {
        "content": [
            {
                "type": "text",
                "text": json.dumps(data, indent=2, ensure_ascii=False),
            }
        ]
    }


def tool_schema(properties: JSONDict, required: list[str] | None = None) -> JSONDict:
    return {
        "type": "object",
        "properties": properties,
        "required": required or [],
        "additionalProperties": False,
    }


def readonly_tools() -> list[JSONDict]:
    run_id = {"type": "string", "description": "Run id under the run store"}
    runs_root = {"type": "string", "description": "Run store root", "default": "runs"}
    return [
        {
            "name": "list_runs",
            "description": "List available jni-tracer runs.",
            "inputSchema": tool_schema({"runs_root": runs_root}),
        },
        {
            "name": "get_run",
            "description": "Get a run manifest.",
            "inputSchema": tool_schema({"run_id": run_id, "runs_root": runs_root}, ["run_id"]),
        },
        {
            "name": "get_summary",
            "description": "Get a run summary.",
            "inputSchema": tool_schema({"run_id": run_id, "runs_root": runs_root}, ["run_id"]),
        },
        {
            "name": "get_calls",
            "description": "Get calls from a run, optionally filtered.",
            "inputSchema": tool_schema(
                {
                    "run_id": run_id,
                    "runs_root": runs_root,
                    "function": {"type": "string"},
                    "class_name": {"type": "string"},
                    "method_name": {"type": "string"},
                },
                ["run_id"],
            ),
        },
        {
            "name": "get_natives",
            "description": "Get RegisterNatives entries for a run.",
            "inputSchema": tool_schema({"run_id": run_id, "runs_root": runs_root}, ["run_id"]),
        },
        {
            "name": "get_classes",
            "description": "Get class names observed in a run.",
            "inputSchema": tool_schema({"run_id": run_id, "runs_root": runs_root}, ["run_id"]),
        },
        {
            "name": "diff_runs",
            "description": "Diff two runs by function counts and registered natives.",
            "inputSchema": tool_schema(
                {
                    "run_a": {"type": "string"},
                    "run_b": {"type": "string"},
                    "runs_root": runs_root,
                },
                ["run_a", "run_b"],
            ),
        },
    ]


def execution_tools(config: ExecutionConfig) -> list[JSONDict]:
    if not config.allow_execute:
        return [
            {
                "name": "validate_mock_config",
                "description": "Validate an inline mock config without running the harness.",
                "inputSchema": tool_schema({"mock_config": {"type": "object"}}, ["mock_config"]),
            }
        ]

    mock_config = {"type": "object", "description": "Inline mock config"}
    so_name = {"type": "string", "description": "SO filename inside configured --libs-dir"}
    label = {"type": "string", "description": "Optional run label"}
    timeout_sec = {"type": "integer", "description": "Per-run timeout in seconds"}
    invoke_plan = {
        "type": "array",
        "description": "Invoke plan entries accepted by the native harness",
        "items": {"type": "object"},
    }
    return [
        {
            "name": "validate_mock_config",
            "description": "Validate an inline mock config without running the harness.",
            "inputSchema": tool_schema({"mock_config": mock_config}, ["mock_config"]),
        },
        {
            "name": "run_harness",
            "description": "Run the configured Android harness for JNI_OnLoad observation.",
            "inputSchema": tool_schema(
                {
                    "so_name": so_name,
                    "label": label,
                    "mock_config": mock_config,
                    "timeout_sec": timeout_sec,
                },
                ["so_name"],
            ),
        },
        {
            "name": "run_invoke_plan",
            "description": "Run the configured Android harness with an inline invoke plan.",
            "inputSchema": tool_schema(
                {
                    "so_name": so_name,
                    "label": label,
                    "invoke_plan": invoke_plan,
                    "mock_config": mock_config,
                    "timeout_sec": timeout_sec,
                },
                ["so_name", "invoke_plan"],
            ),
        },
    ]


def tools(config: ExecutionConfig) -> list[JSONDict]:
    return readonly_tools() + execution_tools(config)


def arg_str(args: JSONDict, key: str, default: str | None = None) -> str | None:
    value = args.get(key, default)
    return value if isinstance(value, str) else default


def dispatch_tool(default_runs_root: str, execution_config: ExecutionConfig, name: str, args: JSONDict) -> JSONDict:
    runs_root = arg_str(args, "runs_root", default_runs_root) or default_runs_root
    if name == "list_runs":
        return content_response(list_runs(runs_root))
    if name == "get_run":
        return content_response(run_manifest(runs_root, str(args["run_id"])))
    if name == "get_summary":
        return content_response(run_summary(runs_root, str(args["run_id"])))
    if name == "get_calls":
        data = load_log(run_log_path(runs_root, str(args["run_id"])))
        return content_response(
            filter_calls(
                data,
                function=arg_str(args, "function"),
                class_name=arg_str(args, "class_name"),
                method_name=arg_str(args, "method_name"),
            )
        )
    if name == "get_natives":
        return content_response(registered_natives(load_log(run_log_path(runs_root, str(args["run_id"])))))
    if name == "get_classes":
        return content_response(summary(load_log(run_log_path(runs_root, str(args["run_id"]))))["classes"])
    if name == "diff_runs":
        a = load_log(run_log_path(runs_root, str(args["run_a"])))
        b = load_log(run_log_path(runs_root, str(args["run_b"])))
        return content_response(diff_logs(a, b))
    if name == "validate_mock_config":
        return content_response(validate_mock_config_tool(args))
    if name == "run_harness":
        return content_response(run_harness_tool(execution_config, runs_root, args))
    if name == "run_invoke_plan":
        return content_response(run_invoke_plan_tool(execution_config, runs_root, args))
    raise ValueError(f"unknown tool: {name}")


def read_message() -> JSONDict | None:
    headers: dict[str, str] = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        key, _, value = line.decode("ascii").partition(":")
        headers[key.lower()] = value.strip()

    length = int(headers.get("content-length", "0"))
    if length <= 0:
        return None
    body = sys.stdin.buffer.read(length)
    return json.loads(body.decode("utf-8"))


def write_message(message: JSONDict) -> None:
    body = json.dumps(message, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    sys.stdout.buffer.write(f"Content-Length: {len(body)}\r\n\r\n".encode("ascii"))
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


def response(msg_id: Any, result: Any) -> JSONDict:
    return {"jsonrpc": "2.0", "id": msg_id, "result": result}


def error_response(msg_id: Any, code: int, message: str) -> JSONDict:
    return {"jsonrpc": "2.0", "id": msg_id, "error": {"code": code, "message": message}}


def handle(default_runs_root: str, execution_config: ExecutionConfig, message: JSONDict) -> JSONDict | None:
    method = message.get("method")
    msg_id = message.get("id")
    params = message.get("params") or {}

    if method == "initialize":
        return response(
            msg_id,
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "jni-tracer", "version": "0.1.0"},
            },
        )
    if method == "notifications/initialized":
        return None
    if method == "tools/list":
        return response(msg_id, {"tools": tools(execution_config)})
    if method == "tools/call":
        name = params.get("name")
        args = params.get("arguments") or {}
        if not isinstance(name, str) or not isinstance(args, dict):
            return error_response(msg_id, -32602, "invalid tools/call params")
        try:
            return response(msg_id, dispatch_tool(default_runs_root, execution_config, name, args))
        except Exception as exc:
            return error_response(msg_id, -32000, str(exc))
    if msg_id is None:
        return None
    return error_response(msg_id, -32601, f"method not found: {method}")


def serve(runs_root: str = "runs", execution_config: ExecutionConfig | None = None) -> None:
    config = execution_config or ExecutionConfig()
    while True:
        message = read_message()
        if message is None:
            break
        reply = handle(runs_root, config, message)
        if reply is not None:
            write_message(reply)
