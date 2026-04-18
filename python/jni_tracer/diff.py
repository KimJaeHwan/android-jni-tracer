from __future__ import annotations

from collections import Counter
from typing import Any

from .log import function_counts, registered_natives


def diff_logs(a: dict[str, Any], b: dict[str, Any]) -> dict[str, Any]:
    ca = function_counts(a)
    cb = function_counts(b)
    functions = sorted(set(ca) | set(cb))

    native_a = {
        (item.get("class_name"), m.get("name"), m.get("signature"))
        for item in registered_natives(a)
        for m in item.get("methods", [])
        if isinstance(m, dict)
    }
    native_b = {
        (item.get("class_name"), m.get("name"), m.get("signature"))
        for item in registered_natives(b)
        for m in item.get("methods", [])
        if isinstance(m, dict)
    }

    return {
        "total_calls": {
            "a": a.get("total_calls", len(a.get("calls", []))),
            "b": b.get("total_calls", len(b.get("calls", []))),
            "delta": b.get("total_calls", len(b.get("calls", []))) - a.get("total_calls", len(a.get("calls", []))),
        },
        "function_counts": [
            {"function": fn, "a": ca.get(fn, 0), "b": cb.get(fn, 0), "delta": cb.get(fn, 0) - ca.get(fn, 0)}
            for fn in functions
            if ca.get(fn, 0) != cb.get(fn, 0)
        ],
        "registered_natives_added": [
            {"class_name": c, "name": n, "signature": s} for c, n, s in sorted(native_b - native_a)
        ],
        "registered_natives_removed": [
            {"class_name": c, "name": n, "signature": s} for c, n, s in sorted(native_a - native_b)
        ],
    }
