// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace camel::source {

using source_file_id_t = uint32_t;
using span_id_t        = uint32_t;
using origin_id_t      = uint64_t;

constexpr source_file_id_t kInvalidSourceFileId = 0;
constexpr span_id_t kInvalidSpanId              = 0;
constexpr origin_id_t kInvalidOriginId          = 0;

} // namespace camel::source
