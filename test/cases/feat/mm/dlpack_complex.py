"""The NumPy side of Camel's DLPack contract for complex numbers (H15c item 5).

Camel exports a complex tensor as DLPack dtype code 5 (complex), 64 bits for c64 and 128 for
c128, lanes 1, with the two parts (real, imaginary) in memory order: src/core/array/dlpack.cpp,
and the round trip itself in src/core/array/block_selftest.cpp (the `std::gc::blocks` pass).

This script checks NumPy's side of that contract: a NumPy complex128 array announces the same
DLPack dtype in the capsule its __dlpack__ gives, and its bytes are the same (real, imaginary)
float64 pairs, for the parts the C++ round trip writes. It exists so the two sides cannot drift
apart silently; there is no bridge that hands a Camel array to Python yet (the capsule wrapper
is H17's interop step), so the round trip is the C++ one and this is its NumPy half.

Runs only when NumPy is importable: the test plan gates it with
`skip_unless = ["${python}", "-c", "import numpy"]`.
"""

import ctypes
import struct
import sys

import numpy as np

# The parts src/core/array/block_selftest.cpp writes into its 20-element c128 array.
PARTS = [(1.5, -2.5), (0.0, 1.0), (-3.25, 0.0), (-0.0, 0.5)]


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
    _fields_ = [
        ("dl_tensor", DLTensor),
        ("manager_ctx", ctypes.c_void_p),
        ("deleter", ctypes.c_void_p),
    ]


def dlpack_view(array):
    """NumPy's own DLPack view of `array`: its capsule (kept alive by the caller) and the
    DLTensor it names (a capsule's deleter frees the DLManagedTensor when the capsule dies)."""
    get_pointer = ctypes.pythonapi.PyCapsule_GetPointer
    get_pointer.restype = ctypes.c_void_p
    get_pointer.argtypes = [ctypes.py_object, ctypes.c_char_p]
    capsule = array.__dlpack__()
    address = get_pointer(capsule, b"dltensor")
    if address is None:
        raise AssertionError("NumPy's capsule named no DLManagedTensor")
    return capsule, DLManagedTensor.from_address(address).dl_tensor


def main():
    if ctypes.sizeof(ctypes.c_void_p) != 8:
        print("dlpack complex: this platform's pointers are not 8 bytes wide; skipped")
        print("dlpack complex: PASS")
        return 0

    values = [complex(real, imaginary) for real, imaginary in (PARTS[i % 4] for i in range(20))]
    array = np.array(values, dtype=np.complex128)
    assert array.dtype.itemsize == 16, f"complex128 is {array.dtype.itemsize} bytes"

    # One element is the two parts, real then imaginary, each a float64 in the machine's order.
    expected = b"".join(struct.pack("=dd", real, imaginary) for real, imaginary in
                        (PARTS[i % 4] for i in range(20)))
    assert array.tobytes() == expected, "the bytes of complex128 are not (real, imaginary) float64 pairs"

    # NumPy's own DLPack view of the same array: the dtype Camel exports and imports.
    capsule, tensor = dlpack_view(array)
    assert tensor.dtype.code == 5, f"dtype code {tensor.dtype.code}, expected 5 (complex)"
    assert tensor.dtype.bits == 128, f"dtype bits {tensor.dtype.bits}, expected 128"
    assert tensor.dtype.lanes == 1, f"dtype lanes {tensor.dtype.lanes}, expected 1"
    assert tensor.ndim == 1 and tensor.shape[0] == 20, "NumPy's DLPack view of the array is not 1-D of 20"
    assert tensor.data == array.ctypes.data, "NumPy's DLPack view does not point at the array"
    # The capsule is the owner of the DLManagedTensor read above: it must outlive the reads.
    del capsule

    print(
        f"dlpack complex: numpy {np.__version__}, complex128 is dtype code 5 bits 128 lanes 1, "
        f"{array.nbytes} bytes")
    print("dlpack complex: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())