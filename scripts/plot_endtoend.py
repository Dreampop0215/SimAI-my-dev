#!/usr/bin/env python3
import argparse
import csv
import math
import os
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


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
        raise RuntimeError("No layer rows found. Check the input CSV format.")
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


def plot_top_exposed(agg, outdir, top_n):
    items = []
    for name, v in agg.items():
        exposed = v["fwd_exposed"] + v["wg_exposed"] + v["ig_exposed"]
        compute = v["fwd_compute"] + v["wg_compute"] + v["ig_compute"]
        items.append((name, exposed, compute))
    items.sort(key=lambda x: x[1], reverse=True)
    items = items[:top_n]
    if not items:
        return

    labels = [x[0] for x in items]
    exposed = [x[1] for x in items]
    compute = [x[2] for x in items]

    plt.figure(figsize=(12, 5))
    plt.bar(labels, exposed, label="exposed_comm")
    plt.bar(labels, compute, bottom=exposed, label="compute")
    plt.xticks(rotation=45, ha="right")
    plt.ylabel("time")
    plt.title(f"Top {len(items)} Layers by Exposed Comm")
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, "top_layers_exposed_vs_compute.png"), dpi=180)
    plt.close()


def plot_breakdown(agg, outdir):
    totals = {
        "fwd_exposed": 0.0,
        "wg_exposed": 0.0,
        "ig_exposed": 0.0,
    }
    for v in agg.values():
        for k in totals:
            totals[k] += v[k]

    labels = list(totals.keys())
    values = [totals[k] for k in labels]
    plt.figure(figsize=(6, 6))
    plt.pie(values, labels=labels, autopct="%1.1f%%")
    plt.title("Exposed Comm Breakdown")
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, "exposed_comm_breakdown.png"), dpi=180)
    plt.close()


def plot_hot_layers(agg, outdir, top_n):
    items = []
    for name, v in agg.items():
        items.append(
            (
                name,
                v["fwd_exposed"] + v["wg_exposed"] + v["ig_exposed"],
                v["fwd_exposed"],
                v["wg_exposed"],
                v["ig_exposed"],
            )
        )
    items.sort(key=lambda x: x[1], reverse=True)
    items = items[:top_n]
    if not items:
        return
    labels = [x[0] for x in items]
    fwd = [x[2] for x in items]
    wg = [x[3] for x in items]
    ig = [x[4] for x in items]

    plt.figure(figsize=(12, 5))
    plt.bar(labels, fwd, label="fwd_exposed")
    plt.bar(labels, wg, bottom=fwd, label="wg_exposed")
    bottom2 = [a + b for a, b in zip(fwd, wg)]
    plt.bar(labels, ig, bottom=bottom2, label="ig_exposed")
    plt.xticks(rotation=45, ha="right")
    plt.ylabel("time")
    plt.title(f"Top {len(items)} Layers Exposed Comm Composition")
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, "top_layers_exposed_composition.png"), dpi=180)
    plt.close()


def main():
    parser = argparse.ArgumentParser(description="Plot SimAI EndToEnd CSV")
    parser.add_argument(
        "-i",
        "--input",
        default="ncclFlowModel_EndToEnd.csv",
        help="Path to ncclFlowModel_EndToEnd.csv",
    )
    parser.add_argument(
        "-o",
        "--outdir",
        default="results/endtoend_plots",
        help="Directory for output plots",
    )
    parser.add_argument(
        "--top-n",
        type=int,
        default=20,
        help="Number of top layers to plot",
    )
    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    header, rows = parse_endtoend(args.input)
    idx = header_index(header)
    agg = aggregate(rows, idx)

    plot_top_exposed(agg, args.outdir, args.top_n)
    plot_breakdown(agg, args.outdir)
    plot_hot_layers(agg, args.outdir, args.top_n)

    print(f"Parsed {len(rows)} layer rows from: {args.input}")
    print(f"Plots written to: {args.outdir}")


if __name__ == "__main__":
    main()
