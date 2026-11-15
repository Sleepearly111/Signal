"""Fit and cross-check the 2026G direct-INA amplitude calibration.

The pure-sine rectangular grid is the calibration set.  Composite-wave
components are an independent validation set.  The script never edits
firmware; it emits candidate constants and error metrics only.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path


OLD_SCALE = 0.147757367
OLD_FREQUENCY = (
    (10_000.0, 1.0000000), (20_000.0, 1.0002991),
    (50_000.0, 0.9992617), (100_000.0, 1.0000020),
    (200_000.0, 0.9998789), (300_000.0, 0.9981553),
    (400_000.0, 0.9967244), (500_000.0, 0.9928932),
)
OLD_AMPLITUDE = (
    (0.0, 0.0), (9.8688, 10.0), (50.0900, 50.0),
    (99.9594, 100.0), (199.2994, 200.0), (249.1202, 250.0),
)
OLD_UPLIFT = (
    (0.0, 0.0), (10.0, 10.0), (24.8368, 25.0),
    (49.5840, 50.0), (74.6591, 75.0), (99.6710, 100.0),
    (149.1428, 150.0), (199.1735, 200.0), (249.2841, 250.0),
)


def interp(points: tuple[tuple[float, float], ...], x: float,
           extrapolate: bool) -> float:
    if x <= points[0][0]:
        return points[0][1]
    for left, right in zip(points, points[1:]):
        if x <= right[0]:
            fraction = (x - left[0]) / (right[0] - left[0])
            return left[1] + fraction * (right[1] - left[1])
    if not extrapolate:
        return points[-1][1]
    left, right = points[-2], points[-1]
    fraction = (x - left[0]) / (right[0] - left[0])
    return left[1] + fraction * (right[1] - left[1])


def old_correct(frequency_hz: float, raw_vpp_mv: float) -> float:
    response = interp(OLD_FREQUENCY, frequency_hz, False)
    amplitude_corrected = interp(OLD_AMPLITUDE, raw_vpp_mv / response, True)
    return interp(OLD_UPLIFT, amplitude_corrected, True)


def invert_old(frequency_hz: float, final_vpp_mv: float) -> float:
    low, high = 0.0, 400.0
    while old_correct(frequency_hz, high) < final_vpp_mv:
        high *= 2.0
    for _ in range(80):
        middle = 0.5 * (low + high)
        if old_correct(frequency_hz, middle) < final_vpp_mv:
            low = middle
        else:
            high = middle
    return 0.5 * (low + high)


def aggregate(values: list[float], method: str) -> float:
    return statistics.median(values) if method == "median" else statistics.mean(values)


def load_pure(path: Path) -> list[dict[str, float]]:
    rows = []
    with path.open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            if row.get("phase") != "main" or row.get("status") not in {"ok", "extended_40"}:
                continue
            frequency_hz = float(row["target_frequency_khz"]) * 1000.0
            target = float(row["target_vpp_mv"])
            current = float(row["median_vpp_mv"])
            raw_old = invert_old(frequency_hz, current)
            rows.append({"frequency": frequency_hz, "target": target,
                         "current": current, "code": raw_old / OLD_SCALE})
    if len(rows) != 323:
        raise RuntimeError(f"expected 323 pure main rows, got {len(rows)}")
    return rows


def load_composite(path: Path) -> list[dict[str, float | str | int]]:
    rows: list[dict[str, float | str | int]] = []
    with path.open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            if row["case"].startswith("ARB_SINE_"):
                continue
            frequency_hz = float(row["measured_grid_frequency_hz"])
            current = float(row["measured_vpp_mv"])
            raw_old = invert_old(frequency_hz, current)
            rows.append({"case": row["case"], "order": int(row["order"]),
                         "frequency": frequency_hz,
                         "target": float(row["theory_vpp_mv"]),
                         "current": current, "code": raw_old / OLD_SCALE})
    if len(rows) != 207:
        raise RuntimeError(f"expected 207 composite components, got {len(rows)}")
    return rows


def fit_candidate(pure: list[dict[str, float]], frequency_knots: tuple[float, ...],
                  amplitude_knots: tuple[float, ...], method: str) -> dict:
    reference = [row for row in pure if row["frequency"] == 10_000.0]
    scale = sum(row["code"] * row["target"] for row in reference) / sum(
        row["code"] * row["code"] for row in reference)

    by_frequency_amplitude = {
        (row["frequency"], row["target"]): row["code"] * scale for row in pure
    }
    frequency_points = []
    for frequency in frequency_knots:
        ratios = []
        for amplitude in sorted({row["target"] for row in pure}):
            base = by_frequency_amplitude[(10_000.0, amplitude)]
            ratios.append(by_frequency_amplitude[(frequency, amplitude)] / base)
        frequency_points.append((frequency, aggregate(ratios, method)))
    frequency_points[0] = (frequency_points[0][0], 1.0)
    frequency_points_tuple = tuple(frequency_points)

    amplitude_points = [(0.0, 0.0)]
    for amplitude in amplitude_knots:
        refs = []
        for row in pure:
            if row["target"] == amplitude:
                response = interp(frequency_points_tuple, row["frequency"], False)
                refs.append(row["code"] * scale / response)
        amplitude_points.append((aggregate(refs, method), amplitude))
    amplitude_points_tuple = tuple(amplitude_points)
    return {"scale": scale, "frequency": frequency_points_tuple,
            "amplitude": amplitude_points_tuple, "method": method}


def correct(candidate: dict, row: dict) -> float:
    raw = float(row["code"]) * candidate["scale"]
    response = interp(candidate["frequency"], float(row["frequency"]), False)
    return interp(candidate["amplitude"], raw / response, True)


def errors(candidate: dict, rows: list[dict]) -> list[float]:
    return [correct(candidate, row) - float(row["target"]) for row in rows]


def metrics(values: list[float]) -> dict[str, float]:
    absolute = [abs(value) for value in values]
    return {"mean": statistics.mean(values),
            "mae": statistics.mean(absolute),
            "max_abs": max(absolute),
            "p95_abs": sorted(absolute)[int(0.95 * (len(absolute) - 1))]}


def worst_rows(candidate: dict, rows: list[dict], count: int = 10) -> list[dict]:
    ranked = []
    for row in rows:
        corrected = correct(candidate, row)
        ranked.append({"case": row.get("case", "pure"),
                       "frequency_hz": row["frequency"],
                       "target_vpp_mv": row["target"],
                       "corrected_vpp_mv": corrected,
                       "error_mv": corrected - float(row["target"])})
    ranked.sort(key=lambda row: abs(row["error_mv"]), reverse=True)
    return ranked[:count]


def reconstructed_vpp(components: list[tuple[int, float]]) -> float:
    minimum = math.inf
    maximum = -math.inf
    for sample in range(16_384):
        phase = 2.0 * math.pi * sample / 16_384.0
        value = sum(0.5 * vpp * math.sin(order * phase)
                    for order, vpp in components)
        minimum = min(minimum, value)
        maximum = max(maximum, value)
    return maximum - minimum


def composite_totals(candidate: dict, rows: list[dict]) -> dict[str, dict[str, float]]:
    grouped: dict[str, list[dict]] = defaultdict(list)
    for row in rows:
        grouped[str(row["case"])].append(row)
    rms_errors = []
    vpp_errors = []
    for case_rows in grouped.values():
        corrected_components = [(int(row["order"]), correct(candidate, row))
                                for row in case_rows]
        target_components = [(int(row["order"]), float(row["target"]))
                             for row in case_rows]
        corrected_rms = math.sqrt(sum(vpp * vpp for _, vpp in corrected_components) / 8.0)
        target_rms = math.sqrt(sum(vpp * vpp for _, vpp in target_components) / 8.0)
        rms_errors.append(corrected_rms - target_rms)
        vpp_errors.append(reconstructed_vpp(corrected_components) -
                          reconstructed_vpp(target_components))
    return {"rms": metrics(rms_errors), "total_vpp": metrics(vpp_errors)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pure", type=Path, required=True)
    parser.add_argument("--composite", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    pure = load_pure(args.pure)
    composite = load_composite(args.composite)
    frequency_sets = {
        "sparse": (10_000.0, 20_000.0, 50_000.0, 100_000.0, 200_000.0,
                   300_000.0, 400_000.0, 500_000.0),
        "dense": tuple(sorted({row["frequency"] for row in pure})),
    }
    amplitude_sets = {
        "sparse": (10.0, 25.0, 50.0, 75.0, 100.0, 150.0, 200.0, 250.0),
        "dense": tuple(sorted({row["target"] for row in pure})),
    }
    ranked = []
    for frequency_name, frequency_knots in frequency_sets.items():
        for amplitude_name, amplitude_knots in amplitude_sets.items():
            for method in ("median", "mean"):
                candidate = fit_candidate(pure, frequency_knots, amplitude_knots, method)
                pure_metrics = metrics(errors(candidate, pure))
                composite_metrics = metrics(errors(candidate, composite))
                # Balance the calibration grid and the independent composite
                # validation set.  A minimax-only score hid a large pure-sine
                # regression for an immaterial composite improvement.
                score = pure_metrics["max_abs"] + composite_metrics["max_abs"] + \
                    0.2 * (pure_metrics["mae"] + composite_metrics["mae"])
                ranked.append({"name": f"{frequency_name}-{amplitude_name}-{method}",
                               "score": score, "pure": pure_metrics,
                               "composite": composite_metrics,
                               "candidate": candidate})
    ranked.sort(key=lambda item: item["score"])
    best = ranked[0]
    serializable = {
        "best": best["name"], "score": best["score"],
        "input_mv_per_code": best["candidate"]["scale"],
        "frequency_response": best["candidate"]["frequency"],
        "amplitude_inverse": best["candidate"]["amplitude"],
        "pure_metrics": best["pure"],
        "composite_component_metrics": best["composite"],
        "composite_total_metrics": composite_totals(best["candidate"], composite),
        "current_pure_metrics": metrics(
            [row["current"] - row["target"] for row in pure]),
        "current_composite_component_metrics": metrics(
            [row["current"] - row["target"] for row in composite]),
        "worst_pure": worst_rows(best["candidate"], pure),
        "worst_composite": worst_rows(best["candidate"], composite),
        "ranking": [{key: value for key, value in item.items() if key != "candidate"}
                    for item in ranked],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(serializable, ensure_ascii=False, indent=2),
                           encoding="utf-8")
    print(json.dumps(serializable, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
