"""Paper report generator: tables and figures from benchmark summary CSVs.

Inputs are one or more ``*_summary.csv`` files written by ``run.py --trials N``
(a plain per-run CSV from ``run.py`` is also accepted and summarized as one
trial per config). When several inputs contain the same (model, config), the
later file wins, so partial re-runs can be layered over a full run.

Outputs (default directory ``results/report/``):
  latency.md   median latency [95% CI] in ms, configs grouped by framework, best per model in bold
  latency.tex  the same table for LaTeX (booktabs)
  memory.md    mean peak RSS in MB
  export.md    ONNX export time in seconds per exporter
  latency.svg  grouped bar chart, log-scale y, CI error bars
  speedup.svg  speedup vs the summary's baseline config, log-scale y, CI error bars
  *.png        the two charts again, only if matplotlib is installed

Config display names and groups come from common/configs.py.

Example:
  python report.py results/paper_summary.csv --out results/report
"""

from __future__ import annotations

import argparse
import csv
import html
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

BENCH_ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_ROOT))

from common import configs as cfgs  # noqa: E402
from common import stats  # noqa: E402
from common.models import MODELS  # noqa: E402

# Group hues: validated categorical slots (blue, aqua, violet, orange) of the
# colorblind-safe reference palette; configs inside a group are shades of it.
GROUP_COLORS: Dict[str, str] = {
    "PyTorch": "#2a78d6",
    "TensorFlow": "#1baf7a",
    "ONNX Runtime": "#4a3aa7",
    "Camel": "#eb6834",
}
INK = "#0b0b0b"
INK_2 = "#52514e"
GRID = "#e4e3df"
SURFACE = "#fcfcfb"
FONT = "Helvetica, Arial, sans-serif"


# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------


@dataclass
class Cell:
    """One summarized (model, config) measurement."""

    status: str
    trials: int
    failures: int
    median: Optional[float]
    lo: Optional[float]
    hi: Optional[float]
    rss: Optional[float]
    export_s: Optional[float]
    speedup: Optional[float]
    sp_lo: Optional[float]
    sp_hi: Optional[float]
    baseline: str
    reps: str
    warmup: str
    threads: str


def _f(v) -> Optional[float]:
    if v is None or v == "":
        return None
    try:
        x = float(v)
    except ValueError:
        return None
    return None if math.isnan(x) else x


def _i(v) -> int:
    x = _f(v)
    return int(x) if x is not None else 0


def load(paths: Sequence[Path], baseline: str) -> Dict[Tuple[str, str], Cell]:
    cells: Dict[Tuple[str, str], Cell] = {}
    for path in paths:
        with path.open(newline="") as f:
            rows = list(csv.DictReader(f))
        if rows and "median_ms" not in rows[0]:
            if "latency_median_ms" not in rows[0]:
                raise SystemExit(f"{path}: neither a summary nor a run.py CSV")
            rows = stats.summarize(rows, baseline=baseline)
        for r in rows:
            if r["config"] not in cfgs.CONFIGS:
                print(f"[report] {path.name}: ignoring unknown config {r['config']!r}", file=sys.stderr)
                continue
            cells[(r["model"], r["config"])] = Cell(
                status=str(r.get("status") or ""),
                trials=_i(r.get("trials")),
                failures=_i(r.get("failures")),
                median=_f(r.get("median_ms")),
                lo=_f(r.get("ci_low_ms")),
                hi=_f(r.get("ci_high_ms")),
                rss=_f(r.get("peak_rss_mb")),
                export_s=_f(r.get("export_s")),
                speedup=_f(r.get("speedup")),
                sp_lo=_f(r.get("speedup_ci_low")),
                sp_hi=_f(r.get("speedup_ci_high")),
                baseline=str(r.get("baseline") or ""),
                reps=str(r.get("reps") or ""),
                warmup=str(r.get("warmup") or ""),
                threads=str(r.get("threads") or ""),
            )
    return cells


def ordered(cells: Dict[Tuple[str, str], Cell]) -> Tuple[List[str], List[Tuple[str, List[str]]]]:
    """Models in registry order and configs grouped by ``Config.group_label``."""
    present_models = {m for m, _ in cells}
    models = [m for m in MODELS if m in present_models] + sorted(present_models - set(MODELS))
    present = {c for _, c in cells}
    groups: Dict[str, List[str]] = {}
    for name, cfg in cfgs.CONFIGS.items():
        if name in present:
            groups.setdefault(cfg.group_label, []).append(name)
    order = cfgs.GROUP_ORDER + sorted(set(groups) - set(cfgs.GROUP_ORDER))
    return models, [(g, groups[g]) for g in order if g in groups]


# ---------------------------------------------------------------------------
# Formatting helpers
# ---------------------------------------------------------------------------


def num(v: float) -> str:
    """Compact fixed-point formatting with ~3 significant digits."""
    a = abs(v)
    if a >= 100:
        return f"{v:.0f}"
    if a >= 10:
        return f"{v:.1f}"
    if a >= 1:
        return f"{v:.2f}"
    return f"{v:.3f}"


def best_per_model(cells, models, groups, attr: str) -> Dict[str, Optional[str]]:
    best: Dict[str, Optional[str]] = {}
    for m in models:
        cands = [
            (getattr(cells[(m, c)], attr), c)
            for _, cs in groups
            for c in cs
            if (m, c) in cells and getattr(cells[(m, c)], attr) is not None
        ]
        best[m] = min(cands)[1] if cands else None
    return best


def _missing(cell: Optional[Cell]) -> str:
    if cell is None:
        return "—"
    return cell.status or "—"


def _meta(cells: Dict[Tuple[str, str], Cell]) -> str:
    def uniq(attr: str) -> str:
        vals = sorted({getattr(c, attr) for c in cells.values() if getattr(c, attr)})
        return "/".join(vals) if vals else "?"

    trials = sorted({c.trials for c in cells.values() if c.trials})
    t = "/".join(map(str, trials)) if trials else "?"
    return f"threads={uniq('threads')}, warmup={uniq('warmup')}, reps={uniq('reps')}, trials={t}"


def _partial_note(cells: Dict[Tuple[str, str], Cell]) -> str:
    partial = any(c.status == "partial" for c in cells.values())
    return " † some trials failed (see the summary CSV notes)." if partial else ""


def _baseline(cells: Dict[Tuple[str, str], Cell]) -> str:
    names = sorted({c.baseline for c in cells.values() if c.baseline})
    return names[0] if len(names) == 1 else ("/".join(names) or "?")


# ---------------------------------------------------------------------------
# Tables
# ---------------------------------------------------------------------------


def _md_table(header: List[str], body: List[List[str]]) -> str:
    lines = ["| " + " | ".join(header) + " |", "|" + "|".join([":--", ":--"] + ["--:"] * (len(header) - 2)) + "|"]
    lines += ["| " + " | ".join(r) + " |" for r in body]
    return "\n".join(lines) + "\n"


def latency_md(cells, models, groups) -> str:
    best = best_per_model(cells, models, groups, "median")
    body = []
    for g, cs in groups:
        for i, c in enumerate(cs):
            row = [g if i == 0 else "", cfgs.CONFIGS[c].label]
            for m in models:
                cell = cells.get((m, c))
                if cell is None or cell.median is None:
                    row.append(_missing(cell))
                    continue
                v = num(cell.median)
                v = f"**{v}**" if best[m] == c else v
                if cell.trials > 1:
                    v += f" [{num(cell.lo)}–{num(cell.hi)}]"
                if cell.status == "partial":
                    v += " †"
                row.append(v)
            body.append(row)
    return (
        "# Inference latency (ms)\n\n"
        "Median of per-trial median latencies, [95% bootstrap CI over trials]; lower is better, "
        f"best per model in bold. {_meta(cells)}.{_partial_note(cells)}\n\n"
        + _md_table(["Framework", "Config", *models], body)
    )


def memory_md(cells, models, groups) -> str:
    best = best_per_model(cells, models, groups, "rss")
    body = []
    for g, cs in groups:
        for i, c in enumerate(cs):
            row = [g if i == 0 else "", cfgs.CONFIGS[c].label]
            for m in models:
                cell = cells.get((m, c))
                if cell is None or cell.rss is None:
                    row.append(_missing(cell))
                    continue
                v = f"{cell.rss:.0f}"
                row.append(f"**{v}**" if best[m] == c else v)
            body.append(row)
    return (
        "# Peak resident memory (MB)\n\n"
        "Mean over trials of the measuring process's peak RSS (Camel: the Camel process); "
        f"lowest per model in bold. {_meta(cells)}.\n\n" + _md_table(["Framework", "Config", *models], body)
    )


def export_rows(cells, models) -> List[Tuple[str, List[Optional[float]]]]:
    rows = []
    for exporter, label in cfgs.EXPORTERS.items():
        consumers = [n for n, c in cfgs.CONFIGS.items() if c.exporter == exporter]
        vals: List[Optional[float]] = []
        for m in models:
            v = [cells[(m, c)].export_s for c in consumers if (m, c) in cells and cells[(m, c)].export_s is not None]
            vals.append(v[0] if v else None)
        if any((m, c) in cells for m in models for c in consumers):
            rows.append((label, vals))
    return rows


def export_md(cells, models) -> str:
    body = [[label] + [num(v) if v is not None else "—" for v in vals] for label, vals in export_rows(cells, models)]
    lines = ["| " + " | ".join(["Exporter", *models]) + " |", "|:--|" + "|".join(["--:"] * len(models)) + "|"]
    lines += ["| " + " | ".join(r) + " |" for r in body]
    return (
        "# ONNX export time (s)\n\n"
        "Wall time spent inside the exporter (framework import excluded), measured once per model "
        "in a fresh process. —: export not run (e.g. `--no-export`) or not applicable.\n\n"
        + "\n".join(lines)
        + "\n"
    )


def _tex(s: str) -> str:
    for a, b in (("\\", r"\textbackslash{}"), ("_", r"\_"), ("&", r"\&"), ("%", r"\%"), ("#", r"\#")):
        s = s.replace(a, b)
    return s


def latency_tex(cells, models, groups) -> str:
    best = best_per_model(cells, models, groups, "median")
    lines = [
        "% Generated by benchmarks/report.py -- requires \\usepackage{booktabs}",
        "\\begin{table}[t]",
        "\\centering",
        "\\small",
        f"\\caption{{Inference latency in ms: median of trial medians with 95\\% bootstrap CI "
        f"({_tex(_meta(cells))}). Lower is better; best per model in bold."
        + (" $^\\dagger$Some trials failed." if _partial_note(cells) else "")
        + "}",
        "\\label{tab:latency}",
        "\\begin{tabular}{ll" + "r" * len(models) + "}",
        "\\toprule",
        "Framework & Config & " + " & ".join(_tex(m) for m in models) + " \\\\",
        "\\midrule",
    ]
    for gi, (g, cs) in enumerate(groups):
        if gi:
            lines.append("\\midrule")
        for i, c in enumerate(cs):
            row = [_tex(g) if i == 0 else "", "\\texttt{" + _tex(cfgs.CONFIGS[c].label) + "}"]
            for m in models:
                cell = cells.get((m, c))
                if cell is None or cell.median is None:
                    row.append("---" if cell is None else _tex(cell.status))
                    continue
                v = num(cell.median)
                v = f"\\textbf{{{v}}}" if best[m] == c else v
                if cell.trials > 1:
                    v += f" {{\\scriptsize[{num(cell.lo)}--{num(cell.hi)}]}}"
                if cell.status == "partial":
                    v += "$^\\dagger$"
                row.append(v)
            lines.append(" & ".join(row) + " \\\\")
    lines += ["\\bottomrule", "\\end{tabular}", "\\end{table}", ""]
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Figures (hand-written SVG; no plotting library required)
# ---------------------------------------------------------------------------


def _mix(hex_color: str, other: str, t: float) -> str:
    a = [int(hex_color[i : i + 2], 16) for i in (1, 3, 5)]
    b = [int(other[i : i + 2], 16) for i in (1, 3, 5)]
    return "#" + "".join(f"{round(x + (y - x) * t):02x}" for x, y in zip(a, b))


def config_colors() -> Dict[str, Tuple[str, str]]:
    """(fill, stroke) per config: the group hue, stepped from darker to lighter.

    Shades follow registry order within the group (not the configs present), so a
    config keeps its color across reports.
    """
    by_group: Dict[str, List[str]] = {}
    for name, cfg in cfgs.CONFIGS.items():
        by_group.setdefault(cfg.group_label, []).append(name)
    out: Dict[str, Tuple[str, str]] = {}
    for g, names in by_group.items():
        base = GROUP_COLORS.get(g, INK_2)
        stroke = _mix(base, "#000000", 0.35)
        n = len(names)
        for i, name in enumerate(names):
            t = -0.25 + 0.85 * i / max(n - 1, 1)  # -0.25 (darker) .. 0.6 (lighter)
            fill = _mix(base, "#000000", -t) if t < 0 else _mix(base, "#ffffff", t)
            out[name] = (fill, stroke)
    return out


def _log_ticks(lo: float, hi: float) -> Tuple[float, float, List[float]]:
    """Axis bounds (log10) and tick values: decades plus 2/5 when the span is short."""
    a, b = math.floor(math.log10(lo)), math.ceil(math.log10(hi))
    if a == b:
        b += 1
    mults = (1, 2, 5) if b - a <= 3 else (1,)
    ticks = [m * 10.0**e for e in range(a, b + 1) for m in mults if m * 10.0**e <= 10.0**b]
    return float(a), float(b), ticks


def _tick_label(v: float) -> str:
    return f"{v:g}" if 1e-3 <= v < 1e5 else f"{v:.0e}"


@dataclass
class Series:
    config: str
    value: float
    lo: float
    hi: float


def bar_chart_svg(
    title: str,
    y_label: str,
    models: List[str],
    series: Dict[str, List[Series]],
    groups: List[Tuple[str, List[str]]],
    ref_line: Optional[Tuple[float, str]] = None,
    unit: str = "",
) -> str:
    """Grouped bar chart on a log y axis with CI whiskers and a grouped legend."""
    colors = config_colors()
    W, H = 980, 440
    left, right, top, bottom = 72, 230, 48, 56
    pw, ph = W - left - right, H - top - bottom
    vals = [v for ss in series.values() for s in ss for v in (s.value, s.lo, s.hi) if v > 0]
    if ref_line:
        vals.append(ref_line[0])
    if not vals:
        vals = [1.0]
    a, b, ticks = _log_ticks(min(vals), max(vals))

    def y(v: float) -> float:
        return top + ph - (math.log10(max(v, 10.0**a)) - a) / (b - a) * ph

    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
        f'font-family="{FONT}" font-size="12">',
        f"<title>{html.escape(title)}</title>",
        f'<rect width="{W}" height="{H}" fill="{SURFACE}"/>',
        f'<text x="{left}" y="24" font-size="15" font-weight="600" fill="{INK}">{html.escape(title)}</text>',
    ]
    for t in ticks:
        yy = y(t)
        out.append(f'<line x1="{left}" x2="{left + pw}" y1="{yy:.1f}" y2="{yy:.1f}" stroke="{GRID}" stroke-width="1"/>')
        out.append(
            f'<text x="{left - 6}" y="{yy + 4:.1f}" text-anchor="end" fill="{INK_2}">{_tick_label(t)}</text>'
        )
    out.append(
        f'<text transform="translate(18 {top + ph / 2:.0f}) rotate(-90)" text-anchor="middle" '
        f'fill="{INK_2}">{html.escape(y_label)}</text>'
    )
    gw = pw / max(len(models), 1)
    for mi, m in enumerate(models):
        ss = series.get(m, [])
        gx = left + mi * gw
        out.append(
            f'<text x="{gx + gw / 2:.1f}" y="{top + ph + 22}" text-anchor="middle" fill="{INK}">{html.escape(m)}</text>'
        )
        if not ss:
            continue
        bw = min(22.0, (gw * 0.84) / len(ss))
        x0 = gx + (gw - bw * len(ss)) / 2
        for si, s in enumerate(ss):
            fill, stroke = colors.get(s.config, (INK_2, INK))
            x = x0 + si * bw
            yv = y(s.value)
            # Bars grow from the reference line (1x) when there is one, else from the axis.
            y_base = y(ref_line[0]) if ref_line else top + ph
            y_top, bar_h = min(yv, y_base), abs(y_base - yv)
            label = cfgs.CONFIGS[s.config].label
            tip = f"{m} / {label}: {num(s.value)}{unit} [{num(s.lo)}–{num(s.hi)}]"
            out.append(
                f'<g><title>{html.escape(tip)}</title><rect x="{x + 1:.1f}" y="{y_top:.1f}" width="{bw - 2:.1f}" '
                f'height="{bar_h:.1f}" fill="{fill}" stroke="{stroke}" stroke-width="0.8"/></g>'
            )
            if s.hi > s.lo:
                cx, y1, y2 = x + bw / 2, y(s.lo), y(s.hi)
                cap = max(2.0, bw * 0.22)
                out.append(
                    f'<path d="M{cx:.1f} {y1:.1f}V{y2:.1f}M{cx - cap:.1f} {y1:.1f}H{cx + cap:.1f}'
                    f'M{cx - cap:.1f} {y2:.1f}H{cx + cap:.1f}" stroke="{INK}" stroke-width="1.2" fill="none"/>'
                )
    if ref_line:
        ry = y(ref_line[0])
        out.append(
            f'<line x1="{left}" x2="{left + pw}" y1="{ry:.1f}" y2="{ry:.1f}" stroke="{INK}" '
            f'stroke-width="1.2" stroke-dasharray="5 4"/>'
        )
    out.append(f'<line x1="{left}" x2="{left + pw}" y1="{top + ph}" y2="{top + ph}" stroke="{INK_2}"/>')
    out.append(f'<line x1="{left}" x2="{left}" y1="{top}" y2="{top + ph}" stroke="{INK_2}"/>')

    # Legend: groups in order, configs that appear in the chart.
    shown = {s.config for ss in series.values() for s in ss}
    lx, ly = left + pw + 24, top + 4
    for g, cs in groups:
        cs = [c for c in cs if c in shown]
        if not cs:
            continue
        out.append(f'<text x="{lx}" y="{ly + 10}" font-weight="600" fill="{INK}">{html.escape(g)}</text>')
        ly += 18
        for c in cs:
            fill, stroke = colors[c]
            out.append(
                f'<rect x="{lx}" y="{ly}" width="12" height="12" rx="2" fill="{fill}" '
                f'stroke="{stroke}" stroke-width="0.8"/>'
            )
            out.append(
                f'<text x="{lx + 18}" y="{ly + 10}" fill="{INK_2}">{html.escape(cfgs.CONFIGS[c].label)}</text>'
            )
            ly += 17
        ly += 8
    if ref_line:
        out.append(
            f'<path d="M{lx} {ly + 8}H{lx + 12}" stroke="{INK}" stroke-width="1.2" stroke-dasharray="3 2"/>'
            f'<text x="{lx + 18}" y="{ly + 12}" fill="{INK_2}">{html.escape(ref_line[1])}</text>'
        )
        ly += 20
    out.append(
        f'<text x="{lx}" y="{ly + 12}" font-size="11" fill="{INK_2}">whiskers: 95% bootstrap CI</text>'
    )
    out.append("</svg>")
    return "\n".join(out) + "\n"


def latency_series(cells, models, groups) -> Dict[str, List[Series]]:
    out: Dict[str, List[Series]] = {}
    for m in models:
        out[m] = [
            Series(c, cell.median, cell.lo if cell.lo is not None else cell.median, cell.hi or cell.median)
            for _, cs in groups
            for c in cs
            if (cell := cells.get((m, c))) is not None and cell.median
        ]
    return out


def speedup_series(cells, models, groups, baseline: str) -> Dict[str, List[Series]]:
    out: Dict[str, List[Series]] = {}
    for m in models:
        out[m] = [
            Series(c, cell.speedup, cell.sp_lo or cell.speedup, cell.sp_hi or cell.speedup)
            for _, cs in groups
            for c in cs
            if c != baseline and (cell := cells.get((m, c))) is not None and cell.speedup
        ]
    return out


def maybe_png(path: Path, title: str, y_label: str, models, series, groups, ref: Optional[float]) -> bool:
    """Render the same chart with matplotlib if it is installed; returns False otherwise."""
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return False
    colors = config_colors()
    fig, ax = plt.subplots(figsize=(9.8, 4.4), dpi=200)
    order = [c for _, cs in groups for c in cs if any(s.config == c for ss in series.values() for s in ss)]
    labelled = set()
    for mi, m in enumerate(models):
        ss = series.get(m, [])
        if not ss:
            continue
        bw = min(0.12, 0.84 / len(ss))
        x0 = mi - bw * len(ss) / 2 + bw / 2
        for si, s in enumerate(ss):
            fill, stroke = colors[s.config]
            lab = cfgs.CONFIGS[s.config].label if s.config not in labelled else None
            labelled.add(s.config)
            base = ref if ref is not None else 0.0  # speedup bars grow from 1x
            ax.bar(
                x0 + si * bw, s.value - base, bw * 0.92, bottom=base, color=fill, edgecolor=stroke,
                linewidth=0.6, label=lab,
            )
            ax.errorbar(
                x0 + si * bw, s.value, yerr=[[s.value - s.lo], [s.hi - s.value]], color=INK, capsize=2, lw=0.9
            )
    if ref is not None:
        ax.axhline(ref, color=INK, lw=1, ls="--")
    ax.set_yscale("log")
    ax.set_xticks(range(len(models)), models)
    ax.set_ylabel(y_label)
    ax.set_title(title, loc="left")
    ax.grid(axis="y", color=GRID, lw=0.6)
    ax.set_axisbelow(True)
    handles, labels = ax.get_legend_handles_labels()
    rank = {cfgs.CONFIGS[c].label: i for i, c in enumerate(order)}
    idx = sorted(range(len(labels)), key=lambda i: rank[labels[i]])
    ax.legend([handles[i] for i in idx], [labels[i] for i in idx], loc="center left", bbox_to_anchor=(1.01, 0.5),
              frameon=False, fontsize=8)
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)
    return True


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="summary CSVs from run.py --trials (plain run CSVs also accepted)")
    ap.add_argument("--out", default=str(BENCH_ROOT / "results" / "report"), help="output directory")
    ap.add_argument(
        "--baseline", default="torch_eager", help="baseline when summarizing plain run CSVs (summaries keep theirs)"
    )
    args = ap.parse_args()

    paths = [Path(p) if Path(p).is_absolute() else Path.cwd() / p for p in args.inputs]
    for p in paths:
        if not p.exists():
            ap.error(f"{p} does not exist")
    cells = load(paths, args.baseline)
    if not cells:
        ap.error("no rows found in the inputs")
    models, groups = ordered(cells)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    written = []

    def write(name: str, text: str) -> None:
        (out / name).write_text(text, encoding="utf-8")
        written.append(name)

    write("latency.md", latency_md(cells, models, groups))
    write("memory.md", memory_md(cells, models, groups))
    write("export.md", export_md(cells, models))
    write("latency.tex", latency_tex(cells, models, groups))

    lat = latency_series(cells, models, groups)
    lat_title = "Inference latency per model (lower is better)"
    write("latency.svg", bar_chart_svg(lat_title, "latency (ms, log scale)", models, lat, groups, unit=" ms"))

    baseline = _baseline(cells)
    base_label = cfgs.CONFIGS[baseline].label if baseline in cfgs.CONFIGS else baseline
    sp = speedup_series(cells, models, groups, baseline)
    sp_title = f"Speedup vs {base_label}: baseline median / config median (higher is better)"
    sp_y = "speedup (×, log scale)"
    ref = (1.0, f"{base_label} = 1×")
    write("speedup.svg", bar_chart_svg(sp_title, sp_y, models, sp, groups, ref_line=ref, unit="×"))

    if maybe_png(out / "latency.png", lat_title, "latency (ms, log scale)", models, lat, groups, None):
        written.append("latency.png")
    if maybe_png(out / "speedup.png", sp_title, sp_y, models, sp, groups, 1.0):
        written.append("speedup.png")
    else:
        print("[report] matplotlib not installed: SVG figures only")
    print(f"[report] {len(cells)} (model, config) cells -> {out}: {', '.join(written)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
