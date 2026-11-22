// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

namespace camel::core {

/// Thrown by the `exit` operator: the program asks to end with `code`. It is neither an error
/// nor a fault, so there is no diagnostic. It unwinds the scheduler the way a fault does (the
/// tasks in flight finish or are skipped, nothing after the call runs) and the pass driver ends
/// the pipeline as a completed run whose exit code is `code`: the process then leaves through
/// the same path as a program whose `main` returned, with the same flushing and clean-up.
///
/// Deliberately not a std::exception: handlers that turn exceptions into errors do not see it.
struct ProcessExit {
    int code = 0;
};

} // namespace camel::core
