/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Keep the original function as a traced capture, dispatch its graph through Tiles.
#include "tiles/internal/dispatch.h"
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/func.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/program.h"
#include "tiles/internal/graph_values.h"
#include "tiles/internal/regions.h"
#include <stdexcept>
#include <string>
#include <vector>

namespace camel::tiles {
Function *gpuFunction(
    Function *source, operator_t run, core::context::Context &context,
    const RegionDeviceOps &preferred) {
    namespace rt = runtime;
    namespace ty = core::type;
    namespace td = core::rtdata;
    core::mm::NoCollectionScope noCollection;
    auto *signature = source->graph()->funcType();
    if (signature->withTypesCount() || !signature->normTypesCount())
        throw std::runtime_error(
            "@gpu requires a function with ordinary input parameters and no unbound context "
            "parameters");
    for (size_t i = 0; i < signature->normTypesCount(); ++i)
        if (signature->normIsVarAt(i))
            throw std::runtime_error("@gpu does not accept mutable input parameters");
    // Partition once when decorating. A detached rewrite leaves the original callable
    // intact; its captured values remain traced by the new Function's closure tuple.
    size_t regions = 0;
    const auto *regional = outlineRegions(
        context,
        source->program(),
        source->graph(),
        preferred.execute,
        preferred.executeUri,
        regions,
        &preferred);
    if (regions) {
        auto *result =
            static_cast<Function *>(source->clone(core::mm::autoSpace(), signature, false));
        result->rebind(regional, regional->root()->id());
        return result;
    }
    rt::GraphDraft draft;
    draft.setFuncType(signature);
    auto *closureType = ty::TupleType::create({signature});
    draft.setClosureType(closureType);
    const auto original = draft.addPortNode(signature, draft.allocateRuntimeSlot(signature));
    draft.appendClosureNode(original);
    std::vector<rt::gc_node_ref_t> inputs;
    for (size_t i = 0; i < signature->normTypesCount(); ++i) {
        if (signature->normIsVarAt(i))
            throw std::runtime_error("@gpu does not accept mutable input parameters");
        auto *type      = signature->normTypeAt(i);
        const auto port = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
        draft.appendNormPort(port);
        inputs.push_back(port);
    }
    auto packed = inputs.front();
    if (inputs.size() > 1)
        packed = packGraphTuple(draft, inputs);
    auto *resultType  = signature->exitType();
    const auto output = draft.addOperNode(
        resultType,
        run,
        "tiles:prefer_gpu",
        draft.allocateRuntimeSlot(resultType));
    draft.setWithInputs(output, std::span(&original, 1));
    draft.setNormInputs(output, std::span(&packed, 1));
    draft.setOutputNode(output);
    draft.setExitNode(output);
    draft.setReturnNode(output, rt::GCReturnKind::Self);
    draft.setId(context.graphIds().allocate());
    auto *graph =
        draft.encode("tiles.gpu/" + source->graph()->mangledName(), source->graph()->name());
    auto *result = ::Function::create(
        rt::makeProgram(context, std::span(&graph, 1)),
        graph->id(),
        closureType,
        core::mm::autoSpace());
    result->tuple()->set<slot_t>(0, td::toSlot(source), closureType);
    return result;
}
} // namespace camel::tiles
