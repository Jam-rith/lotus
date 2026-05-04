#!/usr/bin/env python3
"""Run non-learned APA ordering heuristics and summarize real costs.

This script intentionally does not use linear regression or neural models. It
compares hand-written state-elimination orderings against the original order:

  original           baseline solver order
  min-pred-succ      eliminate low alive-pred * alive-succ nodes first
  expression-aware   estimate expression growth from M[i,k], M[k,k], M[k,j]
  star-risk          expression-aware plus a penalty for expensive self-closure

By default a task is killed if no new per-function summary row appears for
15 minutes. Set --stall-timeout-sec 0 to disable this guard.
"""

from __future__ import annotations

import argparse
import csv
import datetime as _dt
import re
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple


DEFAULT_ORDERS = ["original", "min-pred-succ", "expression-aware", "star-risk"]


@dataclass
class Aggregate:
    elapsed_us: int = 0
    peak_rss_kb: int = 0
    cfg_blocks: int = 0
    cfg_insts: int = 0
    cfg_edges: int = 0
    expr_total_unique_nodes: int = 0
    expr_total_shared_refs: int = 0
    expr_total_union_count: int = 0
    expr_total_concat_count: int = 0
    expr_total_star_count: int = 0
    expr_max_nodes: int = 0
    expr_max_depth: int = 0
    functions: int = 0


@dataclass
class TaskResult:
    exit_code: int
    elapsed_sec: float
    peak_rss_kb: int
    completed_functions: int
    killed_for_stall: bool = False


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--bench-dir",
        type=Path,
        default=Path("benchmarks/spec2006"),
        help="Directory containing SPEC2006 .bc files.",
    )
    parser.add_argument(
        "--tool",
        type=Path,
        default=Path("build/bin/lotus-dfa-apa"),
        help="Path to lotus-dfa-apa.",
    )
    parser.add_argument("--analysis", default="liveness")
    parser.add_argument("--elim-method", default="state")
    parser.add_argument("--orders", nargs="+", default=DEFAULT_ORDERS)
    parser.add_argument(
        "--include",
        default="",
        help="Regex filter for bitcode path/name. Empty means include all.",
    )
    parser.add_argument(
        "--exclude",
        default="",
        help="Regex filter for bitcode path/name to skip.",
    )
    parser.add_argument(
        "--repeat",
        type=int,
        default=1,
        help="Repeat each benchmark/order pair. Aggregates keep each repeat separate.",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Output directory. Default: benchmark_results/apa_simple_ordering_<timestamp>.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print selected commands without running them.",
    )
    parser.add_argument(
        "--progress-interval-sec",
        type=float,
        default=5.0,
        help="Seconds between current-function progress updates. Use 0 to disable.",
    )
    parser.add_argument(
        "--memory-interval-sec",
        type=float,
        default=60.0,
        help="Seconds between in-function memory/progress updates. Use 0 to disable.",
    )
    parser.add_argument(
        "--stall-timeout-sec",
        type=float,
        default=900.0,
        help="Kill a task if no new function summary row appears for this many seconds. Use 0 to disable.",
    )
    parser.add_argument(
        "--continue-after-function-timeout",
        action="store_true",
        help="Run each function in a separate process so a timeout skips only that function.",
    )
    return parser.parse_args()


def selected_bitcodes(bench_dir: Path, include: str, exclude: str) -> List[Path]:
    include_re = re.compile(include) if include else None
    exclude_re = re.compile(exclude) if exclude else None
    bitcodes = sorted(bench_dir.glob("*.bc"))
    selected: List[Path] = []
    for path in bitcodes:
        text = str(path)
        if include_re and not include_re.search(text):
            continue
        if exclude_re and exclude_re.search(text):
            continue
        selected.append(path)
    return selected


def ensure_output_dir(path: Path | None) -> Path:
    if path is not None:
        path.mkdir(parents=True, exist_ok=True)
        return path
    stamp = _dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = Path("benchmark_results") / f"apa_simple_ordering_{stamp}"
    out_dir.mkdir(parents=True, exist_ok=True)
    return out_dir


def shell_command(cmd: Sequence[str]) -> str:
    return " ".join(shlex.quote(part) for part in cmd)


def count_tsv_data_rows(path: Path) -> int:
    if not path.exists():
        return 0
    with path.open(errors="replace") as handle:
        rows = sum(1 for line in handle if line.strip())
    return max(0, rows - 1)


def count_defined_functions(bitcode: Path) -> int | None:
    names = list_defined_functions(bitcode)
    return len(names) if names is not None else None


def list_defined_functions(bitcode: Path) -> List[str] | None:
    llvm_dis = shutil.which("llvm-dis-14") or shutil.which("llvm-dis")
    if llvm_dis is None:
        return None
    try:
        proc = subprocess.run(
            [llvm_dis, str(bitcode), "-o", "-"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except OSError:
        return None
    if proc.returncode != 0:
        return None
    names: List[str] = []
    for line in proc.stdout.splitlines():
        if not line.startswith("define "):
            continue
        match = re.search(r"@([^(\s]+)\(", line)
        if match:
            names.append(match.group(1))
    return names


def format_kb(kb: int | None) -> str:
    if kb is None:
        return "NA"
    if kb >= 1024 * 1024:
        return f"{kb / (1024 * 1024):.2f}GiB"
    if kb >= 1024:
        return f"{kb / 1024:.1f}MiB"
    return f"{kb}KiB"


def read_process_memory_kb(pid: int) -> Tuple[int | None, int | None]:
    status_path = Path("/proc") / str(pid) / "status"
    rss_kb: int | None = None
    peak_kb: int | None = None
    try:
        with status_path.open() as handle:
            for line in handle:
                if line.startswith("VmRSS:"):
                    rss_kb = int(line.split()[1])
                elif line.startswith("VmHWM:"):
                    peak_kb = int(line.split()[1])
    except (FileNotFoundError, ProcessLookupError, PermissionError, ValueError):
        return None, None
    return rss_kb, peak_kb


def format_function_progress(rows_done: int, function_count: int | None) -> str:
    if function_count is None:
        return f"current_function={rows_done + 1} completed_rows={rows_done}"
    current = min(rows_done + 1, function_count)
    return f"current_function={current}/{function_count} completed={rows_done}/{function_count}"


def run_one(
    tool: Path,
    bitcode: Path,
    analysis: str,
    elim_method: str,
    order: str,
    summary_path: Path,
    task_index: int,
    total_tasks: int,
    repeat_index: int,
    repeat_count: int,
    function_count: int | None,
    progress_interval_sec: float,
    memory_interval_sec: float,
    stall_timeout_sec: float,
    function_name: str | None = None,
) -> TaskResult:
    cmd = [
        str(tool),
        str(bitcode),
        "--analysis",
        analysis,
        "--elim-method",
        elim_method,
        "--elim-order",
        order,
        "--order-run-summary-out",
        str(summary_path),
    ]
    if function_name:
        cmd.extend(["--function", function_name])
    if memory_interval_sec > 0:
        cmd.extend(
            [
                "--order-progress-time-interval-sec",
                str(memory_interval_sec),
            ]
        )
    started = time.monotonic()
    rows_before = count_tsv_data_rows(summary_path)
    remaining = total_tasks - task_index
    print(
        f"[{task_index}/{total_tasks}] repeat {repeat_index}/{repeat_count} "
        f"{bitcode.name} order={order} remaining={remaining}",
        file=sys.stderr,
        flush=True,
    )
    print("  RUN", shell_command(cmd), file=sys.stderr, flush=True)
    proc = subprocess.Popen(cmd)
    last_report = started
    last_rows_done = -1
    last_progress_time = started
    observed_peak_rss_kb = 0
    killed_for_stall = False
    while True:
        code = proc.poll()
        now = time.monotonic()
        rss_kb, proc_peak_kb = read_process_memory_kb(proc.pid)
        for candidate in (rss_kb, proc_peak_kb):
            if candidate is not None:
                observed_peak_rss_kb = max(observed_peak_rss_kb, candidate)
        should_report = (
            progress_interval_sec > 0
            and (now - last_report) >= progress_interval_sec
        )
        if should_report or code is not None:
            rows_done = count_tsv_data_rows(summary_path) - rows_before
            if rows_done != last_rows_done or code is not None:
                print(
                    f"  PROGRESS {format_function_progress(rows_done, function_count)} "
                    f"elapsed={now - started:.1f}s",
                    file=sys.stderr,
                    flush=True,
                )
                last_rows_done = rows_done
                if code is None:
                    last_progress_time = now
            last_report = now
        if (
            stall_timeout_sec > 0
            and code is None
            and (now - last_progress_time) >= stall_timeout_sec
        ):
            rows_done = count_tsv_data_rows(summary_path) - rows_before
            print(
                f"  KILL stalled_for={now - last_progress_time:.1f}s "
                f"{format_function_progress(rows_done, function_count)} "
                f"elapsed={now - started:.1f}s "
                f"rss={format_kb(rss_kb)} peak={format_kb(observed_peak_rss_kb)}",
                file=sys.stderr,
                flush=True,
            )
            proc.kill()
            killed_for_stall = True
            code = proc.wait()
        if code is not None:
            break
        time.sleep(
            min(
                1.0,
                max(
                    0.1,
                    min(
                        interval
                        for interval in (progress_interval_sec, memory_interval_sec, 1.0)
                        if interval > 0
                    ),
                ),
            )
        )
    print(
        f"  DONE exit={code} elapsed={time.monotonic() - started:.1f}s "
        f"peak={format_kb(observed_peak_rss_kb)}",
        file=sys.stderr,
        flush=True,
    )
    return TaskResult(
        exit_code=code,
        elapsed_sec=time.monotonic() - started,
        peak_rss_kb=observed_peak_rss_kb,
        completed_functions=count_tsv_data_rows(summary_path) - rows_before,
        killed_for_stall=killed_for_stall,
    )


def write_task_summary_header(path: Path) -> None:
    with path.open("w", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t")
        writer.writerow(
            [
                "input",
                "benchmark",
                "order",
                "function",
                "repeat",
                "exit_code",
                "killed_for_stall",
                "elapsed_sec",
                "peak_rss_kb",
                "completed_functions",
            ]
        )


def append_task_summary(
    path: Path, bitcode: Path, order: str, repeat_index: int,
    result: TaskResult, function_name: str = ""
) -> None:
    with path.open("a", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t")
        writer.writerow(
            [
                str(bitcode),
                bitcode.name,
                order,
                function_name,
                repeat_index,
                result.exit_code,
                1 if result.killed_for_stall else 0,
                f"{result.elapsed_sec:.6f}",
                result.peak_rss_kb,
                result.completed_functions,
            ]
        )


def to_int(row: Dict[str, str], field: str) -> int:
    try:
        return int(row.get(field, "0") or "0")
    except ValueError:
        return 0


def read_summary(path: Path) -> List[Dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def aggregate_rows(rows: Iterable[Dict[str, str]]) -> Dict[Tuple[str, str, str], Aggregate]:
    grouped: Dict[Tuple[str, str, str], Aggregate] = {}
    for row in rows:
        key = (row["input"], row["function"], row["requested_order"])
        agg = grouped.setdefault(key, Aggregate())
        agg.elapsed_us += to_int(row, "elapsed_us")
        agg.peak_rss_kb = max(agg.peak_rss_kb, to_int(row, "peak_rss_kb"))
        agg.cfg_blocks = max(agg.cfg_blocks, to_int(row, "cfg_blocks"))
        agg.cfg_insts = max(agg.cfg_insts, to_int(row, "cfg_insts"))
        agg.cfg_edges = max(agg.cfg_edges, to_int(row, "cfg_edges"))
        agg.expr_total_unique_nodes += to_int(row, "expr_total_unique_nodes")
        agg.expr_total_shared_refs += to_int(row, "expr_total_shared_refs")
        agg.expr_total_union_count += to_int(row, "expr_total_union_count")
        agg.expr_total_concat_count += to_int(row, "expr_total_concat_count")
        agg.expr_total_star_count += to_int(row, "expr_total_star_count")
        agg.expr_max_nodes = max(agg.expr_max_nodes, to_int(row, "expr_max_nodes"))
        agg.expr_max_depth = max(agg.expr_max_depth, to_int(row, "expr_max_depth"))
        agg.functions += 1

    by_benchmark: Dict[Tuple[str, str, str], Aggregate] = {}
    for (input_path, _function, order), fn_agg in grouped.items():
        benchmark = Path(input_path).name
        key = (input_path, benchmark, order)
        agg = by_benchmark.setdefault(key, Aggregate())
        agg.elapsed_us += fn_agg.elapsed_us
        agg.peak_rss_kb = max(agg.peak_rss_kb, fn_agg.peak_rss_kb)
        agg.cfg_blocks += fn_agg.cfg_blocks
        agg.cfg_insts += fn_agg.cfg_insts
        agg.cfg_edges += fn_agg.cfg_edges
        agg.expr_total_unique_nodes += fn_agg.expr_total_unique_nodes
        agg.expr_total_shared_refs += fn_agg.expr_total_shared_refs
        agg.expr_total_union_count += fn_agg.expr_total_union_count
        agg.expr_total_concat_count += fn_agg.expr_total_concat_count
        agg.expr_total_star_count += fn_agg.expr_total_star_count
        agg.expr_max_nodes = max(agg.expr_max_nodes, fn_agg.expr_max_nodes)
        agg.expr_max_depth = max(agg.expr_max_depth, fn_agg.expr_max_depth)
        agg.functions += 1
    return by_benchmark


def write_aggregate(path: Path, aggregate: Dict[Tuple[str, str, str], Aggregate]) -> None:
    fields = [
        "input",
        "benchmark",
        "order",
        "functions",
        "elapsed_us",
        "peak_rss_kb",
        "cfg_blocks",
        "cfg_insts",
        "cfg_edges",
        "expr_total_unique_nodes",
        "expr_total_shared_refs",
        "expr_total_union_count",
        "expr_total_concat_count",
        "expr_total_star_count",
        "expr_max_nodes",
        "expr_max_depth",
    ]
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        for (input_path, benchmark, order), agg in sorted(aggregate.items()):
            writer.writerow(
                {
                    "input": input_path,
                    "benchmark": benchmark,
                    "order": order,
                    "functions": agg.functions,
                    "elapsed_us": agg.elapsed_us,
                    "peak_rss_kb": agg.peak_rss_kb,
                    "cfg_blocks": agg.cfg_blocks,
                    "cfg_insts": agg.cfg_insts,
                    "cfg_edges": agg.cfg_edges,
                    "expr_total_unique_nodes": agg.expr_total_unique_nodes,
                    "expr_total_shared_refs": agg.expr_total_shared_refs,
                    "expr_total_union_count": agg.expr_total_union_count,
                    "expr_total_concat_count": agg.expr_total_concat_count,
                    "expr_total_star_count": agg.expr_total_star_count,
                    "expr_max_nodes": agg.expr_max_nodes,
                    "expr_max_depth": agg.expr_max_depth,
                }
            )


def ratio(num: int, den: int) -> str:
    if den == 0:
        return "NA"
    return f"{num / den:.6g}"


def write_comparison(path: Path, aggregate: Dict[Tuple[str, str, str], Aggregate]) -> None:
    by_benchmark: Dict[Tuple[str, str], Dict[str, Aggregate]] = {}
    for (_input_path, benchmark, order), agg in aggregate.items():
        by_benchmark.setdefault((_input_path, benchmark), {})[order] = agg

    fields = [
        "input",
        "benchmark",
        "order",
        "baseline_elapsed_us",
        "order_elapsed_us",
        "speedup_vs_original",
        "baseline_peak_rss_kb",
        "order_peak_rss_kb",
        "memory_ratio_vs_original",
        "baseline_expr_nodes",
        "order_expr_nodes",
        "expr_node_ratio_vs_original",
    ]
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        for (input_path, benchmark), orders in sorted(by_benchmark.items()):
            baseline = orders.get("original")
            if baseline is None:
                continue
            for order, agg in sorted(orders.items()):
                if order == "original":
                    continue
                writer.writerow(
                    {
                        "input": input_path,
                        "benchmark": benchmark,
                        "order": order,
                        "baseline_elapsed_us": baseline.elapsed_us,
                        "order_elapsed_us": agg.elapsed_us,
                        "speedup_vs_original": ratio(
                            baseline.elapsed_us, agg.elapsed_us
                        ),
                        "baseline_peak_rss_kb": baseline.peak_rss_kb,
                        "order_peak_rss_kb": agg.peak_rss_kb,
                        "memory_ratio_vs_original": ratio(
                            agg.peak_rss_kb, baseline.peak_rss_kb
                        ),
                        "baseline_expr_nodes": baseline.expr_total_unique_nodes,
                        "order_expr_nodes": agg.expr_total_unique_nodes,
                        "expr_node_ratio_vs_original": ratio(
                            agg.expr_total_unique_nodes,
                            baseline.expr_total_unique_nodes,
                        ),
                    }
                )


def write_global(path: Path, aggregate: Dict[Tuple[str, str, str], Aggregate]) -> None:
    totals: Dict[str, Aggregate] = {}
    for (_input_path, _benchmark, order), agg in aggregate.items():
        total = totals.setdefault(order, Aggregate())
        total.elapsed_us += agg.elapsed_us
        total.peak_rss_kb = max(total.peak_rss_kb, agg.peak_rss_kb)
        total.expr_total_unique_nodes += agg.expr_total_unique_nodes
        total.expr_total_concat_count += agg.expr_total_concat_count
        total.expr_total_union_count += agg.expr_total_union_count
        total.expr_total_star_count += agg.expr_total_star_count
        total.functions += agg.functions

    baseline = totals.get("original")
    fields = [
        "order",
        "functions",
        "elapsed_us",
        "speedup_vs_original",
        "peak_rss_kb",
        "memory_ratio_vs_original",
        "expr_total_unique_nodes",
        "expr_node_ratio_vs_original",
        "expr_total_concat_count",
        "expr_total_union_count",
        "expr_total_star_count",
    ]
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        for order, agg in sorted(totals.items()):
            writer.writerow(
                {
                    "order": order,
                    "functions": agg.functions,
                    "elapsed_us": agg.elapsed_us,
                    "speedup_vs_original": (
                        ratio(baseline.elapsed_us, agg.elapsed_us)
                        if baseline
                        else "NA"
                    ),
                    "peak_rss_kb": agg.peak_rss_kb,
                    "memory_ratio_vs_original": (
                        ratio(agg.peak_rss_kb, baseline.peak_rss_kb)
                        if baseline
                        else "NA"
                    ),
                    "expr_total_unique_nodes": agg.expr_total_unique_nodes,
                    "expr_node_ratio_vs_original": (
                        ratio(
                            agg.expr_total_unique_nodes,
                            baseline.expr_total_unique_nodes,
                        )
                        if baseline
                        else "NA"
                    ),
                    "expr_total_concat_count": agg.expr_total_concat_count,
                    "expr_total_union_count": agg.expr_total_union_count,
                    "expr_total_star_count": agg.expr_total_star_count,
                }
            )


def main() -> int:
    args = parse_args()
    out_dir = ensure_output_dir(args.out_dir)
    summary_path = out_dir / "run_summary.tsv"
    aggregate_path = out_dir / "aggregate_by_benchmark.tsv"
    comparison_path = out_dir / "comparison_vs_original.tsv"
    global_path = out_dir / "global_summary.tsv"
    task_summary_path = out_dir / "task_summary.tsv"
    failure_path = out_dir / "failures.tsv"

    bitcodes = selected_bitcodes(args.bench_dir, args.include, args.exclude)
    if not bitcodes:
        print("no bitcode files selected", file=sys.stderr)
        return 1

    print(f"output directory: {out_dir}", file=sys.stderr)
    print(f"selected bitcodes: {len(bitcodes)}", file=sys.stderr)
    print(f"orders: {' '.join(args.orders)}", file=sys.stderr)
    print(f"stall timeout: {args.stall_timeout_sec}s", file=sys.stderr)
    function_names: Dict[Path, List[str] | None] = {}
    if args.continue_after_function_timeout:
        function_names = {bitcode: list_defined_functions(bitcode) for bitcode in bitcodes}
        total_function_tasks = sum(
            len(names or []) for names in function_names.values()
        )
        total_tasks = total_function_tasks * len(args.orders) * args.repeat
    else:
        total_tasks = len(bitcodes) * len(args.orders) * args.repeat
    print(f"total tasks: {total_tasks}", file=sys.stderr)
    function_counts = {
        bitcode: (len(function_names[bitcode])
                  if args.continue_after_function_timeout and
                  function_names.get(bitcode) is not None
                  else count_defined_functions(bitcode))
        for bitcode in bitcodes
    }
    known_functions = sum(count for count in function_counts.values() if count is not None)
    unknown_function_files = sum(1 for count in function_counts.values() if count is None)
    if unknown_function_files:
        print(
            f"function counts: {known_functions} known; "
            f"{unknown_function_files} files unknown",
            file=sys.stderr,
        )
    else:
        print(f"function counts: {known_functions} total", file=sys.stderr)

    if args.dry_run:
        task_index = 0
        for repeat in range(args.repeat):
            for bitcode in bitcodes:
                for order in args.orders:
                    names = function_names.get(bitcode) if args.continue_after_function_timeout else None
                    run_names = names if names is not None else [None]
                    for function_name in run_names:
                        task_index += 1
                        label = (
                            f"{bitcode.name} function={function_name}"
                            if function_name
                            else bitcode.name
                        )
                        print(
                            f"[{task_index}/{total_tasks}] repeat {repeat + 1}/{args.repeat} "
                            f"{label} order={order}",
                            file=sys.stderr,
                        )
                        cmd = [
                            str(args.tool),
                            str(bitcode),
                            "--analysis",
                            args.analysis,
                            "--elim-method",
                            args.elim_method,
                            "--elim-order",
                            order,
                            "--order-run-summary-out",
                            str(summary_path),
                        ]
                        if function_name:
                            cmd.extend(["--function", function_name])
                        if args.memory_interval_sec > 0:
                            cmd.extend(
                                [
                                    "--order-progress-time-interval-sec",
                                    str(args.memory_interval_sec),
                                ]
                            )
                        print(shell_command(cmd))
        return 0

    if summary_path.exists():
        summary_path.unlink()
    write_task_summary_header(task_summary_path)
    failures: List[Tuple[str, str, int, int]] = []
    task_index = 0
    suite_started = time.monotonic()
    for repeat in range(args.repeat):
        print(f"repeat {repeat + 1}/{args.repeat}", file=sys.stderr)
        for bitcode in bitcodes:
            for order in args.orders:
                names = function_names.get(bitcode) if args.continue_after_function_timeout else None
                run_names = names if names is not None else [None]
                for function_name in run_names:
                    task_index += 1
                    result = run_one(
                        args.tool,
                        bitcode,
                        args.analysis,
                        args.elim_method,
                        order,
                        summary_path,
                        task_index,
                        total_tasks,
                        repeat + 1,
                        args.repeat,
                        1 if function_name else function_counts[bitcode],
                        args.progress_interval_sec,
                        args.memory_interval_sec,
                        args.stall_timeout_sec,
                        function_name,
                    )
                    append_task_summary(task_summary_path, bitcode, order,
                                        repeat + 1, result,
                                        function_name or "")
                    if result.exit_code != 0:
                        failures.append(
                            (
                                str(bitcode),
                                f"{order}:{function_name or '*'}",
                                result.exit_code,
                                1 if result.killed_for_stall else 0,
                            )
                        )

    if failures:
        with failure_path.open("w", newline="") as handle:
            writer = csv.writer(handle, delimiter="\t")
            writer.writerow(["input", "order", "exit_code", "killed_for_stall"])
            writer.writerows(failures)

    rows = read_summary(summary_path)
    aggregate = aggregate_rows(rows)
    write_aggregate(aggregate_path, aggregate)
    write_comparison(comparison_path, aggregate)
    write_global(global_path, aggregate)

    print(f"raw summary: {summary_path}")
    print(f"aggregate: {aggregate_path}")
    print(f"comparison: {comparison_path}")
    print(f"global: {global_path}")
    print(f"task summary: {task_summary_path}")
    print(f"total elapsed: {time.monotonic() - suite_started:.1f}s")
    if failures:
        print(f"failures: {failure_path}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
