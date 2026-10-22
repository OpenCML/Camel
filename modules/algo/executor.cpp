/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 */

#include "executor.h"
#include "camel/execute/executor.h"
#include "operators.h"
#include "sequence.h"

namespace {

class AlgoExecutor : public Executor {
  public:
    AlgoExecutor(
        camel::core::context::context_ptr_t ctx, std::unordered_map<std::string, operator_t> ops)
        : Executor(std::move(ctx), std::move(ops)) {}
};

} // namespace

executor_ptr_t createAlgoExecutor(camel::core::context::context_ptr_t ctx) {
    return std::make_shared<AlgoExecutor>(std::move(ctx), getAlgoOpsMap());
}

// The three array primitives are also answered on the `array` protocol. `stdlib/array.cml`
// declares them for itself under that name (H19 step 5), while the kernels stay in this module
// until the C++ side moves; only these three are answered, so `array:sort` is not an operator.
executor_ptr_t createAlgoSequenceExecutor(camel::core::context::context_ptr_t ctx) {
    return std::make_shared<AlgoExecutor>(std::move(ctx), getAlgoSequenceOpsMap());
}
