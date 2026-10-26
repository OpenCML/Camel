#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The Python boundary: what may cross, and what happens when it may not.

* an array handed to Python is read-only -- the DLPack 1.0 flag says so and the NumPy view is not
  writeable -- so Python cannot write memory that a Camel value shares;
* a DLPack import that fails reports its real reason, leaves the capsule unconsumed so that its own
  destructor gives the memory back to the producer, and renames it to "used_dltensor" only when the
  import succeeded;
* nothing the host throws ends the interpreter: it arrives as a Python exception.

    python3 test/cases/feat/bindings/python_boundary.py
"""

import ctypes
import gc
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
CASES = HERE.parent
REPO = HERE.parents[4]

sys.path.insert(0, str(REPO / "pypi" / "camel-lang" / "src"))
os.environ.setdefault("CAMEL_HOME", str(REPO / "out" / "latest"))

import camel  # noqa: E402

sys.path.insert(0, str(CASES))
import python_binding as pb  # noqa: E402  (the DLPack structures and the capsule helpers)

try:
    import numpy
except ImportError:
    numpy = None


def check(condition, message):
    if not condition:
        print("FAIL", message)
        raise SystemExit(1)


_IsValid = ctypes.pythonapi.PyCapsule_IsValid
_IsValid.restype = ctypes.c_int
_IsValid.argtypes = [ctypes.py_object, ctypes.c_char_p]

_IsValidPointer = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p)(
    ("PyCapsule_IsValid", ctypes.pythonapi)
)

_Destructor = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
_Deleter = ctypes.CFUNCTYPE(None, ctypes.POINTER(pb.DLManagedTensor))
_keep = []


def producer_capsule(dtype, count=4):
    """A DLPack capsule the way a producer makes one: with a deleter, and a destructor that calls
    it when the capsule is dropped while still named "dltensor". Returns the capsule and the list
    that records every call of the deleter."""
    calls = []
    buffer = (ctypes.c_float * count)(*range(count))
    shape = (ctypes.c_int64 * 1)(count)
    managed = pb.DLManagedTensor()
    managed.dl_tensor.data = ctypes.cast(buffer, ctypes.c_void_p)
    managed.dl_tensor.device = pb.DLDevice(1, 0)
    managed.dl_tensor.ndim = 1
    managed.dl_tensor.dtype = dtype
    managed.dl_tensor.shape = ctypes.cast(shape, ctypes.POINTER(ctypes.c_int64))
    managed.dl_tensor.strides = None
    managed.dl_tensor.byte_offset = 0
    deleter = _Deleter(lambda _self: calls.append("deleter"))
    managed.deleter = deleter
    address = ctypes.cast(ctypes.byref(managed), ctypes.c_void_p).value

    def destructor(capsule_pointer):
        if _IsValidPointer(capsule_pointer, b"dltensor"):
            managed.deleter(ctypes.byref(managed))

    callback = _Destructor(destructor)
    _keep.append((buffer, shape, managed, deleter, callback))
    capsule = pb._PyCapsule_New(address, b"dltensor", ctypes.cast(callback, ctypes.c_void_p))
    return capsule, calls


def failure_of(call):
    try:
        call()
    except camel.CamelError as error:
        return error
    check(False, "the call was expected to raise a CamelError")


def main():
    boundary = camel.load(str(CASES / "python_boundary.cml"))
    rules = camel.load(str(CASES / "python_rules.cml"))

    # ---- what Python is given is read-only ----

    raw = boundary.session.call("python_boundary", "table", ())
    check(
        type(raw).__name__ == "PyCapsule" and _IsValid(raw, b"dltensor_versioned"),
        "an array comes back as a DLPack 1.0 capsule",
    )
    managed = pb.versioned_of(raw)
    check(managed.version.major == 1, "the capsule says it is DLPack 1.0")
    check(
        managed.flags & pb.DLPACK_FLAG_BITMASK_READ_ONLY,
        "and carries the READ_ONLY flag: Python may not write the table",
    )
    print("read-only:     the exported capsule is DLPack 1.0 with READ_ONLY set")

    if numpy is not None:
        first = boundary.table()
        check(list(first) == [1.0, 2.0, 3.0], "the table reads 1, 2, 3")
        check(not first.flags.writeable, "the NumPy view is not writeable")
        try:
            first[0] = 5.0
        except ValueError:
            pass
        else:
            check(False, "writing the view must raise")
        try:
            first.flags.writeable = True
        except ValueError:
            pass
        check(not first.flags.writeable, "the view cannot be made writeable again")
        second = boundary.table()
        check(list(second) == [1.0, 2.0, 3.0], "the next call still sees the table unchanged")
        # a consumer that predates DLPack 1.0 is given the legacy capsule; the wrapper marks it
        legacy = camel._as_python(boundary.session.call("python_boundary", "table", ()))
        check(not legacy.flags.writeable, "the array of the default path is read-only too")
        old = camel._CapsuleArray(boundary.session.call("python_boundary", "table", ()))
        capsule = old.__dlpack__()
        check(_IsValid(capsule, b"dltensor"), "an old consumer gets the legacy capsule")
        print("read-only:     the NumPy view is not writeable and the table survives a write attempt")

    # ---- a failed import says why and does not consume the capsule ----

    refused, calls = producer_capsule(pb.DLDataType(2, 16, 1))  # float16: Camel has no such array
    error = failure_of(lambda: rules.energy(refused))
    check("dtype" in str(error), f"the reason is the dtype, not a missing __dlpack__: {error}")
    check(
        _IsValid(refused, b"dltensor") and not _IsValid(refused, b"used_dltensor"),
        "a refused capsule keeps its name, so its destructor can return the memory",
    )
    check(calls == [], "nothing released the producer's memory yet")
    del refused, error
    gc.collect()
    check(calls == ["deleter"], f"dropping the capsule runs the producer's deleter once: {calls}")
    print("import:        a refused float16 capsule keeps its name; its deleter ran once")

    accepted, calls = producer_capsule(pb.DLDataType(2, 32, 1))
    check(rules.energy(accepted) == 14.0, "a float32 capsule imports (0+1+4+9)")
    check(
        _IsValid(accepted, b"used_dltensor") and not _IsValid(accepted, b"dltensor"),
        "an accepted capsule is renamed used_dltensor",
    )
    print("import:        an accepted capsule is renamed used_dltensor")

    if numpy is not None:
        readonly = numpy.frombuffer(bytes(16), dtype=numpy.float32)
        message = str(failure_of(lambda: rules.energy(readonly)))
        check("must offer __dlpack__" not in message, f"a read-only array does offer it: {message}")
        check("BufferError" in message, f"the producer's own refusal is named: {message}")
        half = numpy.zeros(4, dtype=numpy.float16)
        message = str(failure_of(lambda: rules.energy(half)))
        check("dtype" in message, f"a float16 array is refused for its dtype: {message}")
        transposed = numpy.zeros((3, 4), dtype=numpy.float32).T
        message = str(failure_of(lambda: rules.energy(transposed)))
        check("contiguous" in message, f"a transposed view is refused for its layout: {message}")
        print("import:        NumPy refusals are reported with their reasons")

    # ---- nothing the host throws ends the interpreter ----

    probe = "\n".join(
        [
            "import os, sys",
            f"sys.path.insert(0, {str(REPO / 'pypi' / 'camel-lang' / 'src')!r})",
            f"os.environ['CAMEL_HOME'] = {os.environ['CAMEL_HOME']!r}",
            "import camel",
            "session = camel._camel.Session(entry_dir='a' * 100000)",
            "try:",
            "    session.call('python_rules', 'energy', ())",
            "except camel.CamelError as error:",
            "    print('raised', type(error).__name__)",
            "print('alive')",
        ]
    )
    done = subprocess.run([sys.executable, "-c", probe], capture_output=True, text=True)
    check(done.returncode == 0 and "alive" in done.stdout, f"the interpreter survived: {done}")
    print("host failure:  the interpreter survives a session on an impossible path")
    print("python boundary: PASS")


if __name__ == "__main__":
    main()
