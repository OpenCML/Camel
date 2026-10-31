// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A host run of one kernel body: the values a device program would compute, computed here.
//
// This is not a second lowering and it is not an optimizer. It walks the statements the kernel IR
// already holds -- the same loops, the same addresses, the same typed scalar expressions -- and
// executes them one thread at a time in the order they are written, so what it reads is the value of
// the program the plan describes. It exists because a device can refuse a program: a narrow element
// type has no kernel on any device yet, and "the value this kernel produces" is unreadable from a
// device that will not run it. Nothing in the device path calls this; a check does.

#pragma once
#include "tiles/internal/kernel_ir.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace camel::tiles::kernel {
/// The thread grid of a planned launch: the block extents a body was lowered for, and the number of
/// blocks the launch schedules.
struct SerialGrid {
    unsigned blockX = 1;
    unsigned blockY = 1;
    unsigned gridX  = 1;
    unsigned gridY  = 1;
};

/// One buffer of the body, as the bytes a device buffer holds: `extent` elements of `type`, in the
/// order the addresses index them. The extent is the body's, so the two cannot disagree.
struct SerialBuffer {
    ScalarType type;
    std::vector<std::byte> bytes;
};

/// One store a thread performed, as the register it narrowed and the element it wrote: the f32 a
/// narrow store rounds is a value no reader of the buffer can see afterwards.
struct SerialWrite {
    size_t buffer = 0;
    uint64_t address = 0;
    ScalarValue value;  // the register written, in the store's own type
};

struct SerialRun {
    std::vector<SerialBuffer> buffers;
    std::vector<SerialWrite> writes;
    /// Threads that executed the body, and the first fault source a thread reported (0 when none):
    /// a serial run is a reading, so it reports the fault instead of failing the caller's process.
    size_t threads = 0;
    bool faulted   = false;
    uint32_t fault = 0;
};

/// Runs `body` on the host with `grid`, writing into `buffers` (which must match the body's buffers
/// element for element: same element type, `extent` elements of it).
///
/// A statement the executor has no meaning for, and an element type it cannot hold, are refused by
/// throwing: a run that silently skipped either would report values that are not the program's.
SerialRun serialExecute(
    const KernelBody &body, const SerialGrid &grid, std::vector<SerialBuffer> buffers);

/// The element types a serial run holds, by the name the plan prints. A type missing from this list
/// is refused, so a type added to the register layer cannot quietly execute as its neighbour.
const std::vector<ScalarType> &serialElementTypes();
} // namespace camel::tiles::kernel