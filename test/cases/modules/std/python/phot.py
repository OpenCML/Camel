"""
Local phot stub used by Camel tests so the wrapper can run without an external phot package.
"""

from __future__ import annotations

import math
from pathlib import Path

_PLOT_ENABLED = False


def config(plot: bool) -> None:
    global _PLOT_ENABLED
    _PLOT_ENABLED = bool(plot)


def gen_bits(num_bits: int, bits_per_symbol: int):
    half = max(1, num_bits // 2)
    x = [(i + bits_per_symbol) % 2 for i in range(half)]
    y = [((i // 2) + bits_per_symbol + 1) % 2 for i in range(half)]
    return (x, y)


def modulation(bits, bits_per_symbol: int):
    scale = max(1.0, float(bits_per_symbol))
    return [_bits_to_symbols(bits[0], scale), _bits_to_symbols(bits[1], scale)]


def up_sample(signals, up_sampling_factor: int):
    factor = max(1, int(up_sampling_factor))
    return [_repeat_signal(signals[0], factor), _repeat_signal(signals[1], factor)]


def pulse_shaper(signals, up_sampling_factor: int, roll_off: float, total_baud: float):
    factor = max(1, int(up_sampling_factor))
    kernel = [1.0 / factor for _ in range(factor)]
    return [_smooth_signal(signals[0], kernel), _smooth_signal(signals[1], kernel)]


def constellation_diagram(signals, is_plot: bool = True, isdata: bool = False):
    if not (_PLOT_ENABLED and is_plot):
        return {"points": sum(len(ch) for ch in signals)}
    out = Path("test/tmp/phot/constellation_diagram.txt")
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8") as f:
        for idx, channel in enumerate(signals):
            f.write(f"channel {idx}\n")
            for value in channel[:32]:
                f.write(f"{value.real:.6f},{value.imag:.6f}\n")
    return {"path": str(out), "isdata": bool(isdata)}


def _bits_to_symbols(bits, scale: float):
    symbols = []
    for idx in range(0, len(bits), 2):
        b0 = bits[idx]
        b1 = bits[idx + 1] if idx + 1 < len(bits) else 0
        real = (-1.0 if b0 else 1.0) / scale
        imag = (-1.0 if b1 else 1.0) / scale
        symbols.append(complex(real, imag))
    return symbols


def _repeat_signal(channel, factor: int):
    result = []
    for value in channel:
        result.extend([value] * factor)
    return result


def _smooth_signal(channel, kernel):
    if not channel:
        return []
    radius = len(kernel) // 2
    result = []
    for idx in range(len(channel)):
        acc = 0j
        weight_sum = 0.0
        for k, weight in enumerate(kernel):
            src = idx + k - radius
            if 0 <= src < len(channel):
                acc += channel[src] * weight
                weight_sum += weight
        result.append(acc / weight_sum if weight_sum else channel[idx])
    return result
