from __future__ import annotations

from typing import Any

from ..diff import diff_logs
from ..log import filter_calls, load_log, registered_natives, summary
from ..store import list_runs, run_log_path, run_manifest, run_summary
from .execution import (
    ExecutionConfig,
    rerun_with_mock_tool,
    run_harness_tool,
    run_invoke_plan_tool,
    validate_mock_config_tool,
)


JSONDict = dict[str, Any]


def build_server(runs_root: str = "runs", execution_config: ExecutionConfig | None = None) -> Any:
    try:
        from mcp.server.fastmcp import FastMCP
    except ImportError as exc:
        raise RuntimeError(
            "mcp Python SDK is required for 'jni-tracer mcp serve'. "
            "Install the package with 'python3 -m pip install -e python' "
            "or 'python3 -m pip install mcp'."
        ) from exc

    config = execution_config or ExecutionConfig()
    mcp = FastMCP(
        "jni-tracer",
        instructions=(
            "Inspect android-jni-tracer run stores and, when explicitly enabled, "
            "run controlled Android JNI harness experiments."
        ),
    )

    @mcp.tool(name="list_runs")
    def list_runs_tool(limit: int = 20) -> list[JSONDict]:
        """List available jni-tracer runs."""
        return list_runs(runs_root)[:limit]

    @mcp.tool()
    def get_run(run_id: str) -> JSONDict:
        """Get a run manifest."""
        return run_manifest(runs_root, run_id)

    @mcp.tool()
    def get_summary(run_id: str) -> JSONDict:
        """Get a run summary."""
        return run_summary(runs_root, run_id)

    @mcp.tool()
    def get_calls(
        run_id: str,
        function: str | None = None,
        class_name: str | None = None,
        method_name: str | None = None,
    ) -> list[JSONDict]:
        """Get calls from a run, optionally filtered."""
        data = load_log(run_log_path(runs_root, run_id))
        return filter_calls(data, function=function, class_name=class_name, method_name=method_name)

    @mcp.tool()
    def get_natives(run_id: str) -> list[JSONDict]:
        """Get RegisterNatives entries for a run."""
        return registered_natives(load_log(run_log_path(runs_root, run_id)))

    @mcp.tool()
    def get_classes(run_id: str) -> list[str]:
        """Get class names observed in a run."""
        return summary(load_log(run_log_path(runs_root, run_id)))["classes"]

    @mcp.tool()
    def diff_runs(run_a: str, run_b: str) -> JSONDict:
        """Diff two runs by function counts and registered natives."""
        a = load_log(run_log_path(runs_root, run_a))
        b = load_log(run_log_path(runs_root, run_b))
        return diff_logs(a, b)

    @mcp.tool()
    def validate_mock_config(mock_config: JSONDict) -> JSONDict:
        """Validate an inline mock config without running the harness."""
        return validate_mock_config_tool({"mock_config": mock_config})

    if config.allow_execute:

        @mcp.tool()
        def run_harness(
            so_name: str,
            label: str | None = None,
            mock_config: JSONDict | None = None,
            timeout_sec: int | None = None,
        ) -> JSONDict:
            """Run the configured Android harness for JNI_OnLoad observation."""
            return run_harness_tool(
                config,
                runs_root,
                {
                    "so_name": so_name,
                    "label": label,
                    "mock_config": mock_config,
                    "timeout_sec": timeout_sec,
                },
            )

        @mcp.tool()
        def run_invoke_plan(
            so_name: str,
            invoke_plan: list[JSONDict],
            label: str | None = None,
            mock_config: JSONDict | None = None,
            timeout_sec: int | None = None,
        ) -> JSONDict:
            """Run the configured Android harness with an inline invoke plan."""
            return run_invoke_plan_tool(
                config,
                runs_root,
                {
                    "so_name": so_name,
                    "label": label,
                    "invoke_plan": invoke_plan,
                    "mock_config": mock_config,
                    "timeout_sec": timeout_sec,
                },
            )

        @mcp.tool()
        def rerun_with_mock(
            base_run_id: str,
            mock_config: JSONDict,
            label: str | None = None,
            reuse_invoke_plan: bool = True,
            timeout_sec: int | None = None,
        ) -> JSONDict:
            """Rerun a base run with an inline mock config and return the diff."""
            return rerun_with_mock_tool(
                config,
                runs_root,
                {
                    "base_run_id": base_run_id,
                    "label": label,
                    "mock_config": mock_config,
                    "reuse_invoke_plan": reuse_invoke_plan,
                    "timeout_sec": timeout_sec,
                },
            )

    return mcp


def serve(runs_root: str = "runs", execution_config: ExecutionConfig | None = None) -> None:
    build_server(runs_root, execution_config).run(transport="stdio")
