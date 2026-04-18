from __future__ import annotations

from pathlib import Path


def resolve_existing_file(path: str | Path, label: str) -> Path:
    resolved = Path(path).expanduser().resolve()
    if not resolved.is_file():
        raise FileNotFoundError(f"{label} not found: {resolved}")
    return resolved


def resolve_existing_dir(path: str | Path, label: str) -> Path:
    resolved = Path(path).expanduser().resolve()
    if not resolved.is_dir():
        raise FileNotFoundError(f"{label} not found: {resolved}")
    return resolved


def validate_so_name(so_name: object) -> str:
    if not isinstance(so_name, str) or not so_name:
        raise ValueError("so_name must be a non-empty string")
    if so_name != Path(so_name).name:
        raise ValueError("so_name must be a filename, not a path")
    if not so_name.endswith(".so"):
        raise ValueError("so_name must end with .so")
    return so_name


def ensure_child(parent: Path, child: Path, label: str) -> Path:
    parent_resolved = parent.resolve()
    child_resolved = child.resolve()
    if child_resolved != parent_resolved and parent_resolved not in child_resolved.parents:
        raise ValueError(f"{label} escapes allowed directory: {child_resolved}")
    return child_resolved


def validate_target_so(libs_dir: str | Path, allowed_so_dir: str | Path, so_name: object) -> str:
    name = validate_so_name(so_name)
    libs = resolve_existing_dir(libs_dir, "libs_dir")
    allowed = resolve_existing_dir(allowed_so_dir, "allowed_so_dir")
    target = ensure_child(allowed, libs / name, "target so")
    if target.parent != libs.resolve():
        raise ValueError("target so must be directly inside libs_dir")
    if not target.is_file():
        raise FileNotFoundError(f"target so not found: {target}")
    return name


def clamp_timeout(value: object, default: int, maximum: int = 300) -> int:
    if value is None:
        return default
    if not isinstance(value, int):
        raise ValueError("timeout_sec must be an integer")
    if value <= 0:
        raise ValueError("timeout_sec must be positive")
    return min(value, maximum)
