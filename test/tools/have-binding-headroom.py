#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Precondition of `python_binding.session`'s timing line.

The entry asserts that handing over 64 MiB costs less than twice copying 1 MiB
(`python_binding.py`: `pass_through[64 MiB] < 2 * copying[1 MiB]`). It compares the *fixed* cost of
one call with twice the *variable* cost of a small copy, so the line can hold only where one call
costs less than twice a 1 MiB memmove. On macOS arm64 it does not: the call cost 46.0 us and twice
the 1 MiB copy 41.2 us, while the call still passes the buffer by pointer (64 MiB copied: 3109.7 us,
68x the call; the address and memory checks beside the line were green). The entry has no way to
say "this host's call is too expensive for this yardstick", so it declares the premise here and is
skipped where the premise is false.

The reading is the call's fixed cost, taken with four elements: a binding that copies at 64 MiB does
not move it, so the entry's own line stays able to fail a binding that moves the bytes.

Whether the extension module exists is **not** asked here. It is the entry's own product, and the
plan answers it with `require_artifacts`, which fails the entry and names the path; a probe can only
answer "skip", and a skip and a pass are the same number in a summary. Two questions in one command
meant the module's absence chose the weaker answer for both.

Run: python3 test/tools/have-binding-headroom.py
Exit 0 where the entry's yardstick can hold, 1 with the reading elsewhere.
"""

import ctypes
import importlib.util
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# The entry's own capsule and module, so the call measured here is the call the entry measures: the
# case is a script, and its `main` runs only under `__main__`.
CASE = ROOT / "test" / "cases" / "feat" / "bindings" / "python_binding.py"
spec = importlib.util.spec_from_file_location("python_binding_case", CASE)
case = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = case
spec.loader.exec_module(case)

try:
    rules = case.camel.load(str(case.CASES / "python_rules.cml"))
except Exception as error:  # noqa: BLE001 -- the reason is the reading, not a traceback
    print(f"the module the entry calls does not load: {type(error).__name__}: {error}")
    sys.exit(1)


def fastest(run, calls):
    """The smallest of `calls` single calls, the reading `fastest` in the entry takes."""
    best = float("inf")
    for _ in range(calls):
        started = time.perf_counter()
        run()
        best = min(best, time.perf_counter() - started)
    return best


values = (ctypes.c_float * 4)(1.0, 2.0, 3.0, 4.0)


def one_call():
    capsule, _ = case.float32_capsule(values)
    rules.session.call("python_rules", "identity", (capsule,))


call_us = fastest(one_call, 200) * 1e6

MIB = 1 << 20
source = (ctypes.c_float * (MIB // 4))()
target = (ctypes.c_float * (MIB // 4))()


def copy_one_mib():
    ctypes.memmove(target, source, MIB)


copy_us = fastest(copy_one_mib, 200) * 1e6

if call_us < 2 * copy_us:
    sys.exit(0)

# The 64 MiB copy the entry's line is measured against, for the reason: it is what still shows the
# call passes the buffer by pointer on the host that reds the line.
big = 64 << 20
big_source = (ctypes.c_float * (big // 4))()
big_target = (ctypes.c_float * (big // 4))()
copy_us_64 = fastest(lambda: ctypes.memmove(big_target, big_source, big), 10) * 1e6
print(
    f"one call costs {call_us:.1f} us and twice a 1 MiB memmove {2 * copy_us:.1f} us: the entry's "
    f"64 MiB line compares those two and cannot hold on this host even when the call passes the "
    f"buffer by pointer (64 MiB copied in {copy_us_64:.1f} us, "
    f"{copy_us_64 / call_us:.0f}x the call)"
)
sys.exit(1)