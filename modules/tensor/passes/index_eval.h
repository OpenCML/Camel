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
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * TEST-ONLY: `tensor::test_index_eval`, the declaration evaluator of docs/design/op-semantics
 * section 12 (phase 2). It is what makes the index declarations trustworthy for a back end
 * that generates code from them (Tiles): a declaration is code, nothing checks it when it is
 * registered (design section 16, first risk), so this pass executes every declared operator
 * from its declaration alone and compares the result with the real CPU kernel, bit for bit.
 *
 * For each operator it generates many calls (deterministic pseudo-random shapes including
 * empty, size-1 and broadcast axes, negative axes, absent optional arguments; values including
 * NaN, infinities, signed zeros and subnormals), asks the operator table for the instance of
 * each call (sem::instantiate, with every shape known), and:
 *
 *   evaluates it: visits the domain in canonical order, reads every operand through its access
 *   map (applying the validity condition and its policy), and either writes the point's value
 *   (a Unique write; the value is the operator's scalar function, written here from the
 *   numeric declaration's algorithm) or collects the contribution and folds it by the declared
 *   reduction law (start, accumulator type, order, finalization, empty case);
 *
 *   checks the maps for soundness on the way: every coordinate an Unreachable access reads is
 *   in range, every write lands inside the result, a Unique write is never written twice and
 *   every element of its result is written;
 *
 *   runs the kernel (the operator's OpDef kernel, as the program would) and compares: both
 *   fault, or the same shape, dtype and bits (two NaNs agree unless the operator is exact,
 *   which declares the canonical NaN; a bounded operator is held to its ulp bound).
 *
 * It prints `EVAL op=<uri> cases=<n> agree=<n> faults=<n> points=<n> unsound=<n>` per operator,
 * `EVAL_MISMATCH ...` for a disagreement (the first few), `QUERY <check> <result>` for the
 * facts that must give no instance (unknown ranks, Q9 broadcasts, constants that are not), and
 * a final `EVAL total ...` line. The program's graph is returned unchanged. It exists only when
 * a test pipeline names them.
 */

#pragma once

#include "camel/execute/pass/opt.h"

namespace camel::tensor::passes {

class IndexEvalTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

} // namespace camel::tensor::passes
