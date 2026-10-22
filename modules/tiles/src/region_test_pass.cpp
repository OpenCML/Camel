/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Test region rewriting through ordinary CPU calls and explicit GPU execution.
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/execute/function_call.h"
#include "camel/execute/pass/opt.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/facts.h"
#include "modules/tensor/tensor.h"
#include "modules/tensor/type.h"
#include "tiles/device.h"
#include "tiles/internal/compiled_region.h"
#include "tiles/internal/graph_values.h"
#include "tiles/internal/regions.h"
#include "tiles/program.h"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>
namespace camel::tiles {
slot_t testCompiledCall(ArgsView &with, ArgsView &norm, core::context::Context &context) {
    {
        core::mm::NoCollectionScope noCollection;
        auto *function = with.get<::Function *>(0);
        std::vector<Argument> args;
        const auto count = function->graph()->normPorts().size();
        if (count == 1) {
            args.push_back({norm.slot(0), norm.type(0)});
        } else {
            auto *tuple = norm.get<::Tuple *>(0);
            auto *type  = static_cast<core::type::TupleType *>(norm.type(0));
            for (size_t i = 0; i < count; ++i)
                args.push_back({tuple->get<slot_t>(i), type->typeAt(i)});
        }
        const auto before = compilationExtractions();
        const auto first  = prepareProgram(function, args, context.facts(), true);
        const auto second = prepareProgram(function, args, context.facts(), true);
        if (compilationExtractions() != before || first.parameterInputs.size() != args.size() ||
            second.parameterInputs != first.parameterInputs)
            throw std::runtime_error("compiled region reread its graph after outlining");
        // An independent fresh extraction is the input oracle for this test only.
        // The production path above must have completed without any extraction.
        const auto reference = compile(function, args, context.facts());
        for (size_t i = 0; i < args.size(); ++i) {
            const auto &bound = first.nodes.at(first.parameterInputs[i]);
            const auto &fresh = reference.nodes.at(reference.parameterInputs.at(i));
            if (bound.data.size() != fresh.data.size() ||
                (!bound.data.empty() && std::memcmp(
                                            bound.data.data(),
                                            fresh.data.data(),
                                            bound.data.size() * sizeof(float))) ||
                bound.scalarBits != fresh.scalarBits || bound.resident != fresh.resident)
                throw std::runtime_error("compiled region retained an earlier invocation input");
        }
        // Per-worker counters make this assertion independent of simultaneous calls.
        // Its CPU execution below also covers GC safepoints and scheduler callbacks.
    }
    return testNativeCall(with, norm, context);
}
// Invoke twice and re-read the operator views after the first nested call. Regions
// are pure, so the discarded result is not an observable extra effect.
slot_t testNativeCall(ArgsView &with, ArgsView &norm, core::context::Context &context) {
    auto invoke = [&]() {
        auto *function = with.get<::Function *>(0);
        std::vector<slot_t> args;
        if (function->graph()->normPorts().size() == 1) {
            args.push_back(norm.slot(0));
        } else {
            auto *tuple = norm.get<::Tuple *>(0);
            for (size_t i = 0; i < function->graph()->normPorts().size(); ++i)
                args.push_back(tuple->get<slot_t>(i));
        }
        return execute::invokeFunction(context, function, {}, args);
    };
    (void)invoke();
    return invoke();
}
namespace {
void checkBindingKinds(core::context::Context &context) {
    namespace ty = core::type;
    namespace td = core::rtdata;
    runtime::GraphDraft draft;
    auto *scalar = ty::Type::Float64();
    auto input   = draft.addPortNode(scalar, draft.allocateRuntimeSlot(scalar));
    draft.appendNormPort(input);
    const auto constant = draft.materializeStaticValue(td::toSlot(3.5), scalar);
    const runtime::gc_node_ref_t fields[]{input, constant};
    const auto output = packGraphTuple(draft, fields);
    ty::FunctionTypeFactory signature;
    signature.addNormArg("value", scalar, false);
    signature.setExitType(draft.header(output)->dataType);
    draft.setFuncType(signature.build());
    draft.setOutputNode(output);
    draft.setExitNode(output);
    draft.setReturnNode(output, runtime::GCReturnKind::Self);
    draft.setId(context.graphIds().allocate());
    auto *graph    = draft.encode("tiles.test.binding", "tiles.test.binding");
    auto *function = ::Function::create(
        runtime::makeProgram(context, nullptr, std::span(&graph, 1)),
        graph->id(),
        ty::TupleType::create(std::vector<ty::Type *>{}),
        core::mm::autoSpace());
    const auto prototype = compileTypes(function, context.facts());
    const auto before    = compilationExtractions();
    for (uint64_t bits : {0x8000000000000000ULL, 0x7ff8000000001234ULL, 0x3ff0000000000001ULL}) {
        const double value = std::bit_cast<double>(bits);
        const auto host =
            prepareProgram(function, {{td::toSlot(value), scalar}}, context.facts(), true);
        if (host.nodes.at(host.parameterInputs.at(0)).scalarBits != bits ||
            host.nodes.at(host.outputs.at(1)).attributes !=
                std::vector<int64_t>{std::bit_cast<int64_t>(3.5)})
            throw std::runtime_error("compiled f64 binding changed input bits or embedded literal");
        auto resident = retainHostValue({}, DType::F64, std::as_bytes(std::span(&value, 1)));
        std::weak_ptr<DeviceStorage> owner = resident->owner;
        auto *holder                       = new std::shared_ptr<DeviceValue>(resident);
        td::ForeignResourceDescriptor descriptor;
        descriptor.kind    = "tiles.test.binding";
        descriptor.deleter = [](void *p) { delete static_cast<std::shared_ptr<DeviceValue> *>(p); };
        auto *wrapper =
            td::ForeignResourceObject::create(core::mm::autoSpace(), std::move(descriptor), holder);
        {
            const auto bound = prepareProgram(
                function,
                {{td::toSlot(wrapper), DeviceType::get(scalar)}},
                context.facts(),
                true);
            if (bound.nodes.at(bound.parameterInputs.at(0)).resident != resident ||
                bound.nodes.at(bound.outputs.at(1)).attributes !=
                    prototype.nodes.at(prototype.outputs.at(1)).attributes)
                throw std::runtime_error("compiled resident binding retained an earlier owner");
        }
        wrapper->dispose();
        resident.reset();
        if (!owner.expired())
            throw std::runtime_error("compiled template retained resident storage after the call");
    }
    if (compilationExtractions() != before)
        throw std::runtime_error("f64/resident template bindings extracted the graph again");
    const auto unbound = compileTypes(function, context.facts());
    for (auto inputNode : unbound.parameterInputs) {
        const auto &node = unbound.nodes.at(inputNode);
        if (!node.data.empty() || node.scalarBits || node.resident)
            throw std::runtime_error("compiled template acquired invocation data");
    }
}
void checkInvalidation(::Function *function, core::context::Context &context) {
    const auto first = compileTypes(function, context.facts());
    std::vector<Argument> args;
    for (auto input : first.parameterInputs) {
        const auto &node = first.nodes.at(input);
        auto *tensor     = tensor::createTensor(
            core::type::TypeCode::Float32,
            node.shape,
            core::mm::autoSpace());
        std::fill_n(tensor->dataAs<float>(), elements(node.shape), 0.0f);
        args.push_back(
            {core::rtdata::toSlot(tensor),
             tensor::TensorType::get(core::type::TypeCode::Float32, node.shape)});
    }
    const auto before = compilationExtractions();
    context.facts().invalidateGraph(function->graph());
    (void)prepareProgram(function, args, context.facts(), true);
    (void)prepareProgram(function, args, context.facts(), true);
    if (compilationExtractions() != before + 1)
        throw std::runtime_error("compiled region invalidation failed to rebuild once");
    auto &operators      = core::OperatorTable::instance();
    const auto &contract = first.contracts.at(0);
    operators.declareSemantics(contract.uri, *contract.declaration);
    if (hasCompiledRegion(function, context.facts()))
        throw std::runtime_error("declaration epoch did not invalidate the template");
    (void)prepareProgram(function, args, context.facts(), true);
    (void)prepareProgram(function, args, context.facts(), true);
    if (compilationExtractions() != before + 2)
        throw std::runtime_error("compiled region declaration epoch failed to rebuild once");
    runtime::RuntimeGraphDraftSession session(
        context.shared_from_this(),
        function->program(),
        function->graph());
    auto &draft  = session.rootDraft();
    bool changed = false;
    for (runtime::gc_node_ref_t id = 0; id < function->graph()->nodeCount(); ++id) {
        if (function->graph()->node(id)->kind == runtime::GCNodeKind::Oper) {
            draft.setNodePermissions(
                id,
                function->graph()->nodePermissions(id) ^ core::sem::kContract);
            changed = true;
            break;
        }
    }
    if (!changed)
        throw std::runtime_error("compiled region revision test has no operator");
    const auto *revised   = session.commit();
    auto *revisedFunction = ::Function::create(
        revised,
        revised->root()->id(),
        function->tupleType(),
        core::mm::autoSpace());
    if (hasCompiledRegion(revisedFunction, context.facts()))
        throw std::runtime_error("numeric permissions did not invalidate the template");
    (void)prepareProgram(revisedFunction, args, context.facts(), true);
    (void)prepareProgram(revisedFunction, args, context.facts(), true);
    if (compilationExtractions() != before + 3)
        throw std::runtime_error("compiled region numeric permission revision was reused");
    // Leave the original graph's template usable by subsequent calls.
    (void)compileTypes(function, context.facts());
}
class RegionOutlineTestPass final : public RuntimeGraphRewritePass {
    operator_t run_;
    std::optional<RegionDeviceOps> device_;

  public:
    RegionOutlineTestPass(
        const core::context::context_ptr_t &context, operator_t run = nullptr,
        std::optional<RegionDeviceOps> device = {})
        : RuntimeGraphRewritePass(context), run_(run), device_(device) {}
    const runtime::Program *apply(const runtime::Program *program, std::ostream &os) override {
        size_t count = 0;
        auto *result = outlineRegions(
            *context_,
            program,
            program->root(),
            run_,
            run_ == &testNativeCall     ? "tiles:test_call"
            : run_ == &testCompiledCall ? "tiles:test_compiled_call"
                                        : "tiles:run_compiled",
            count,
            device_ ? &*device_ : nullptr);
        // Global declaration replacement and detached graph publication happen
        // before scheduler execution, never in a PRL worker or concurrent call.
        if (run_ == &testCompiledCall && std::getenv("CAMEL_TILES_TEST_INVALIDATION")) {
            core::mm::NoCollectionScope noCollection;
            checkBindingKinds(*context_);
            runtime::RuntimeGraphDraftSession session(context_, result);
            std::vector<::Function *> templates;
            for (auto *graph : session.collectReachableRuntimeGraphs()) {
                auto *function = ::Function::create(
                    result,
                    graph->id(),
                    core::type::TupleType::create(std::vector<core::type::Type *>{}),
                    core::mm::autoSpace());
                if (hasCompiledRegion(function, context_->facts()))
                    templates.push_back(function);
            }
            if (templates.empty())
                throw std::runtime_error("outlined callee lost its compiled graph fact");
            checkInvalidation(templates.front(), *context_);
            for (auto *function : templates)
                (void)compileTypes(function, context_->facts());
        }
        os << "TILES_OUTLINED " << count << '\n';
        return result;
    }
};
} // namespace
void registerRegionTestPasses(
    operator_t run, RegionDeviceOps device, RegionDeviceOps host, RegionDeviceOps preferred) {
    // Test-only passes: named by a test's pipeline and by nothing else. No environment variable
    // gates them (H81).
    core::OperatorTable::instance().set(
        "tiles:test_compiled_call",
        {.pure = false, .callsGraphs = true, .readsGraphs = true});
    registerModulePass(
        "tiles::test_outline_compiled",
        [](const core::context::context_ptr_t &ctx) {
            return std::make_unique<RegionOutlineTestPass>(ctx, &testCompiledCall);
        });
    registerModulePass(
        "tiles::test_outline_preferred",
        [preferred](const core::context::context_ptr_t &ctx) {
            return std::make_unique<RegionOutlineTestPass>(ctx, preferred.execute, preferred);
        });
    registerModulePass(
        "tiles::test_outline_host_resident",
        [host](const core::context::context_ptr_t &ctx) {
            return std::make_unique<RegionOutlineTestPass>(ctx, host.execute, host);
        });
    registerModulePass(
        "tiles::test_outline_resident",
        [device](const core::context::context_ptr_t &ctx) {
            return std::make_unique<RegionOutlineTestPass>(ctx, device.execute, device);
        });
    core::OperatorTable::instance().set(
        "tiles:test_call",
        {.pure = false, .callsGraphs = true});
    registerModulePass(
        "tiles::test_outline_native",
        [](const core::context::context_ptr_t &ctx) {
            return std::make_unique<RegionOutlineTestPass>(ctx, &testNativeCall);
        });
    registerModulePass("tiles::test_outline", [](const core::context::context_ptr_t &ctx) {
        return std::make_unique<RegionOutlineTestPass>(ctx);
    });
    registerModulePass(
        "tiles::test_outline_gpu",
        [run](const core::context::context_ptr_t &ctx) {
            return std::make_unique<RegionOutlineTestPass>(ctx, run);
        });
}
} // namespace camel::tiles
