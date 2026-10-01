"""Uniform runner interface shared by every benchmark configuration.

A ``Runner`` separates untimed input preparation (NumPy -> framework-native
tensor) from the timed call. ``call`` must return a NumPy array, which forces
the result to be fully materialized so asynchronous frameworks cannot hide
work outside the timed region.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable

import numpy as np


@dataclass
class Runner:
    prepare: Callable[[np.ndarray], Any]
    call: Callable[[Any], np.ndarray]

    def __call__(self, x: np.ndarray) -> np.ndarray:
        return self.call(self.prepare(x))
