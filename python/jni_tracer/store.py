from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any


def make_run_id(label: str | None = None) -> str:
    stamp = time.strftime("%Y%m%d_%H%M%S")
    if label:
        safe = "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in label)
        return f"{stamp}_{safe}"
    return f"{stamp}_run"


def ensure_run_dir(root: str | Path, run_id: str) -> Path:
    path = Path(root) / run_id
    path.mkdir(parents=True, exist_ok=False)
    return path


def write_json(path: str | Path, data: dict[str, Any]) -> None:
    with Path(path).open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")


def read_json(path: str | Path) -> dict[str, Any]:
    with Path(path).open("r", encoding="utf-8") as f:
        return json.load(f)


def list_runs(root: str | Path) -> list[dict[str, Any]]:
    base = Path(root)
    if not base.exists():
        return []
    records = []
    for item in sorted(base.iterdir(), reverse=True):
        manifest = item / "manifest.json"
        if manifest.exists():
            try:
                records.append(read_json(manifest))
            except Exception:
                records.append({"run_id": item.name, "status": "manifest_error"})
    return records
