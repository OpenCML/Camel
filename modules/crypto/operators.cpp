/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 */

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

std::unordered_map<std::string, operator_t> getCryptoOpsMap() {
    return {
        {"sha256", __crypto_sha256__},
        {"sha256_rounds", __crypto_sha256_rounds__},
    };
}

slot_t __crypto_sha256__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const auto input = camel::core::type::isBytesType(norm.type(0))
                           ? norm.get<BytesObject *>(0)->view()
                           : norm.get<String *>(0)->view();
    return toSlot(String::from(sha256Hex(input), mm::autoSpace()));
}

slot_t __crypto_sha256_rounds__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *input  = norm.get<String *>(0);
    int64_t rounds = norm.get<int64_t>(1);
    if (rounds < 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "crypto.sha256_rounds",
            std::format("the number of rounds is {}; it must not be negative", rounds));
    }

    std::string value(input->view());
    for (int64_t i = 0; i < rounds; ++i) {
        value = sha256Hex(value);
    }
    return toSlot(String::from(value, mm::autoSpace()));
}
