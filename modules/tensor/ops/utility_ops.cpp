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
 *
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Element indexing, printing, and runtime controls (thread count, GEMM backend
 * name). Printing and the controls are effectful and marked impure.
 */

#include "../interop.h"
#include "../kernels/gemm.h"
#include "../kernels/parallel.h"
#include "../npy.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/string.h"
#include "catalog.h"
#include "support.h"

#include <iostream>

namespace camel::tensor::ops {

using namespace camel::core::type;
namespace k = camel::tensor::kernels;

namespace {

slot_t idxKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("idx", [&] {
        TensorObject *t     = tensorArg(norm, 0);
        const int64_t index = intArg(norm, 1);
        if (t->rank() != 1) {
            throw std::invalid_argument("single-index access requires a rank-1 tensor");
        }
        if (index < 0 || index >= t->dim(0)) {
            throw std::invalid_argument("tensor index out of bounds");
        }
        return camel::core::rtdata::toSlot(t->getAsDouble(static_cast<uint64_t>(index)));
    });
}

slot_t idx2dKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("idx2d", [&] {
        TensorObject *t   = tensorArg(norm, 0);
        const int64_t row = intArg(norm, 1), col = intArg(norm, 2);
        if (t->rank() != 2) {
            throw std::invalid_argument("two-index access requires a rank-2 tensor");
        }
        if (row < 0 || row >= t->dim(0) || col < 0 || col >= t->dim(1)) {
            throw std::invalid_argument("tensor index out of bounds");
        }
        return camel::core::rtdata::toSlot(
            t->getAsDouble(static_cast<uint64_t>(row * t->dim(1) + col)));
    });
}

std::optional<Type *> floatScalar(const InferContext &) { return Type::Float64(); }
std::optional<Type *> voidResult(const InferContext &) { return Type::Void(); }

slot_t showKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("show", [&] {
        TensorObject *t = tensorArg(norm, 0, true);
        t->print(std::cout, TensorType::Default());
        std::cout << std::endl;
        return NullSlot;
    });
}

slot_t loadNpyKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("load_npy", [&] {
        return wrap(loadNpy(stringArg(norm, 0), resultAllocator()));
    });
}

slot_t saveNpyKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("save_npy", [&] {
        saveNpy(tensorArg(norm, 0), stringArg(norm, 1));
        return NullSlot;
    });
}

slot_t setThreadsKernel(ArgsView &, ArgsView &norm, context::Context &) {
    k::setNumThreads(static_cast<int>(intArg(norm, 0)));
    return NullSlot;
}

slot_t numThreadsKernel(ArgsView &, ArgsView &, context::Context &) {
    return camel::core::rtdata::toSlot(static_cast<camel::core::rtdata::Int64>(k::numThreads()));
}

slot_t backendKernel(ArgsView &, ArgsView &, context::Context &) {
    return camel::core::rtdata::toSlot(
        ::String::from(std::string(k::gemmBackendName()), mm::autoSpace()));
}

} // namespace

std::vector<OpDef> utilityOps() {
    const OpTraits impure{.pure = false};
    std::vector<OpDef> defs;
    defs.push_back(OpDef{
        .name      = "idx",
        .exports   = {"__idx__"},
        .params    = {{"t", ParamKind::Tensor}, {"i", ParamKind::Int}},
        .resultDoc = "float",
        .infer     = floatScalar,
        .kernel    = &idxKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "idx2d",
        .exports   = {"__idx__"},
        .params    = {{"t", ParamKind::Tensor}, {"i", ParamKind::Int}, {"j", ParamKind::Int}},
        .resultDoc = "float",
        .infer     = floatScalar,
        .kernel    = &idx2dKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "show",
        .exports   = {"show"},
        .params    = {{"t", ParamKind::TensorLike}},
        .resultDoc = "void",
        .infer     = voidResult,
        .kernel    = &showKernel,
        .traits    = impure});
    defs.push_back(OpDef{
        .name      = "load_npy",
        .exports   = {"load_npy"},
        .params    = {{"path", ParamKind::String}},
        .resultDoc = "Tensor",
        .infer     = [](const InferContext &) -> std::optional<Type *> {
            return TensorType::Default();
        },
        .kernel = &loadNpyKernel,
        .traits = impure});
    defs.push_back(OpDef{
        .name      = "save_npy",
        .exports   = {"save_npy"},
        .params    = {{"t", ParamKind::Tensor}, {"path", ParamKind::String}},
        .resultDoc = "void",
        .infer     = voidResult,
        .kernel    = &saveNpyKernel,
        .traits    = impure});
    defs.push_back(OpDef{
        .name      = "set_num_threads",
        .exports   = {"set_num_threads"},
        .params    = {{"n", ParamKind::Int}},
        .resultDoc = "void",
        .infer     = voidResult,
        .kernel    = &setThreadsKernel,
        .traits    = impure});
    defs.push_back(OpDef{
        .name      = "num_threads",
        .exports   = {"num_threads"},
        .params    = {},
        .resultDoc = "int",
        .infer     = [](const InferContext &) -> std::optional<Type *> { return Type::Int64(); },
        .kernel    = &numThreadsKernel,
        .traits    = impure});
    defs.push_back(OpDef{
        .name      = "gemm_backend",
        .exports   = {"gemm_backend"},
        .params    = {},
        .resultDoc = "string",
        .infer     = [](const InferContext &) -> std::optional<Type *> { return Type::String(); },
        .kernel    = &backendKernel,
        .traits    = {}});
    return defs;
}

} // namespace camel::tensor::ops
