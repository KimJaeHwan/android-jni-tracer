from __future__ import annotations

import json
from pathlib import Path
from typing import Any


VALID_SECTIONS = {
    "bool_returns": bool,
    "int_returns": int,
    "string_returns": str,
}


def template() -> dict[str, Any]:
    return {
        "bool_returns": [
            {"class": "com/security/RootChecker", "method": "isRooted", "sig": "()Z", "return": False}
        ],
        "int_returns": [
            {"class": "android/os/Build$VERSION", "method": "getSdkInt", "sig": "()I", "return": 34}
        ],
        "string_returns": [
            {"class": "android/os/Build", "method": "getModel", "sig": "()Ljava/lang/String;", "return": "Pixel 8"}
        ],
    }


def validate_mock(path: str | Path) -> list[str]:
    with Path(path).open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        return ["top-level JSON value must be an object"]

    errors: list[str] = []
    for section, return_type in VALID_SECTIONS.items():
        value = data.get(section, [])
        if not isinstance(value, list):
            errors.append(f"{section} must be an array")
            continue
        for i, item in enumerate(value):
            if not isinstance(item, dict):
                errors.append(f"{section}[{i}] must be an object")
                continue
            for key in ("class", "method", "sig", "return"):
                if key not in item:
                    errors.append(f"{section}[{i}] missing {key}")
            for key in ("class", "method", "sig"):
                if key in item and not isinstance(item[key], str):
                    errors.append(f"{section}[{i}].{key} must be a string")
            if "return" in item and not isinstance(item["return"], return_type):
                errors.append(f"{section}[{i}].return must be {return_type.__name__}")
    return errors
