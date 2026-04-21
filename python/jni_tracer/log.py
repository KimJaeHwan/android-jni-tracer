from __future__ import annotations

import json
from collections import Counter
from pathlib import Path
from typing import Any


def load_log(path: str | Path) -> dict[str, Any]:
    with Path(path).open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError("top-level JSON value must be an object")
    calls = data.get("calls")
    if not isinstance(calls, list):
        raise ValueError("missing or invalid calls array")
    return data


def load_ndjson_log(path: str | Path) -> dict[str, Any]:
    calls: list[dict[str, Any]] = []
    with Path(path).open("r", encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            entry = json.loads(line)
            if not isinstance(entry, dict):
                raise ValueError(f"ndjson line {lineno} is not an object")
            calls.append(entry)
    return {
        "log_version": "1.0-ndjson",
        "calls": calls,
        "total_calls": len(calls),
        "recovered_from": str(path),
    }


def validate_log(path: str | Path) -> list[str]:
    data = load_log(path)
    errors: list[str] = []
    calls = data["calls"]
    total = data.get("total_calls")
    if total is not None and total != len(calls):
        errors.append(f"total_calls={total} but len(calls)={len(calls)}")
    required = {"index", "function", "arguments"}
    for i, call in enumerate(calls):
        if not isinstance(call, dict):
            errors.append(f"calls[{i}] is not an object")
            continue
        missing = required - set(call)
        if missing:
            errors.append(f"calls[{i}] missing: {', '.join(sorted(missing))}")
        if not isinstance(call.get("arguments", {}), dict):
            errors.append(f"calls[{i}].arguments is not an object")
    return errors


def function_counts(data: dict[str, Any]) -> Counter[str]:
    return Counter(str(c.get("function", "")) for c in data.get("calls", []))


def classes(data: dict[str, Any]) -> list[str]:
    found: set[str] = set()
    for call in data.get("calls", []):
        args = call.get("arguments", {})
        if not isinstance(args, dict):
            continue
        for key in ("class_name", "name"):
            value = args.get(key)
            if isinstance(value, str) and "/" in value and value not in ("Unknown", "NULL"):
                found.add(value)
    return sorted(found)


def registered_natives(data: dict[str, Any]) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for call in data.get("calls", []):
        if call.get("function") != "RegisterNatives":
            continue
        args = call.get("arguments", {})
        if not isinstance(args, dict):
            continue
        out.append(
            {
                "index": call.get("index"),
                "caller_module": call.get("caller_module"),
                "caller_offset": call.get("caller_offset"),
                "class_name": args.get("class_name"),
                "nMethods": args.get("nMethods"),
                "methods": args.get("methods", []),
            }
        )
    return out


def filter_calls(
    data: dict[str, Any],
    function: str | None = None,
    class_name: str | None = None,
    method_name: str | None = None,
) -> list[dict[str, Any]]:
    out = []
    for call in data.get("calls", []):
        if function and call.get("function") != function:
            continue
        args = call.get("arguments", {})
        if not isinstance(args, dict):
            continue
        if class_name and args.get("class_name") != class_name:
            continue
        if method_name and args.get("method_name") != method_name and args.get("name") != method_name:
            continue
        out.append(call)
    return out


def summary(data: dict[str, Any]) -> dict[str, Any]:
    counts = function_counts(data)
    natives = registered_natives(data)
    invoke_results = filter_calls(data, function="InvokeNative")
    return {
        "total_calls": data.get("total_calls", len(data.get("calls", []))),
        "call_entries": len(data.get("calls", [])),
        "top_functions": [{"function": k, "count": v} for k, v in counts.most_common()],
        "classes": classes(data),
        "register_natives": [
            {
                "class_name": item.get("class_name"),
                "nMethods": item.get("nMethods"),
                "caller_offset": item.get("caller_offset"),
            }
            for item in natives
        ],
        "invoke_results": [
            {
                "class_name": c.get("arguments", {}).get("class_name"),
                "method_name": c.get("arguments", {}).get("method_name"),
                "sig": c.get("arguments", {}).get("sig"),
                "status": c.get("arguments", {}).get("status"),
                "sequence_step": c.get("arguments", {}).get("sequence_step"),
            }
            for c in invoke_results
        ],
    }
