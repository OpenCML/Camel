// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Where a transform says what it built.
 *
 * A program's stdout belongs to the program. `println` is what its caller reads, and a library
 * kernel that borrows that stream puts two speakers on one channel: a run of a program that
 * transposes a function printed the transform's own walk among the program's numbers, and the only
 * place the *reason* a transpose was refused existed was that same stream, so a reader who captured
 * the program's output captured the transform's debugging with it and a reader who collected the
 * diagnostics got nothing (the author's review, high-3 second half).
 *
 * The reading goes to `std::cerr`, the diagnostic side: the same channel every diagnostic already
 * uses, and the one a plan reads with `expect_output_contains` / `expect_stderr_contains`. It is not
 * behind a log switch. A refusal's reason has to be readable without turning anything on, and a
 * reading that can be silenced is a reading nobody can cite: `present.remat` pins
 * `in_recompute=0 out_recompute=1` precisely so a transform that stopped marking anything cannot
 * stay green.
 */

#pragma once

#include <iostream>
#include <ostream>

namespace camel::transforms {

/// The stream a transform writes its reading to. Never `std::cout`: see the comment above.
inline std::ostream &readings() { return std::cerr; }

} // namespace camel::transforms