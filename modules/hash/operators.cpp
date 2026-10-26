// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/bytes.h"
#include "camel/utils/sha256.h"

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;

using camel::utils::sha256Hex;

std::unordered_map<std::string, operator_t> getHashOpsMap() {
    return {
        {"sha256_bytes", __hash_sha256_bytes__},
    };
}

// The one kernel: the digest of a Bytes value. A text is encoded first (stdlib/hash.cml).
slot_t __hash_sha256_bytes__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(String::from(sha256Hex(norm.get<BytesObject *>(0)->view()), mm::autoSpace()));
}
