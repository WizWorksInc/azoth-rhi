# Copyright 2026 Ian Pike
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Runs the NRI and Azoth arms as separate processes on one pinned Vulkan driver and compares them.

Each process is one sample. The arm order inside every round and the size of each process's environment are
randomized, ratios carry percentile bootstrap intervals, and the primary metrics share one Bonferroni-corrected
error rate. Everything else is a diagnostic read at the uncorrected level.
"""

import argparse
import json
import math
import os
import random
import statistics
import subprocess
import sys
import time
from pathlib import Path

ARMS = ("nri", "azoth")

ICD_CANDIDATES = (
    "/usr/local/share/vulkan/icd.d/MoltenVK_icd.json",
    "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json",
    "/opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json",
)

# Context keys that must agree between every process, or the comparison is of two machines or two workloads.
SHARED_CONTEXT = (
    "device",
    "driver",
    "driver_info",
    "driver_version",
    "vulkan_api",
    "VK_DRIVER_FILES",
    "gapi_validation",
    "asserts",
    "tracy",
    "scene",
    "scene_instances",
    "scene_triangles",
    "scene_textures",
    "scene_path_frames",
)

TIME_UNITS = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}

# Counters read per family besides the manual time.
FAMILY_COUNTERS = {
    "record": ("raw_ns", "over_raw_ns", "allocs_pass"),
    "sweep": ("record_us", "submit_us", "wait_us", "cpu_p99_us", "gpu_us", "draw_ns", "allocs_frame"),
    "scene": ("record_us", "submit_us", "wait_us", "cpu_p50_us", "cpu_p99_us", "cpu_max_us", "gpu_us", "gpu_p99_us", "draws_frame", "allocs_frame"),
    "threads": ("record_us", "submit_us", "wait_us", "cpu_p99_us", "draw_ns", "allocs_frame"),
    "churn": ("allocs_op",),
    "submit": ("allocs_op",),
}

# The metrics the verdict rests on. The sweep's 30 fps crossing joins them.
PRIMARY = {"scene": ("time_ns", "gpu_us")}
CROSSING_METRIC = "crossing_draws"

# Matches kSweepBudgetMilliseconds in scene.hpp.
SWEEP_BUDGET_NS = 1e9 / 30.0

# A raw control that moves this much between the arms means the environment differed, not the libraries.
RAW_DRIFT_LIMIT = 0.10

# Environment size alone shifts stack alignment and with it timing (Mytkowicz et al., ASPLOS 2009).
PAD_VARIABLE = "VSNRI_ENV_PAD"
PAD_LIMIT = 4096


def find_icd(requested):
    if requested:
        return requested
    sdk = os.environ.get("VULKAN_SDK")
    candidates = list(ICD_CANDIDATES)
    if sdk:
        candidates.insert(0, str(Path(sdk) / "share" / "vulkan" / "icd.d" / "MoltenVK_icd.json"))
    for candidate in candidates:
        if Path(candidate).is_file():
            return candidate
    return None


def arm_environment(icd, pad):
    environment = dict(os.environ)
    environment["VK_DRIVER_FILES"] = icd
    for name in ("VK_ICD_FILENAMES", "VK_ADD_DRIVER_FILES"):
        environment.pop(name, None)
    environment[PAD_VARIABLE] = "x" * pad
    return environment


def wait_for_load(max_load, wait_seconds):
    load = os.getloadavg()[0]
    if max_load is None or load <= max_load:
        return load
    print(f"load average {load:.1f} is above {max_load}, waiting up to {wait_seconds:.0f} seconds", flush=True)
    deadline = time.monotonic() + wait_seconds
    while load > max_load and time.monotonic() < deadline:
        time.sleep(10.0)
        load = os.getloadavg()[0]
    if load > max_load:
        sys.exit(f"load average stayed at {load:.1f}, above {max_load}. Quiet the machine or raise --max-load")
    return load


def run_arm(executable, output, icd, pad, extra):
    command = [str(executable), "--benchmark_out=" + str(output), "--benchmark_out_format=json", *extra]
    print("running", " ".join(command), f"({PAD_VARIABLE} {pad} bytes)", flush=True)
    completed = subprocess.run(command, env=arm_environment(icd, pad), check=False, stdout=subprocess.DEVNULL)
    if completed.returncode != 0:
        sys.exit(f"{executable.name} exited with {completed.returncode}")


def load_run(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def context_mismatches(runs):
    mismatches = []
    first = runs[0]["context"]
    for run in runs[1:]:
        for key in SHARED_CONTEXT:
            if run["context"].get(key) != first.get(key):
                mismatches.append(f"{key}: {first.get(key)!r} against {run['context'].get(key)!r}")
    return mismatches


def family_of(name):
    slash = name.find("/")
    return name if slash < 0 else name[:slash]


def display_name(name):
    cut = name.find("/min_time:")
    return name if cut < 0 else name[:cut]


def sweep_draws(name):
    parts = name.split("/")
    if len(parts) < 2 or not parts[1].startswith("draws:"):
        return None
    digits = parts[1][len("draws:") :]
    return int(digits) if digits.isdigit() else None


def process_values(run):
    """One process becomes one sample: the median of its repetitions for every benchmark and metric."""
    series = {}
    failures = {}
    for entry in run["benchmarks"]:
        if entry.get("run_type") != "iteration":
            continue
        name = display_name(entry.get("run_name", entry["name"]))
        if entry.get("error_occurred"):
            failures[name] = entry.get("error_message", "skipped")
            continue
        metrics = series.setdefault(name, {})
        scale = TIME_UNITS.get(entry.get("time_unit", "ns"), 1.0)
        metrics.setdefault("time_ns", []).append(entry["real_time"] * scale)
        for counter in FAMILY_COUNTERS.get(family_of(name), ()):
            if counter in entry:
                metrics.setdefault(counter, []).append(entry[counter])

    values = {name: {metric: statistics.median(samples) for metric, samples in metrics.items()} for name, metrics in series.items()}
    return values, failures


def sweep_crossing(values):
    """Interpolates the draw count at which CPU frame time reaches the 30 fps budget. Returns (draws, censoring)."""
    points = sorted((sweep_draws(name), metrics["time_ns"]) for name, metrics in values.items() if family_of(name) == "sweep" and sweep_draws(name) is not None)
    if len(points) < 2:
        return None, None
    for (low_draws, low_time), (high_draws, high_time) in zip(points, points[1:]):
        if low_time < SWEEP_BUDGET_NS <= high_time:
            return low_draws + (SWEEP_BUDGET_NS - low_time) * (high_draws - low_draws) / (high_time - low_time), ""
    if points[0][1] >= SWEEP_BUDGET_NS:
        return points[0][0], "at or below"
    return points[-1][0], "above"


def mean(values):
    return statistics.fmean(values)


def ratio_interval(azoth, nri, confidence, resamples, rng):
    """Percentile bootstrap of mean(Azoth) / mean(NRI), resampling each arm's processes independently."""
    if len(azoth) < 2 or len(nri) < 2:
        return None
    ratios = []
    for _ in range(resamples):
        denominator = mean(rng.choices(nri, k=len(nri)))
        if denominator > 0:
            ratios.append(mean(rng.choices(azoth, k=len(azoth))) / denominator)
    if not ratios:
        return None
    ratios.sort()
    tail = (1.0 - confidence) / 2.0
    last = len(ratios) - 1
    return ratios[math.floor(tail * last)], ratios[math.ceil((1.0 - tail) * last)]


def verdict(interval, margin):
    if interval is None:
        return "no interval"
    low, high = interval
    upper = 1.0 + margin
    lower = 1.0 / upper
    if low > 1.0:
        if low > upper:
            return "Azoth higher, past margin"
        return "Azoth higher, within margin" if high <= upper else "Azoth higher"
    if high < 1.0:
        if high < lower:
            return "Azoth lower, past margin"
        return "Azoth lower, within margin" if low >= lower else "Azoth lower"
    if lower <= low and high <= upper:
        return "equivalent"
    return "inconclusive"


def is_primary(name, metric):
    return metric == CROSSING_METRIC or metric in PRIMARY.get(family_of(name), ())


def format_value(metric, value):
    if metric == "time_ns":
        if value >= 1e6:
            return f"{value / 1e6:.3f} ms"
        if value >= 1e3:
            return f"{value / 1e3:.2f} us"
        return f"{value:.1f} ns"
    if metric == CROSSING_METRIC:
        return f"{value:,.0f}"
    return f"{value:.3f}"


def spread(values):
    return statistics.stdev(values) / mean(values) if len(values) > 1 and mean(values) != 0 else 0.0


def build_rows(per_process, crossings):
    """Every (benchmark, metric) present in both arms, with each arm's per-process values."""
    names = sorted(set().union(*(values.keys() for arm in ARMS for values in per_process[arm])))
    rows = []
    for name in names:
        for metric in ["time_ns", *FAMILY_COUNTERS.get(family_of(name), ())]:
            arm_values = {arm: [values[name][metric] for values in per_process[arm] if metric in values.get(name, {})] for arm in ARMS}
            if all(arm_values[arm] for arm in ARMS):
                rows.append({"benchmark": name, "metric": metric, "values": arm_values})

    crossing_values = {arm: [draws for draws, censoring in crossings[arm] if draws is not None and not censoring] for arm in ARMS}
    censored = {arm: [censoring for draws, censoring in crossings[arm] if censoring] for arm in ARMS}
    if all(crossing_values[arm] for arm in ARMS) or any(censored[arm] for arm in ARMS):
        rows.append({"benchmark": "sweep", "metric": CROSSING_METRIC, "values": crossing_values, "censored": censored})
    return rows


def analyze(rows, options, rng):
    primary = [row for row in rows if is_primary(row["benchmark"], row["metric"])]
    family_alpha = options.alpha / max(len(primary), 1)

    for row in rows:
        values = row["values"]
        primary_row = is_primary(row["benchmark"], row["metric"])
        confidence = 1.0 - (family_alpha if primary_row else options.alpha)
        row["primary"] = primary_row
        row["confidence"] = confidence
        row["nri_mean"] = mean(values["nri"]) if values["nri"] else None
        row["azoth_mean"] = mean(values["azoth"]) if values["azoth"] else None
        row["nri_cv"] = spread(values["nri"]) if values["nri"] else None
        row["azoth_cv"] = spread(values["azoth"]) if values["azoth"] else None
        row["azoth_over_nri"] = row["azoth_mean"] / row["nri_mean"] if row["nri_mean"] else None

        interval = None
        if row.get("censored") and any(row["censored"][arm] for arm in ARMS):
            row["verdict"] = "censored: " + ", ".join(f"{arm} {' '.join(row['censored'][arm])}" for arm in ARMS if row["censored"][arm])
        elif row["nri_mean"] == 0:
            row["verdict"] = "NRI is zero, no ratio"
        else:
            interval = ratio_interval(values["azoth"], values["nri"], confidence, options.resamples, rng)
            row["verdict"] = verdict(interval, options.margin)
        row["interval"] = list(interval) if interval else None

        if row["metric"] == "raw_ns" and row["nri_mean"]:
            drift = abs(row["azoth_mean"] / row["nri_mean"] - 1.0)
            if drift > RAW_DRIFT_LIMIT:
                row["verdict"] = f"raw control drifted {drift * 100:.0f} percent, distrust this shape"
        if row["metric"] == "draws_frame" and row["nri_mean"] != row["azoth_mean"]:
            row["verdict"] = "the arms drew different workloads"

    return len(primary), family_alpha


def print_rows(rows, count, family_alpha, options):
    print()
    print(f"each sample is one process. Ratios are mean(Azoth) / mean(NRI) with {options.resamples} bootstrap resamples.")
    print(f"primary (*): {count} comparisons sharing alpha {options.alpha}, so each interval is {100 * (1 - family_alpha):.2f} percent.")
    print(f"diagnostics: {100 * (1 - options.alpha):.0f} percent intervals, uncorrected. Equivalence margin {100 * options.margin:.0f} percent.")
    print("higher time is slower. A higher crossing is better.")

    header = f"{'benchmark':<34} {'metric':<15} {'NRI mean':>12} {'Azoth mean':>12} {'Azoth/NRI':>9} {'interval':>17}  verdict"
    print()
    print(header)
    print("-" * len(header))
    for row in sorted(rows, key=lambda row: (not row["primary"], row["benchmark"], row["metric"])):
        metric = row["metric"]
        nri = format_value(metric, row["nri_mean"]) if row["nri_mean"] is not None else "-"
        azoth = format_value(metric, row["azoth_mean"]) if row["azoth_mean"] is not None else "-"
        ratio = f"{row['azoth_over_nri']:.3f}" if row["azoth_over_nri"] is not None else "n/a"
        interval = f"[{row['interval'][0]:.3f}, {row['interval'][1]:.3f}]" if row["interval"] else ""
        marker = "*" if row["primary"] else " "
        print(f"{row['benchmark']:<34} {metric:<13}{marker:>2} {nri:>12} {azoth:>12} {ratio:>9} {interval:>17}  {row['verdict']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default="out/bench-nri", help="build tree holding benchmarks/vs_nri")
    parser.add_argument("--rounds", type=int, default=6, help="each round runs both arms once, in a random order")
    parser.add_argument("--repetitions", type=int, default=3, help="repetitions inside one process, folded into one sample by their median")
    parser.add_argument("--cooldown", type=float, default=5.0, help="seconds to idle between processes so heat from one does not land on the next")
    parser.add_argument("--max-load", type=float, help="wait before each process until the one-minute load average is at or below this")
    parser.add_argument("--load-wait", type=float, default=600.0, help="seconds to wait for --max-load before giving up")
    parser.add_argument("--seed", type=int, help="seed for the run order, environment padding and bootstrap")
    parser.add_argument("--alpha", type=float, default=0.05, help="error rate, shared by the primary metrics")
    parser.add_argument("--margin", type=float, default=0.05, help="ratio band around 1 that counts as equivalent")
    parser.add_argument("--resamples", type=int, default=10000, help="bootstrap resamples per comparison")
    parser.add_argument("--icd", help="Vulkan driver manifest to pin, MoltenVK by default")
    parser.add_argument("--out", help="directory for the JSON each process writes")
    parser.add_argument("--analyze", help="skip running and analyze the nri-*.json and azoth-*.json already in this directory")
    parser.add_argument("--allow-mismatch", action="store_true", help="report even when the processes saw different drivers, settings or scenes")
    parser.epilog = "Any other argument, such as --benchmark_filter=scene, is passed to both executables."
    options, extra = parser.parse_known_args()

    seed = options.seed if options.seed is not None else int(time.time())
    rng = random.Random(seed)
    plan = []

    if options.analyze:
        out_dir = Path(options.analyze)
        outputs = {arm: sorted(out_dir.glob(f"{arm}-*.json")) for arm in ARMS}
        if not all(outputs[arm] for arm in ARMS):
            sys.exit(f"{out_dir} holds no runs of both arms")
    else:
        icd = find_icd(options.icd)
        if icd is None:
            sys.exit("no MoltenVK driver manifest found, pass --icd")

        bin_dir = Path(options.build) / "benchmarks" / "vs_nri"
        executables = {arm: bin_dir / f"rhi_bench_vs_nri_{arm}" for arm in ARMS}
        for arm, executable in executables.items():
            if not executable.is_file():
                sys.exit(f"{executable} is missing, build the bench-nri preset first")

        if not any(argument.startswith("--benchmark_repetitions") for argument in extra):
            extra = [f"--benchmark_repetitions={options.repetitions}", *extra]

        out_dir = Path(options.out) if options.out else bin_dir / "results" / time.strftime("%Y%m%d-%H%M%S")
        out_dir.mkdir(parents=True, exist_ok=True)

        print("pinned driver manifest:", icd)
        print("seed:", seed)
        outputs = {arm: [] for arm in ARMS}
        for round_index in range(options.rounds):
            order = list(ARMS)
            rng.shuffle(order)
            for arm in order:
                pad = rng.randrange(PAD_LIMIT)
                load = wait_for_load(options.max_load, options.load_wait)
                output = out_dir / f"{arm}-{round_index}.json"
                run_arm(executables[arm], output, icd, pad, extra)
                plan.append({"round": round_index, "arm": arm, "pad": pad, "load_before": load})
                outputs[arm].append(output)
                time.sleep(options.cooldown)

    runs = {arm: [load_run(path) for path in outputs[arm]] for arm in ARMS}

    mismatches = context_mismatches([*runs["nri"], *runs["azoth"]])
    if mismatches:
        print("the processes did not share one environment:")
        for mismatch in mismatches:
            print("  ", mismatch)
        if not options.allow_mismatch:
            sys.exit(1)

    context = runs["nri"][0]["context"]
    print()
    print(f"device {context.get('device')}, driver {context.get('driver')} ({context.get('driver_info')}), Vulkan {context.get('vulkan_api')}")
    print(f"scene {context.get('scene')}: {context.get('scene_instances')} instances, {context.get('scene_triangles')} triangles, textures {context.get('scene_textures')}")
    for arm in ARMS:
        print(f"{arm}: {runs[arm][0]['context'].get('library')} {runs[arm][0]['context'].get('library_version')}, {len(runs[arm])} processes")
    if min(len(runs[arm]) for arm in ARMS) < 5:
        print("fewer than five processes an arm, so the intervals are coarse")

    per_process = {arm: [] for arm in ARMS}
    failures = {arm: {} for arm in ARMS}
    crossings = {arm: [] for arm in ARMS}
    for arm in ARMS:
        for run in runs[arm]:
            values, failed = process_values(run)
            per_process[arm].append(values)
            failures[arm].update(failed)
            crossings[arm].append(sweep_crossing(values))

    rows = build_rows(per_process, crossings)
    count, family_alpha = analyze(rows, options, rng)
    print_rows(rows, count, family_alpha, options)

    for arm in ARMS:
        for name, message in sorted(failures[arm].items()):
            print(f"{arm}: {name} did not run: {message}")

    summary = out_dir / "summary.json"
    with open(summary, "w", encoding="utf-8") as handle:
        json.dump(
            {
                "seed": seed,
                "alpha": options.alpha,
                "margin": options.margin,
                "resamples": options.resamples,
                "primary_comparisons": count,
                "plan": plan,
                "rows": rows,
            },
            handle,
            indent=2,
        )
    print()
    print("summary written to", summary)


if __name__ == "__main__":
    main()
