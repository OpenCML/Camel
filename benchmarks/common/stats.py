"""Statistics for repeated benchmark trials (pure Python, no NumPy needed).

``run.py --trials N`` measures every (model, config) N times in shuffled
round-robin order. Each trial yields one per-trial median latency; this module
aggregates those into a summary row:

  median_ms         median of the trial medians
  ci_low/high_ms    percentile-bootstrap 95% CI of that median (resampling trials)
  speedup           baseline median / config median (> 1 means faster than baseline)
  speedup_ci_*      bootstrap CI of the speedup; paired by trial (round) when the
                    config and the baseline succeeded in the same rounds

All randomness goes through ``random.Random(seed)`` so summaries are reproducible.

Self-test: ``python common/stats.py --self-test``.
"""

from __future__ import annotations

import argparse
import math
import random
import sys
from typing import Callable, Dict, Iterable, List, Optional, Sequence, Tuple

DEFAULT_RESAMPLES = 2000
DEFAULT_CONFIDENCE = 0.95

SUMMARY_COLUMNS = [
    "model",
    "config",
    "status",
    "trials",
    "failures",
    "threads",
    "warmup",
    "reps",
    "batch",
    "median_ms",
    "ci_low_ms",
    "ci_high_ms",
    "min_ms",
    "max_ms",
    "peak_rss_mb",
    "export_s",
    "agree",
    "baseline",
    "speedup",
    "speedup_ci_low",
    "speedup_ci_high",
    "notes",
]


# ---------------------------------------------------------------------------
# Pure estimators
# ---------------------------------------------------------------------------


def median(values: Sequence[float]) -> float:
    """Median of a non-empty sequence (mean of the two middle values if even)."""
    if not values:
        raise ValueError("median of an empty sequence")
    s = sorted(values)
    n = len(s)
    mid = n // 2
    return float(s[mid]) if n % 2 else (s[mid - 1] + s[mid]) / 2.0


def percentile(values: Sequence[float], q: float) -> float:
    """Linear-interpolated percentile, ``q`` in [0, 100] (NumPy's default method)."""
    if not values:
        raise ValueError("percentile of an empty sequence")
    if not 0.0 <= q <= 100.0:
        raise ValueError(f"q must be in [0, 100], got {q}")
    s = sorted(values)
    pos = (len(s) - 1) * q / 100.0
    lo = math.floor(pos)
    hi = min(lo + 1, len(s) - 1)
    return float(s[lo] + (s[hi] - s[lo]) * (pos - lo))


def _interval(samples: List[float], confidence: float) -> Tuple[float, float]:
    alpha = (1.0 - confidence) / 2.0
    return percentile(samples, 100.0 * alpha), percentile(samples, 100.0 * (1.0 - alpha))


def bootstrap_ci(
    values: Sequence[float],
    stat: Callable[[Sequence[float]], float] = median,
    resamples: int = DEFAULT_RESAMPLES,
    confidence: float = DEFAULT_CONFIDENCE,
    seed: int = 0,
) -> Tuple[float, float]:
    """Percentile-bootstrap confidence interval of ``stat(values)``.

    With a single value the interval collapses to that value. Note that with very
    few trials (e.g. 3) the interval cannot extend beyond the observed range.
    """
    if not values:
        raise ValueError("bootstrap_ci of an empty sequence")
    if len(values) == 1:
        v = float(stat(values))
        return v, v
    rng = random.Random(seed)
    n = len(values)
    samples = [stat([values[rng.randrange(n)] for _ in range(n)]) for _ in range(resamples)]
    return _interval(samples, confidence)


def ratio_ci(
    numerators: Sequence[float],
    denominators: Sequence[float],
    stat: Callable[[Sequence[float]], float] = median,
    resamples: int = DEFAULT_RESAMPLES,
    confidence: float = DEFAULT_CONFIDENCE,
    seed: int = 0,
    paired: bool = False,
) -> Tuple[float, float, float]:
    """Point estimate and bootstrap CI of ``stat(numerators) / stat(denominators)``.

    ``paired=True`` (requires equal lengths) resamples index positions jointly, which
    keeps measurements taken in the same round together and cancels shared drift.
    Otherwise both samples are resampled independently. Returns ``(ratio, lo, hi)``.
    """
    if not numerators or not denominators:
        raise ValueError("ratio_ci needs non-empty samples")
    if paired and len(numerators) != len(denominators):
        raise ValueError("paired ratio_ci needs samples of equal length")
    point = stat(numerators) / stat(denominators)
    if len(numerators) == 1 and len(denominators) == 1:
        return point, point, point
    rng = random.Random(seed)
    n, m = len(numerators), len(denominators)
    samples: List[float] = []
    for _ in range(resamples):
        if paired:
            idx = [rng.randrange(n) for _ in range(n)]
            num = [numerators[i] for i in idx]
            den = [denominators[i] for i in idx]
        else:
            num = [numerators[rng.randrange(n)] for _ in range(n)]
            den = [denominators[rng.randrange(m)] for _ in range(m)]
        samples.append(stat(num) / stat(den))
    lo, hi = _interval(samples, confidence)
    return point, lo, hi


# ---------------------------------------------------------------------------
# Summaries of per-trial rows
# ---------------------------------------------------------------------------


def _float(v) -> Optional[float]:
    if v is None or v == "":
        return None
    try:
        f = float(v)
    except (TypeError, ValueError):
        return None
    return None if math.isnan(f) else f


def _agree(values: Iterable[str]) -> str:
    vals = [v for v in values if v]
    if not vals:
        return ""
    if "NO" in vals:
        return "NO"
    if all(v == "yes" for v in vals):
        return "yes"
    if all(v == "n/a" for v in vals):
        return "n/a"
    return "mixed"


def summarize(
    rows: Sequence[Dict[str, object]],
    baseline: str = "torch_eager",
    resamples: int = DEFAULT_RESAMPLES,
    seed: int = 0,
) -> List[Dict[str, object]]:
    """Aggregate per-trial rows (run.py raw CSV rows) into one row per (model, config).

    Rows without a ``trial`` field count as trial 0 (a plain single-run CSV).
    Order of the output follows first appearance in ``rows``.
    """
    groups: Dict[Tuple[str, str], List[Dict[str, object]]] = {}
    for r in rows:
        groups.setdefault((str(r["model"]), str(r["config"])), []).append(r)

    # Trial medians per (model, config), keyed by trial index for pairing.
    medians: Dict[Tuple[str, str], Dict[str, float]] = {}
    for key, rs in groups.items():
        medians[key] = {
            str(r.get("trial") or 0): v
            for r in rs
            if r.get("status") == "ok" and (v := _float(r.get("latency_median_ms"))) is not None
        }

    out: List[Dict[str, object]] = []
    for (model, config), rs in groups.items():
        by_trial = medians[(model, config)]
        vals = list(by_trial.values())
        failures = sum(1 for r in rs if r.get("status") == "failed")
        skipped = sum(1 for r in rs if r.get("status") == "skipped")
        first = rs[0]
        row: Dict[str, object] = {
            "model": model,
            "config": config,
            "trials": len(vals),
            "failures": failures,
            "baseline": baseline,
        }
        for k in ("threads", "warmup", "reps", "batch"):
            row[k] = first.get(k)
        export = [e for r in rs if (e := _float(r.get("export_s"))) is not None]
        row["export_s"] = export[0] if export else None

        reasons = sorted({str(r.get("notes")) for r in rs if r.get("status") != "ok" and r.get("notes")})
        if not vals:
            row["status"] = "skipped" if skipped == len(rs) else "failed"
            row["notes"] = "; ".join(reasons)
            out.append(row)
            continue

        row["status"] = "ok" if failures == 0 and skipped == 0 else "partial"
        seed_key = seed + sum(map(ord, model + "/" + config))
        row["median_ms"] = median(vals)
        row["ci_low_ms"], row["ci_high_ms"] = bootstrap_ci(vals, resamples=resamples, seed=seed_key)
        row["min_ms"], row["max_ms"] = min(vals), max(vals)
        rss = [x for r in rs if r.get("status") == "ok" and (x := _float(r.get("peak_rss_mb"))) is not None]
        row["peak_rss_mb"] = sum(rss) / len(rss) if rss else None
        row["agree"] = _agree(str(r.get("agree") or "") for r in rs if r.get("status") == "ok")

        base = medians.get((model, baseline), {})
        if base:
            common = sorted(set(base) & set(by_trial))
            paired = len(common) == len(base) == len(by_trial)
            num = [base[t] for t in common] if paired else list(base.values())
            den = [by_trial[t] for t in common] if paired else vals
            row["speedup"], row["speedup_ci_low"], row["speedup_ci_high"] = ratio_ci(
                num, den, resamples=resamples, seed=seed_key, paired=paired
            )
        notes = []
        if failures or skipped:
            notes.append(f"{failures + skipped}/{len(rs)} trials not ok")
        notes.extend(reasons)
        row["notes"] = "; ".join(notes)
        out.append(row)
    return out


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------


def _self_test() -> None:
    assert median([3.0]) == 3.0
    assert median([3.0, 1.0, 2.0]) == 2.0
    assert median([4.0, 1.0, 3.0, 2.0]) == 2.5
    assert percentile([1.0, 2.0, 3.0, 4.0, 5.0], 50) == 3.0
    assert abs(percentile([1.0, 2.0, 3.0, 4.0], 25) - 1.75) < 1e-12
    assert percentile([7.0], 90) == 7.0

    # CI brackets the point estimate, is deterministic, and stays inside the data range.
    data = [10.0, 11.0, 9.5, 10.2, 10.8, 9.9, 10.1, 30.0]
    lo, hi = bootstrap_ci(data, seed=1)
    assert lo <= median(data) <= hi, (lo, hi)
    assert min(data) <= lo and hi <= max(data)
    assert (lo, hi) == bootstrap_ci(data, seed=1)
    assert bootstrap_ci([5.0]) == (5.0, 5.0)
    # Constant data: zero-width interval.
    assert bootstrap_ci([2.0, 2.0, 2.0]) == (2.0, 2.0)

    # Ratio: exact when samples are constant; paired resampling cancels shared drift.
    assert ratio_ci([4.0, 4.0], [2.0, 2.0]) == (2.0, 2.0, 2.0)
    base = [1.0, 2.0, 3.0, 4.0, 5.0]
    cfg = [0.5, 1.0, 1.5, 2.0, 2.5]  # always exactly 2x faster in the same round
    r, lo, hi = ratio_ci(base, cfg, paired=True)
    assert abs(r - 2.0) < 1e-12 and abs(lo - 2.0) < 1e-12 and abs(hi - 2.0) < 1e-12
    r, lo, hi = ratio_ci(base, cfg, paired=False)
    assert abs(r - 2.0) < 1e-12 and lo < 2.0 < hi
    try:
        ratio_ci([1.0], [1.0, 2.0], paired=True)
    except ValueError:
        pass
    else:
        raise AssertionError("paired ratio_ci must reject unequal lengths")

    # summarize: failures are counted, not fatal; speedup uses the baseline.
    rows = []
    for t in (1, 2, 3):
        rows.append({"model": "m", "config": "b", "trial": t, "status": "ok", "latency_median_ms": 2.0 * t,
                     "peak_rss_mb": 100.0, "agree": "yes"})
        rows.append({"model": "m", "config": "c", "trial": t, "status": "ok" if t != 2 else "failed",
                     "latency_median_ms": 1.0 * t if t != 2 else None, "peak_rss_mb": 50.0, "agree": "yes",
                     "notes": "boom" if t == 2 else ""})
        rows.append({"model": "m", "config": "s", "trial": t, "status": "skipped", "notes": "missing"})
    summ = {r["config"]: r for r in summarize(rows, baseline="b")}
    assert summ["b"]["status"] == "ok" and summ["b"]["median_ms"] == 4.0 and summ["b"]["speedup"] == 1.0
    c = summ["c"]
    assert c["status"] == "partial" and c["trials"] == 2 and c["failures"] == 1, c
    assert c["median_ms"] == 2.0 and "1/3 trials not ok" in c["notes"] and "boom" in c["notes"]
    assert c["speedup"] == 2.0  # median(2,4,6)=4 / median(1,3)=2 (unpaired: trials differ)
    assert summ["s"]["status"] == "skipped" and summ["s"]["trials"] == 0
    print("stats self-test: OK")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-test", action="store_true", help="run the built-in checks and exit")
    args = ap.parse_args()
    if args.self_test:
        _self_test()
        return 0
    ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
