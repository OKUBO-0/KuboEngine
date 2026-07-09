#!/usr/bin/env python3
"""Aggregate collision_telemetry.csv and emit a summary CSV plus SVG chart."""

from __future__ import annotations

import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path


def percentile(values: list[float], ratio: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    position = (len(ordered) - 1) * ratio
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def load(path: Path, warmup_frames: int) -> dict[tuple[str, int], list[dict[str, float]]]:
    groups: dict[tuple[str, int], list[dict[str, float]]] = defaultdict(list)
    with path.open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            frame = int(row["frame"])
            if frame < warmup_frames:
                continue
            mode = row["mode"]
            enemy_count = int(row["enemyCount"])
            groups[(mode, enemy_count)].append({
                "collision_ms": float(row["collisionMilliseconds"]),
                "build_ms": float(row["spatialBuildMilliseconds"]),
                "separation_ms": float(row["separationMilliseconds"]),
                "reduction": float(row["candidateReductionPercent"]),
                "nearby": float(row["nearbyCandidateCount"]),
                "baseline": float(row["bruteForceCandidateCount"]),
            })
    return groups


def summarize(groups):
    rows = []
    for (mode, enemies), samples in sorted(groups.items()):
        collision = [sample["collision_ms"] for sample in samples]
        total = [
            sample["collision_ms"] + sample["build_ms"] + sample["separation_ms"]
            for sample in samples
        ]
        rows.append({
            "mode": mode,
            "enemyCount": enemies,
            "sampleCount": len(samples),
            "collisionMedianMs": statistics.median(collision),
            "collisionP95Ms": percentile(collision, 0.95),
            "totalMedianMs": statistics.median(total),
            "totalP95Ms": percentile(total, 0.95),
            "candidateReductionMedianPercent": statistics.median(
                sample["reduction"] for sample in samples),
            "nearbyCandidatesMedian": statistics.median(
                sample["nearby"] for sample in samples),
            "bruteForceCandidatesMedian": statistics.median(
                sample["baseline"] for sample in samples),
        })
    return rows


def write_csv(path: Path, rows) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = list(rows[0].keys()) if rows else [
        "mode", "enemyCount", "sampleCount", "collisionMedianMs",
        "collisionP95Ms", "totalMedianMs", "totalP95Ms",
        "candidateReductionMedianPercent", "nearbyCandidatesMedian",
        "bruteForceCandidatesMedian",
    ]
    with path.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def write_svg(path: Path, rows) -> None:
    width, height = 960, 540
    margin_left, margin_right, margin_top, margin_bottom = 90, 30, 50, 80
    chart_w = width - margin_left - margin_right
    chart_h = height - margin_top - margin_bottom
    points_by_mode = defaultdict(list)
    for row in rows:
        points_by_mode[row["mode"]].append(
            (row["enemyCount"], row["totalMedianMs"]))
    all_points = [point for values in points_by_mode.values() for point in values]
    max_x = max((point[0] for point in all_points), default=1)
    max_y = max((point[1] for point in all_points), default=1.0) or 1.0
    colors = {"spatial_grid": "#39735D", "brute_force": "#B46A32"}

    def x(value):
        return margin_left + chart_w * value / max_x

    def y(value):
        return margin_top + chart_h * (1.0 - value / max_y)

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<style>text{font-family:"Noto Sans JP",sans-serif;fill:#1C252B}</style>',
        '<text x="90" y="28" font-size="20" font-weight="700">Broad-phase CPU time comparison</text>',
        f'<line x1="{margin_left}" y1="{margin_top}" x2="{margin_left}" y2="{margin_top + chart_h}" stroke="#202020"/>',
        f'<line x1="{margin_left}" y1="{margin_top + chart_h}" x2="{margin_left + chart_w}" y2="{margin_top + chart_h}" stroke="#202020"/>',
    ]
    for tick in range(6):
        value = max_y * tick / 5
        py = y(value)
        parts.append(f'<line x1="{margin_left}" y1="{py:.1f}" x2="{margin_left + chart_w}" y2="{py:.1f}" stroke="#D8DEE2"/>')
        parts.append(f'<text x="{margin_left - 10}" y="{py + 5:.1f}" text-anchor="end" font-size="12">{value:.3f}</text>')
    for mode, points in sorted(points_by_mode.items()):
        points.sort()
        color = colors.get(mode, "#27678A")
        coordinates = " ".join(f"{x(px):.1f},{y(py):.1f}" for px, py in points)
        parts.append(f'<polyline points="{coordinates}" fill="none" stroke="{color}" stroke-width="3"/>')
        for px, py in points:
            parts.append(f'<circle cx="{x(px):.1f}" cy="{y(py):.1f}" r="4" fill="{color}"/>')
        legend_y = margin_top + 18 + 24 * len([p for p in parts if "legend" in p])
        parts.append(f'<g class="legend"><rect x="{width - 220}" y="{legend_y - 12}" width="18" height="4" fill="{color}"/><text x="{width - 195}" y="{legend_y - 6}" font-size="13">{mode}</text></g>')
    parts.extend([
        f'<text x="{margin_left + chart_w / 2}" y="{height - 22}" text-anchor="middle" font-size="14">Active enemy count</text>',
        f'<text x="20" y="{margin_top + chart_h / 2}" transform="rotate(-90 20 {margin_top + chart_h / 2})" text-anchor="middle" font-size="14">Median build + collision + separation (ms)</text>',
        '</svg>',
    ])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(parts), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--out-dir", type=Path, default=Path("output/telemetry"))
    parser.add_argument("--warmup-frames", type=int, default=300)
    args = parser.parse_args()
    if not args.input.exists():
        parser.error(f"input CSV not found: {args.input}")
    groups = load(args.input, args.warmup_frames)
    rows = summarize(groups)
    write_csv(args.out_dir / "collision_summary.csv", rows)
    write_svg(args.out_dir / "collision_cpu_comparison.svg", rows)
    print(f"groups={len(rows)}")
    print(args.out_dir / "collision_summary.csv")
    print(args.out_dir / "collision_cpu_comparison.svg")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
