"""Create a *proposal only* from a completed 2026G sine sweep.

The STM32 source and its active calibration tables are intentionally never
modified here.  This tool states the additional correction that the sine data
would suggest, so it can later be compared with harmonic-combination results.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path


CURRENT_MV_PER_CODE = 0.147757367


def load_rows(run_dir: Path) -> list[dict[str, str]]:
    path = run_dir / "point_summary.csv"
    with path.open(encoding="utf-8-sig", newline="") as handle:
        rows = list(csv.DictReader(handle))
    return [
        row
        for row in rows
        if row.get("phase") == "main"
        and row.get("status", "").startswith(("ok", "extended_40"))
        and row.get("target_frequency_khz")
        and row.get("target_vpp_mv")
        and row.get("median_vpp_mv")
    ]


def through_origin_scale(rows: list[dict[str, str]]) -> float:
    numerator = 0.0
    denominator = 0.0
    for row in rows:
        measured = float(row["median_vpp_mv"])
        target = float(row["target_vpp_mv"])
        numerator += measured * target
        denominator += measured * measured
    if denominator <= 0.0:
        raise ValueError("no valid sine measurements")
    return numerator / denominator


def nearest(rows: list[dict[str, str]], frequency_khz: float, amplitude_mv: float) -> dict[str, str] | None:
    choices = [
        row for row in rows
        if math.isclose(float(row["target_frequency_khz"]), frequency_khz, abs_tol=1e-9)
        and math.isclose(float(row["target_vpp_mv"]), amplitude_mv, abs_tol=1e-9)
    ]
    return choices[0] if choices else None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("run_dir", type=Path)
    args = parser.parse_args()
    run_dir = args.run_dir
    rows = load_rows(run_dir)
    if len(rows) < 20:
        raise SystemExit("not enough completed sine points")

    global_scale = through_origin_scale(rows)
    frequency_rows: list[dict[str, float]] = []
    amplitude_rows: list[dict[str, float]] = []

    frequencies = sorted({float(row["target_frequency_khz"]) for row in rows})
    amplitudes = sorted({float(row["target_vpp_mv"]) for row in rows})
    for frequency in frequencies:
        row = nearest(rows, frequency, 100.0)
        if row is not None:
            measured = float(row["median_vpp_mv"])
            frequency_rows.append({
                "frequency_khz": frequency,
                "target_vpp_mv": 100.0,
                "measured_vpp_mv": measured,
                "additional_ratio": 100.0 / measured,
                "error_mv": measured - 100.0,
            })
    for amplitude in amplitudes:
        row = nearest(rows, 10.0, amplitude)
        if row is not None:
            measured = float(row["median_vpp_mv"])
            amplitude_rows.append({
                "reference_frequency_khz": 10.0,
                "target_vpp_mv": amplitude,
                "measured_vpp_mv": measured,
                "additional_ratio": amplitude / measured,
                "error_mv": measured - amplitude,
            })

    residuals_before = [float(row["median_vpp_mv"]) - float(row["target_vpp_mv"]) for row in rows]
    residuals_global = [float(row["median_vpp_mv"]) * global_scale - float(row["target_vpp_mv"]) for row in rows]
    proposal = {
        "purpose": "proposal only; no firmware values have been written",
        "input_rows": len(rows),
        "active_input_mv_per_code": CURRENT_MV_PER_CODE,
        "global_scale_candidate": global_scale,
        "global_mv_per_code_candidate_if_global_only": CURRENT_MV_PER_CODE * global_scale,
        "mean_signed_error_before_mv": sum(residuals_before) / len(residuals_before),
        "max_abs_error_before_mv": max(abs(x) for x in residuals_before),
        "mean_signed_error_after_global_mv": sum(residuals_global) / len(residuals_global),
        "max_abs_error_after_global_mv": max(abs(x) for x in residuals_global),
        "frequency_additional_ratio_at_100mv": frequency_rows,
        "amplitude_additional_ratio_at_10khz": amplitude_rows,
        "implementation_rule": (
            "Use either the global mV/code candidate OR replace/append the post-DSP "
            "frequency/amplitude correction tables. Do not apply both as a full-scale "
            "change, otherwise the same error is corrected twice."
        ),
    }
    out_json = run_dir / "sine_calibration_proposal.json"
    out_json.write_text(json.dumps(proposal, ensure_ascii=False, indent=2), encoding="utf-8")

    out_csv = run_dir / "sine_calibration_proposal_frequency.csv"
    with out_csv.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(frequency_rows[0]) if frequency_rows else ["frequency_khz"])
        writer.writeheader()
        writer.writerows(frequency_rows)

    out_md = run_dir / "sine_calibration_proposal.md"
    out_md.write_text(
        "# 单正弦标定候选（未写入固件）\n\n"
        f"- 有效正式点：{len(rows)}\n"
        f"- 当前 `input_mv_per_code`：{CURRENT_MV_PER_CODE:.9f}\n"
        f"- 仅全局比例候选：`{global_scale:.9f}`\n"
        f"- 若只采用全局比例，候选 `input_mv_per_code`：`{CURRENT_MV_PER_CODE * global_scale:.9f}`\n"
        f"- 原始平均有符号误差：{proposal['mean_signed_error_before_mv']:.4f} mV\n"
        f"- 原始最大绝对误差：{proposal['max_abs_error_before_mv']:.4f} mV\n"
        f"- 仅全局比例后平均有符号误差：{proposal['mean_signed_error_after_global_mv']:.4f} mV\n"
        f"- 仅全局比例后最大绝对误差：{proposal['max_abs_error_after_global_mv']:.4f} mV\n\n"
        "本报告只记录候选值；在谐波验证完成前，不得据此修改 `g2026_calibration.c`。\n",
        encoding="utf-8",
    )
    print(out_json)
    print(out_md)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
