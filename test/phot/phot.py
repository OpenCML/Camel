"""
Mock phot module for testing the Camel phot pipeline.
Uses native Python types only, no numpy dependency.
"""

import random


def config(plot: bool = False):
    """Initialize phot configuration. plot=True would enable matplotlib backend."""
    pass


def gen_bits(num_bits: int, bits_per_symbol: int):
    """
    Generate random bits for dual-polarization transmission.
    Returns [bits_x, bits_y] as two lists of ints.
    """
    total = num_bits * 2
    bits = [random.randint(0, 1) for _ in range(total)]
    n = num_bits
    return [bits[:n], bits[n:]]


def modulation(bits, bits_per_symbol: int):
    """
    Modulate bits to complex symbols (PAM-M simulation).
    bits: [bits_x, bits_y] - list or tuple
    Returns [x_complex, y_complex] as lists of complex.
    """
    bits = list(bits) if not isinstance(bits, list) else bits
    if len(bits) < 2:
        raise ValueError(f"phot.modulation: bits must have 2 elements [bits_x, bits_y], got len={len(bits)}")
    bits_x = [int(b) for b in bits[0]]
    bits_y = [int(b) for b in bits[1]]

    M = 2 ** bits_per_symbol

    def bits_to_symbols(b):
        syms = []
        n_syms = len(b) // bits_per_symbol
        for k in range(n_syms):
            i = k * bits_per_symbol
            chunk = b[i : i + bits_per_symbol]
            sym = sum(chunk[j] << (bits_per_symbol - 1 - j) for j in range(bits_per_symbol))
            val = (2 * sym - M + 1) / (M - 1) if M > 1 else float(sym)
            syms.append(complex(val, 0))
        return syms

    return [bits_to_symbols(bits_x), bits_to_symbols(bits_y)]


def up_sample(signals, up_sampling_factor: int):
    """
    Up-sample signals by repeating each symbol up_sampling_factor times.
    signals: [x_complex, y_complex]
    Returns [x_upsampled, y_upsampled].
    """
    x, y = signals[0], signals[1]
    x = [c for c in x for _ in range(up_sampling_factor)]
    y = [c for c in y for _ in range(up_sampling_factor)]
    return [x, y]


def pulse_shaper(signals, up_sampling_factor: int, roll_off: float, total_baud: float):
    """
    Apply RRC pulse shaping (simplified: just return upsampled signals).
    """
    return up_sample(signals, up_sampling_factor)


def constellation_diagram(signals, is_plot: bool, isdata: bool):
    """Optionally plot constellation. Mock: no-op."""
    pass
