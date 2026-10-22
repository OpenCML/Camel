// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/bytes.h"
#include "camel/utils/sha256.h"
#include <format>

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
        {"sha256_str", __hash_sha256_str__},
        {"sha256_bytes", __hash_sha256_bytes__},
        {"sha256_rounds", __hash_sha256_rounds__},
    };
}

// One URI per signature (the overload is chosen when the program is compiled): the digest of the
// UTF-8 bytes of a string, and of a Bytes value.
slot_t __hash_sha256_str__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(String::from(sha256Hex(norm.get<String *>(0)->view()), mm::autoSpace()));
}

slot_t __hash_sha256_bytes__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(String::from(sha256Hex(norm.get<BytesObject *>(0)->view()), mm::autoSpace()));
}

slot_t __hash_sha256_rounds__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *input  = norm.get<String *>(0);
    int64_t rounds = norm.get<int64_t>(1);
    if (rounds < 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "hash.sha256_rounds",
            std::format("the number of rounds is {}; it must not be negative", rounds));
    }

    std::string value(input->view());
    for (int64_t i = 0; i < rounds; ++i) {
        value = sha256Hex(value);
    }
    return toSlot(String::from(value, mm::autoSpace()));
}
