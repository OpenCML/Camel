// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Physical scalar element types shared by plans and kernel expressions. I64 is the width a position
// answers in -- an index is not a narrow float -- and the kernel expression layer already carries
// that register type. These five are what a plan can name as a tensor's element: the two wide
// floats, the two narrow ones a mixed-precision program stores, and the index.
//
// A narrow element is two bytes. The type gate in `graph_regions.cpp` reads it as a type the device
// knows, and no device kernel reads one yet: `eligibility` keeps a narrow node out of every region
// and says so as `device_element_kernel_missing` -- the reason names the device, because the host
// half does compute it (the core's values, and the host serial executor over the same kernel body)
// -- so the plan names the device's missing kernel instead of a type the device cannot take. The
// width here is the element's own either way, so a site that turns a dtype into bytes cannot
// silently take a bf16 value for an f32 one, which is the mistake an "else four bytes" fallback
// makes.

#pragma once

#include <cstddef>

namespace camel::tiles {
enum class DType { F32, F64, F16, BF16, I64 };

/// The number of bytes one element of the type occupies. The switch is exhaustive on purpose: a
/// type added to the enum without a width here is a warning at build time, not a default that
/// answers the wrong number of bytes.
inline std::size_t dtypeWidth(DType dtype) {
    switch (dtype) {
    case DType::F16:
    case DType::BF16:
        return 2;
    case DType::F32:
        return 4;
    case DType::F64:
    case DType::I64:
        return 8;
    }
    return 0;
}

/// Whether the type is a float. The index is not, and every other value in the enum is.
inline bool isFloating(DType dtype) { return dtype != DType::I64; }

/// The name a report writes for the type. The CUDA spelling of a generated kernel is a separate
/// question: f16 and bf16 are not C++ type names.
inline const char *dtypeName(DType dtype) {
    switch (dtype) {
    case DType::F32:
        return "f32";
    case DType::F64:
        return "f64";
    case DType::F16:
        return "f16";
    case DType::BF16:
        return "bf16";
    case DType::I64:
        return "i64";
    }
    return "?";
}
} // namespace camel::tiles