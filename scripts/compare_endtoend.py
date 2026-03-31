#!/usr/bin/env python3
import argparse
import csv
import math
import os
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

ELEC_COLOR = "#1f77b4"
OPTICAL_COLOR = "#ff7f0e"


def canonical_label(label: str) -> str:
    low = (label or "").strip().lower()
    if "elec" in low:
        return "elec"
    if "opt" in low:
        return "optical"
    return label


def color_for_label(label: str) -> str:
    key = canonical_label(label)
    if key == "elec":
        return ELEC_COLOR
    if key == "optical":
        return OPTICAL_COLOR
    return "#7f7f7f"


def to_float(value: str) -> float:
    text = (value or "").strip().lower()
    if text in {"", "none", "nan", "-nan", "inf", "-inf"}:
        return 0.0
    try:
        num = float(text)
    except ValueError:
        return 0.0
    return num if math.isfinite(num) else 0.0


def parse_endtoend(csv_path: str):
    header = None
    rows = []
    with open(csv_path, newline="") as f:
        reader = csv.reader(f)
        for raw in reader:
            row = [c.strip() for c in raw]
            if not row or not row[0]:
                continue
            if row[0] == "layer_name":
                header = row
                continue
            if header is None:
                continue
            if row[0] in {"SUM", "total exposed comm"}:
                break
            rows.append(row)
    if header is None or not rows:
        raise RuntimeError(f"No layer rows found in: {csv_path}")
    return header, rows


def header_index(header):
    idx = {name: i for i, name in enumerate(header)}
    needed = [
        "layer_name",
        "fwd compute",
        "wg compute",
        "ig compute",
        "fwd exposed comm",
        "wg exposed comm",
        "ig exposed comm",
    ]
    for key in needed:
        if key not in idx:
            raise RuntimeError(f"Missing column in EndToEnd CSV: {key}")
    return idx


def aggregate(rows, idx):
    agg = defaultdict(lambda: defaultdict(float))
    for r in rows:
        name = r[idx["layer_name"]]
        agg[name]["fwd_compute"] += to_float(r[idx["fwd compute"]])
        agg[name]["wg_compute"] += to_float(r[idx["wg compute"]])
        agg[name]["ig_compute"] += to_float(r[idx["ig compute"]])
        agg[name]["fwd_exposed"] += to_float(r[idx["fwd exposed comm"]])
        agg[name]["wg_exposed"] += to_float(r[idx["wg exposed comm"]])
        agg[name]["ig_exposed"] += to_float(r[idx["ig exposed comm"]])
    return agg


def enrich_totals(metric_dict):
    out = dict(metric_dict)
    out["total_compute"] = (
        out.get("fwd_compute", 0.0)
        + out.get("wg_compute", 0.0)
        + out.get("ig_compute", 0.0)
    )
    out["total_exposed"] = (
        out.get("fwd_exposed", 0.0)
        + out.get("wg_exposed", 0.0)
        + out.get("ig_exposed", 0.0)
    )
    out["total_time"] = out["total_compute"] + out["total_exposed"]
    return out


def sum_all_layers(agg):
    total = defaultdict(float)
    for v in agg.values():
        for k, val in v.items():
            total[k] += val
    return enrich_totals(total)


def pct_change(base, other):
    if abs(base) < 1e-12:
        return 0.0 if abs(other) < 1e-12 else math.nan
    return (other - base) * 100.0 / base


def build_layer_diff(agg_a, agg_b):
    rows = []
    for name in sorted(set(agg_a.keys()) | set(agg_b.keys())):
        a = enrich_totals(agg_a.get(name, {}))
        b = enrich_totals(agg_b.get(name, {}))
        row = {
            "layer_name": name,
            "a_total_compute": a["total_compute"],
            "b_total_compute": b["total_compute"],
            "delta_total_compute": b["total_compute"] - a["total_compute"],
            "pct_total_compute": pct_change(a["total_compute"], b["total_compute"]),
            "a_total_exposed": a["total_exposed"],
            "b_total_exposed": b["total_exposed"],
            "delta_total_exposed": b["total_exposed"] - a["total_exposed"],
            "pct_total_exposed": pct_change(a["total_exposed"], b["total_exposed"]),
            "a_fwd_exposed": a.get("fwd_exposed", 0.0),
            "b_fwd_exposed": b.get("fwd_exposed", 0.0),
            "delta_fwd_exposed": b.get("fwd_exposed", 0.0) - a.get("fwd_exposed", 0.0),
            "a_wg_exposed": a.get("wg_exposed", 0.0),
            "b_wg_exposed": b.get("wg_exposed", 0.0),
            "delta_wg_exposed": b.get("wg_exposed", 0.0) - a.get("wg_exposed", 0.0),
            "a_ig_exposed": a.get("ig_exposed", 0.0),
            "b_ig_exposed": b.get("ig_exposed", 0.0),
            "delta_ig_exposed": b.get("ig_exposed", 0.0) - a.get("ig_exposed", 0.0),
        }
        rows.append(row)
    rows.sort(key=lambda x: abs(x["delta_total_exposed"]), reverse=True)
    return rows


def write_layer_diff_csv(out_csv, layer_rows):
    cols = [
        "layer_name",
        "a_total_compute",
        "b_total_compute",
        "delta_total_compute",
        "pct_total_compute",
        "a_total_exposed",
        "b_total_exposed",
        "delta_total_exposed",
        "pct_total_exposed",
        "a_fwd_exposed",
        "b_fwd_exposed",
        "delta_fwd_exposed",
        "a_wg_exposed",
        "b_wg_exposed",
        "delta_wg_exposed",
        "a_ig_exposed",
        "b_ig_exposed",
        "delta_ig_exposed",
    ]
    with open(out_csv, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=cols)
        writer.writeheader()
        for row in layer_rows:
            writer.writerow(row)


def write_summary_csv(out_csv, totals_a, totals_b, label_a, label_b):
    metrics = [
        "total_compute",
        "total_exposed",
        "total_time",
        "fwd_exposed",
        "wg_exposed",
        "ig_exposed",
    ]
    with open(out_csv, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["metric", label_a, label_b, "delta", "pct_change"])
        for key in metrics:
            a = totals_a.get(key, 0.0)
            b = totals_b.get(key, 0.0)
            writer.writerow([key, a, b, b - a, pct_change(a, b)])


def plot_totals(totals_a, totals_b, label_a, label_b, outdir):
    display_a = canonical_label(label_a)
    display_b = canonical_label(label_b)
    color_a = color_for_label(label_a)
    color_b = color_for_label(label_b)

    labels = ["total_exposed", "total_time"]
    series = [
        {
            "display": display_a,
            "color": color_a,
            "values": [totals_a[k] for k in labels],
        },
        {
            "display": display_b,
            "color": color_b,
            "values": [totals_b[k] for k in labels],
        },
    ]
    if {display_a, display_b} == {"elec", "optical"}:
        series.sort(key=lambda s: 0 if s["display"] == "elec" else 1)

    left = series[0]
    right = series[1]

    x = list(range(len(labels)))
    width = 0.38
    all_vals = left["values"] + right["values"]
    vmin = min(all_vals)
    vmax = max(all_vals)
    span = vmax - vmin
    if span < 1e-9:
        margin = max(abs(vmax) * 0.01, 1.0)
    else:
        # Use a softer zoom so small deltas are visible but not visually exaggerated.
        margin = max(span * 1.0, abs(vmax) * 0.001)

    plt.figure(figsize=(8, 5))
    bars_left = plt.bar(
        [i - width / 2 for i in x],
        left["values"],
        width=width,
        label=left["display"],
        color=left["color"],
    )
    bars_right = plt.bar(
        [i + width / 2 for i in x],
        right["values"],
        width=width,
        label=right["display"],
        color=right["color"],
    )
    plt.xticks(x, labels)
    plt.ylabel("time")
    plt.ylim(vmin - margin, vmax + margin)
    plt.title("EndToEnd Totals Comparison")
    plt.bar_label(bars_left, fmt="%.3f", padding=3, fontsize=8)
    plt.bar_label(bars_right, fmt="%.3f", padding=3, fontsize=8)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, "totals_comparison.png"), dpi=180)
    plt.close()


def plot_top_exposed_delta(layer_rows, label_a, label_b, outdir, top_n):
    rows = layer_rows[:top_n]
    if not rows:
        return
    labels = [r["layer_name"] for r in rows]
    deltas = [r["delta_total_exposed"] for r in rows]
    colors = ["#2ca02c" if d < 0 else "#d62728" for d in deltas]

    plt.figure(figsize=(11, max(5, 0.35 * len(rows))))
    plt.barh(labels, deltas, color=colors)
    plt.axvline(0.0, color="black", linewidth=0.8)
    plt.gca().invert_yaxis()
    plt.xlabel(f"delta exposed comm ({label_b} - {label_a})")
    plt.title(f"Top {len(rows)} Layers by |delta exposed comm|")
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, "top_layers_exposed_delta.png"), dpi=180)
    plt.close()


def plot_phase_delta(totals_a, totals_b, label_a, label_b, outdir):
    phases = ["fwd_exposed", "wg_exposed", "ig_exposed"]
    deltas = [totals_b[p] - totals_a[p] for p in phases]
    colors = ["#2ca02c" if d < 0 else "#d62728" for d in deltas]

    plt.figure(figsize=(7, 5))
    plt.bar(phases, deltas, color=colors)
    plt.axhline(0.0, color="black", linewidth=0.8)
    plt.ylabel(f"delta exposed comm ({label_b} - {label_a})")
    plt.title("Exposed Comm Delta by Phase")
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, "phase_exposed_delta.png"), dpi=180)
    plt.close()


def main():
    parser = argparse.ArgumentParser(description="Compare two SimAI EndToEnd CSV files")
    parser.add_argument("--a", required=True, help="Baseline EndToEnd CSV path")
    parser.add_argument("--b", required=True, help="Compared EndToEnd CSV path")
    parser.add_argument("--label-a", default="A", help="Legend label for --a")
    parser.add_argument("--label-b", default="B", help="Legend label for --b")
    parser.add_argument(
        "-o",
        "--outdir",
        default="results/endtoend_compare",
        help="Directory for output files",
    )
    parser.add_argument(
        "--top-n",
        type=int,
        default=20,
        help="Top N layers by absolute exposed delta",
    )
    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)

    header_a, rows_a = parse_endtoend(args.a)
    header_b, rows_b = parse_endtoend(args.b)
    idx_a = header_index(header_a)
    idx_b = header_index(header_b)
    agg_a = aggregate(rows_a, idx_a)
    agg_b = aggregate(rows_b, idx_b)

    totals_a = sum_all_layers(agg_a)
    totals_b = sum_all_layers(agg_b)
    layer_rows = build_layer_diff(agg_a, agg_b)

    write_layer_diff_csv(os.path.join(args.outdir, "layer_diff.csv"), layer_rows)
    write_summary_csv(
        os.path.join(args.outdir, "summary_totals.csv"),
        totals_a,
        totals_b,
        args.label_a,
        args.label_b,
    )
    plot_totals(totals_a, totals_b, args.label_a, args.label_b, args.outdir)
    plot_top_exposed_delta(layer_rows, args.label_a, args.label_b, args.outdir, args.top_n)
    plot_phase_delta(totals_a, totals_b, args.label_a, args.label_b, args.outdir)

    print(f"A rows: {len(rows_a)} from {args.a}")
    print(f"B rows: {len(rows_b)} from {args.b}")
    print(f"Layer count in A: {len(agg_a)}")
    print(f"Layer count in B: {len(agg_b)}")
    print(f"Outputs written to: {args.outdir}")
    print(f"total_exposed delta ({args.label_b} - {args.label_a}): {totals_b['total_exposed'] - totals_a['total_exposed']:.6f}")
    print(f"total_time delta ({args.label_b} - {args.label_a}): {totals_b['total_time'] - totals_a['total_time']:.6f}")


if __name__ == "__main__":
    main()
