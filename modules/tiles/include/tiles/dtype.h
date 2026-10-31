// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Physical scalar element types shared by plans and kernel expressions. `I64` is the width a
// position answers in -- an index is not a narrow float -- and the kernel expression layer already
// carries that register type; these three are what a plan can name as a tensor's element.

#pragma once

namespace camel::tiles {
enum class DType { F32, F64, I64 };
}
