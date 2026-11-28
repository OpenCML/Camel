// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <unordered_map>

#include "camel/execute/executor.h"

/// The operators the `std` protocol answers: the optimization passes a program may name as
/// values (`std.cml`, `namespace cxx`). Nothing
/// runs them yet -- each kernel reports that and throws (plan step 3) -- so a call that resolves
/// is answered by a message rather than by silence.
///
/// The map holds those seven and nothing else. Every other name of `std` is answered exactly as it
/// was before this executor existed: the protocol has an executor, that executor has no such
/// operator, and the answer is "no operator" either way.
const std::unordered_map<std::string, operator_t> &getStdProtocolOpsMap();

/// Declares those operators' traits: a call to a pass yields a program and reads nothing else, so
/// it is pure (design principles 3.4: what a pass may arrange). Called once at load, before any
/// declaration that names one of them is compiled.
void declareStdPassCallTraits();