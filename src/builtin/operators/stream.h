// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"

// The bookkeeping of `tee` (design principles 5.4). A stream is a value and is consumed once on
// each execution path, so reading one stream from two places is `tee(s, n)`, which is written in
// Camel (stdlib/stream.cml) because its result is a value of the graph, not a value C++ can build.
// What the report needs from it -- how many elements it moved and how far the two sides ran apart
// -- is only known while it runs, so the tee hands each pull of its source to this operator and
// the profile session keeps the numbers (`STREAM_TEE`).
slot_t __stream_tee_note__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);