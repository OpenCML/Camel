/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Jul. 29, 2025
 * Updated: Oct. 24, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "str.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"

#include "fmt/args.h"
#include "fmt/core.h"

#include <sstream>

namespace GIR = GraphIR;

std::string format_vector(const std::string &fmtStr, const std::vector<std::string> &args) {
    fmt::dynamic_format_arg_store<fmt::format_context> store;
    for (const auto &arg : args) {
        store.push_back(fmt::string_view(arg));
    }
    return fmt::vformat(fmtStr, store);
}

OperatorReturnCode __format__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &with = self->withInputs();
    const auto &norm = self->normInputs();

    const data_ptr_t &fmtStrData = frame.get(norm[0]->index());

    std::string fmtStr = fmtStrData->as<StringData>(Type::String())->data();
    std::vector<std::string> args;

    for (size_t i = 0; i < with.size(); ++i) {
        const data_ptr_t &arg = frame.get(with[i]->index());
        std::ostringstream oss;
        arg->print(oss);
        args.push_back(oss.str());
    }

    try {
        frame.set(self->index(), std::make_shared<StringData>(format_vector(fmtStr, args)));
    } catch (const fmt::format_error &e) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<format>", e.what());
        frame.set(self->index(), Data::null());
    }

    return OperatorReturnCode::OK;
}

OperatorReturnCode __join__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &with = self->withInputs();
    const auto &norm = self->normInputs();

    const data_ptr_t &sepData = frame.get(with[0]->index());

    std::string separator = sepData->as<StringData>(Type::String())->data();

    std::ostringstream joined;

    const data_ptr_t &arr = frame.get(norm.front()->index());
    auto vecData = arr->as<ArrayData>(Type::Array(Type::String()));
    for (auto &arg : vecData->raw()) {
        if (joined.tellp() > 0)
            joined << separator;
        arg->print(joined);
    }

    frame.set(self->index(), std::make_shared<StringData>(joined.str()));
    return OperatorReturnCode::OK;
}
