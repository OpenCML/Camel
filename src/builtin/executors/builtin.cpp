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
 * Created: Sep. 09, 2025
 * Updated: Sep. 27, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "builtin.h"
#include "utils/log.h"

#include "../operators/io.h"
#include "../operators/math.h"
#include "../operators/ops.h"
#include "../operators/os.h"
#include "../operators/other.h"
#include "../operators/str.h"
#include "../operators/struct.h"
#include "../operators/this.h"
#include "../operators/time.h"

const std::unordered_map<std::string, operator_t> &getOpsOpMap() {
    static const std::unordered_map<std::string, operator_t> map = {
        {"not-impl", __not_implemented__},

        // ops
        {"op/assn", __builtin__assn__},
        {"op/assn_add", __builtin__assn_add__},
        {"op/assn_sub", __builtin__assn_sub__},
        {"op/assn_mul", __builtin__assn_mul__},
        {"op/assn_div", __builtin__assn_div__},
        {"op/assn_mod", __builtin__assn_mod__},
        {"op/assn_mat", __builtin__assn_mat__},
        {"op/assn_exp", __builtin__assn_exp__},
        {"op/assn_and", __builtin__assn_and__},
        {"op/assn_or", __builtin__assn_or__},
        {"op/or", __builtin__or__},
        {"op/and", __builtin__and__},
        {"op/eq", __builtin__eq__},
        {"op/neq", __builtin__neq__},
        {"op/strict_eq", __builtin__strict_eq__},
        {"op/strict_neq", __builtin__strict_neq__},
        {"op/lt", __builtin__lt__},
        {"op/le", __builtin__le__},
        {"op/gt", __builtin__gt__},
        {"op/ge", __builtin__ge__},
        {"op/add", __builtin__add__},
        {"op/sub", __builtin__sub__},
        {"op/mul", __builtin__mul__},
        {"op/div", __builtin__div__},
        {"op/mod", __builtin__mod__},
        {"op/mat", __builtin__mat__},
        {"op/exp", __builtin__exp__},
        {"op/idx", __builtin__idx__},
        {"io/print", __print__},
        {"io/println", __println__},
        {"io/input", __input__},

        // struct
        {"struct/len", __len__},
        {"struct/head", __head__},
        {"struct/tail", __tail__},
        {"struct/range", __range__},
        {"struct/slice", __slice__},
        {"struct/concat", __concat__},
        {"struct/concat", __concat__},
        {"struct/append", __append__},
        {"struct/extend", __extend__},
        {"struct/contains", __contains__},

        // os
        {"os/exit", __exit__},
        {"os/sleep", __sleep__},
        {"os/whoami", __whoami__},
        {"os/set_terminal_raw_mode", __set_terminal_raw_mode__},
        {"os/has_input", __has_input__},
        {"os/get_char", __get_char__},

        // str
        {"str/format", __format__},
        {"str/join", __join__},

        // math
        {"str/abs", __abs__},
        {"str/round", __round__},
        {"str/ceil", __ceil__},
        {"str/floor", __floor__},
        {"str/bin", __bin__},
        {"str/oct", __oct__},
        {"str/hex", __hex__},

        // time
        {"time/now", __now__},
        {"time/strftime", __strftime__},
        {"time/strptime", __strptime__},

        // this
        {"zen", __zen__},
    };
    return map;
}

BasicBuiltinExecutor::BasicBuiltinExecutor(context_ptr_t ctx) : Executor(ctx, getOpsOpMap()) {};

OperatorReturnCode
BasicBuiltinExecutor::eval(std::string uri, GraphIR::node_ptr_t &self, Frame &frame) {
    EXEC_WHEN_DEBUG(l.in("BasicExec").debug("Evaluating operator of URI: {}", uri));
    auto it = opsMap_.find(uri);
    if (it == opsMap_.end()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidURI,
            std::format("Invalid URI: {}", uri));
    }
    return it->second(self, frame, *context_);
};
