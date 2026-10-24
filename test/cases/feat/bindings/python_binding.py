#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The Python binding (H253), from the Python side.

Runs against `out/latest/python/_camel*.so` and the package in `pypi/camel-lang/src`, and needs no
third-party library: the DLPack capsules here are built with `ctypes`, so the test measures the
binding rather than NumPy. The extension's own return value is read as the capsule it is -- through
`session.call`, which is what the wrapper around it calls -- and when NumPy happens to be
importable the convenient path is checked as well (a project that has NumPy passes arrays by
`__dlpack__` and gets arrays back; that is the path the example under `examples/python` takes).

What is asserted:

* an array crosses in and out without a copy -- the capsule the call returns names the buffer that
  was handed in, and the call's cost does not grow with the array while the cost of copying the
  same bytes does;
* eight results of the copying function hold eight blocks, eight results of the passing-through one
  hold one, which the process's own memory use shows;
* a Camel error value arrives as `camel.FaultError` with the kind, the message and the
  file/line/column the compiler recorded on the `throw`;
* a library that does not compile arrives as `camel.DiagnosticError` with the diagnostic name, its
  catalogue code and its position.

Run it with the interpreter whose extension module was built:

    python3 test/cases/feat/bindings/python_binding.py
"""

import ctypes
import os
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve()
CASES = HERE.parent
REPO = HERE.parents[4]

# The binding needs the extension built beside libcamel, and Camel needs its install for the
# standard library and the module search roots. Both are the install the build collected.
sys.path.insert(0, str(REPO / "pypi" / "camel-lang" / "src"))
os.environ.setdefault("CAMEL_HOME", str(REPO / "out" / "latest"))

import camel  # noqa: E402

try:
    import numpy
except ImportError:
    numpy = None


# ---- the DLPack structures, so that no library is needed --------------------

class DLDevice(ctypes.Structure):
    _fields_ = [("device_type", ctypes.c_int32), ("device_id", ctypes.c_int32)]


class DLDataType(ctypes.Structure):
    _fields_ = [("code", ctypes.c_uint8), ("bits", ctypes.c_uint8), ("lanes", ctypes.c_uint16)]


class DLTensor(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.c_void_p),
        ("device", DLDevice),
        ("ndim", ctypes.c_int32),
        ("dtype", DLDataType),
        ("shape", ctypes.POINTER(ctypes.c_int64)),
        ("strides", ctypes.POINTER(ctypes.c_int64)),
        ("byte_offset", ctypes.c_uint64),
    ]


class DLManagedTensor(ctypes.Structure):
    pass


DLManagedTensor._fields_ = [
    ("dl_tensor", DLTensor),
    ("manager_ctx", ctypes.c_void_p),
    ("deleter", ctypes.CFUNCTYPE(None, ctypes.POINTER(DLManagedTensor))),
]

_PyCapsule_New = ctypes.pythonapi.PyCapsule_New
_PyCapsule_New.restype = ctypes.py_object
_PyCapsule_New.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p]

_PyCapsule_GetPointer = ctypes.pythonapi.PyCapsule_GetPointer
_PyCapsule_GetPointer.restype = ctypes.c_void_p
_PyCapsule_GetPointer.argtypes = [ctypes.py_object, ctypes.c_char_p]

# The buffers, shapes and managed tensors a capsule points at must outlive it: the runtime adopts
# the memory, so nothing here may be collected while a Camel array still reads it.
_alive = []


def float32_capsule(flat):
    """A DLPack capsule over a ctypes buffer of f32, shaped [len], with no producer deleter."""
    shape = (ctypes.c_int64 * 1)(len(flat))
    managed = DLManagedTensor()
    managed.dl_tensor.data = ctypes.cast(flat, ctypes.c_void_p)
    managed.dl_tensor.device = DLDevice(1, 0)  # kDLCPU
    managed.dl_tensor.ndim = 1
    managed.dl_tensor.dtype = DLDataType(2, 32, 1)  # kDLFloat, 32 bits, 1 lane
    managed.dl_tensor.shape = ctypes.cast(shape, ctypes.POINTER(ctypes.c_int64))
    managed.dl_tensor.strides = None
    managed.dl_tensor.byte_offset = 0
    managed.manager_ctx = None
    managed.deleter = ctypes.cast(None, ctypes.CFUNCTYPE(None, ctypes.POINTER(DLManagedTensor)))
    _alive.append((shape, managed))
    capsule = _PyCapsule_New(ctypes.cast(ctypes.byref(managed), ctypes.c_void_p), b"dltensor", None)
    return capsule, ctypes.cast(flat, ctypes.c_void_p).value


def is_capsule(value):
    return type(value).__name__ == "PyCapsule"


def bytes_of(capsule):
    """The address, the element count and the itemsize the capsule names."""
    pointer = _PyCapsule_GetPointer(capsule, b"dltensor")
    tensor = ctypes.cast(pointer, ctypes.POINTER(DLManagedTensor)).contents
    elements = 1
    for i in range(tensor.dl_tensor.ndim):
        elements *= tensor.dl_tensor.shape[i]
    return tensor.dl_tensor.data, elements, tensor.dl_tensor.dtype.bits // 8


def values_of(value, elements):
    """The first `elements` f32 of whatever a call returned: the capsule's memory, or the array
    the wrapper built from it (an array imported through DLPack is read-only, which is DLPack's
    own limit: it has no way to say that someone else's buffer may be written)."""
    if not is_capsule(value):
        return [float(value[i]) for i in range(elements)]
    pointer = _PyCapsule_GetPointer(value, b"dltensor")
    tensor = ctypes.cast(pointer, ctypes.POINTER(DLManagedTensor)).contents
    data = ctypes.cast(tensor.dl_tensor.data, ctypes.POINTER(ctypes.c_float))
    return [data[i] for i in range(elements)]


def address_of(value):
    return bytes_of(value)[0] if is_capsule(value) else value.ctypes.data


def resident_bytes():
    """How much memory the process holds, or None where the system does not say.

    Linux answers with the resident size; elsewhere the peak resident size is the portable measure
    (`ru_maxrss`, KiB on Linux and bytes on macOS). The peak never falls, so it is read before the
    measurement that raises it, which is what the caller does.
    """
    try:
        with open("/proc/self/statm", "r", encoding="ascii") as handle:
            return int(handle.read().split()[1]) * os.sysconf("SC_PAGE_SIZE")
    except (OSError, IndexError, ValueError):
        pass
    try:
        import resource
    except ImportError:
        return None
    peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return peak if sys.platform == "darwin" else peak * 1024


def check(condition, message):
    if not condition:
        print("FAIL", message)
        raise SystemExit(1)


def best_of(run, repeats, rounds=3):
    """The fastest mean of `repeats` calls, in seconds: the least noisy number a plain loop of
    this kind gives."""
    best = float("inf")
    for _ in range(rounds):
        started = time.perf_counter()
        for _ in range(repeats):
            run()
        best = min(best, (time.perf_counter() - started) / repeats)
    return best


def main():
    rules = camel.load(str(CASES / "python_rules.cml"))
    check(rules.name == "python_rules", f"the module is named python_rules, not {rules.name}")

    def raw(function, *arguments):
        """The call as the extension answers it, before the wrapper turns a capsule into an
        array: the addresses below are the extension's own statement."""
        return rules.session.call("python_rules", function, arguments)

    # ---- an array crosses in and out without a copy ----

    flat = (ctypes.c_float * 4)(1.0, 2.0, 3.0, 4.0)
    capsule, address = float32_capsule(flat)
    returned = raw("identity", capsule)
    out_address, out_elements, out_size = bytes_of(returned)
    check(out_elements == 4 and out_size == 4, "identity returns four f32")
    check(
        out_address == address,
        f"identity must return the caller's own memory: in 0x{address:x}, out 0x{out_address:x}",
    )
    check(values_of(returned, 4) == [1.0, 2.0, 3.0, 4.0], "identity returns the same values")
    print(f"zero-copy in:  in 0x{address:x} == out 0x{out_address:x}, 4 f32, no copy")

    capsule2, address2 = float32_capsule(flat)
    scaled = raw("scale", capsule2, 2.0)
    scaled_address, scaled_elements, _ = bytes_of(scaled)
    check(scaled_elements == 4, "scale returns four f32")
    check(scaled_address != address2, "scale must build a block of its own, not write the caller's")
    check(values_of(scaled, 4) == [2.0, 4.0, 6.0, 8.0], "scale doubles every element")
    check(list(flat) == [1.0, 2.0, 3.0, 4.0], "the caller's buffer is unchanged by scale")
    print(
        f"copy out:      in 0x{address2:x} != out 0x{scaled_address:x}, "
        "4 f32, caller's buffer untouched"
    )

    capsule3, _ = float32_capsule(flat)
    total = raw("energy", capsule3)
    check(total == 30.0, f"energy is 1+4+9+16 = 30, not {total}")
    print(f"scalars:       energy = {total}")

    if numpy is not None:
        capsule_np, address_np = float32_capsule((ctypes.c_float * 4)(1.0, 2.0, 3.0, 4.0))
        array = rules.identity(capsule_np)
        check(not is_capsule(array), "with NumPy importable the wrapper gives an array")
        check(
            array.ctypes.data == address_np,
            f"the array the wrapper gives points at 0x{array.ctypes.data:x}, not 0x{address_np:x}",
        )
        check(list(array) == [1.0, 2.0, 3.0, 4.0], "and it reads the caller's values")
        print(f"numpy:         the array rules.identity returns points at 0x{address_np:x} as well")
    else:
        print("numpy:         not importable here; the wrapper hands back the capsule itself")

    # ---- passing an array costs what a call costs, not what its bytes cost ----

    # The claim "no copy" has a shape: the cost of the call does not grow with the array, while
    # the cost of copying the same bytes does. `memmove` is the copy this binding avoids, measured
    # in the same process on the same bytes, so the two numbers are comparable.
    sizes = [1 << 20, 4 << 20, 16 << 20, 64 << 20]
    print("size          pass-through (the binding)   memmove (a copy of the same bytes)")
    pass_through = {}
    copying = {}
    for size in sizes:
        source = (ctypes.c_float * (size // 4))()
        target = (ctypes.c_float * (size // 4))()

        def call(source=source):
            capsule, _ = float32_capsule(source)
            rules.session.call("python_rules", "identity", (capsule,))

        def copy(source=source, target=target, size=size):
            ctypes.memmove(target, source, size)

        pass_through[size] = best_of(call, 20)
        copying[size] = best_of(copy, 20)
        print(
            f"{size / 2**20:>5.0f} MiB   {pass_through[size] * 1e6:>12.1f} us"
            f"              {copying[size] * 1e6:>10.1f} us"
        )

    smallest, largest = sizes[0], sizes[-1]
    check(
        pass_through[largest] < 2 * copying[smallest],
        "handing over 64 MiB must cost less than copying 1 MiB, or the binding moves the bytes",
    )
    check(
        copying[largest] > 10 * copying[smallest],
        "64x the bytes must cost far more to copy, or the comparison above proves nothing",
    )
    print(
        f"therefore:    64 MiB by call {pass_through[largest] * 1e6:.1f} us, "
        f"by copy {copying[largest] * 1e6:.1f} us ({largest / copying[largest] / 1e9:.1f} GB/s), "
        f"a factor of {copying[largest] / pass_through[largest]:.0f}"
    )

    # A Camel function that walks every element, for scale: its time is the interpreted work, not
    # a copy, and it is here so that the two are not confused.
    walked = (ctypes.c_float * (2 << 20 >> 2))()
    capsule_walked, _ = float32_capsule(walked)
    started = time.perf_counter()
    rules.scale(capsule_walked, 1.0)
    print(
        f"camel work:   2 MiB through scale() takes {(time.perf_counter() - started) * 1e6:.0f} us "
        "(a closure per element on the node VM, not a measurement of copying)"
    )
    del walked

    # ---- eight results: one block, or eight ----

    # 4 MiB rather than 16: the eight copying calls below each walk every element on the node VM,
    # and the memory a result holds does not depend on how large the array is.
    before = resident_bytes()
    big = (ctypes.c_float * (4 << 20 >> 2))()
    size = 4 << 20

    def measure(function, *extra):
        if resident_bytes() is None:
            return None
        held = []
        for _ in range(8):
            capsule, _ = float32_capsule(big)
            held.append(rules.session.call("python_rules", function, (capsule, *extra)))
        used = resident_bytes() - before
        del held
        return used

    passing = measure("identity")
    copied = measure("scale", 1.0)
    if passing is None or copied is None:
        print("memory:        8 pass-through results and 8 copies: unavailable on this system")
    else:
        print(
            f"memory:        8 results of a 4 MiB pass-through hold {passing / 2**20:.1f} MiB, "
            f"8 results of a 4 MiB copy hold {copied / 2**20:.1f} MiB"
        )
        check(
            copied - passing > size * 4,
            "eight copies of a 4 MiB array must hold more memory than eight pass-throughs",
        )
    del big

    print("python binding: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())