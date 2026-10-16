/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Mutation tests for plan validation: failures must precede device execution.
#include "tiles/program.h"
#include "camel/core/operator_table.h"
#include "tiles/device.h"
#include "tiles/internal/index_expr.h"
#include "tiles/internal/access.h"
#include "tiles/internal/codegen_cache.h"
#include "tiles/internal/scalar_expr.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <atomic>
#include <barrier>
#include <thread>
using namespace camel::tiles;
static const float hostSentinel = 0.0f;

template<class F> void rejects(const Program &original, F mutate, const char *reason) {
    auto p = original;
    mutate(p);
    try { validatePlan(p); }
    catch (const std::runtime_error &e) {
        if (std::string(e.what()).find(reason) != std::string::npos) return;
        throw;
    }
    throw std::runtime_error(std::string("accepted invalid plan: ") + reason);
}

void checkKernelIR();
void checkMatrixContracts();

int main() {
    try {
        {
            std::vector<float> input{1.0f, -0.0f};
            const auto expected = std::as_bytes(std::span(input));
            const std::vector<std::byte> copy(expected.begin(), expected.end());
            auto host = retainHostValue({2}, DType::F32, expected);
            input[0] = 99.0f;
            if (downloadValue(*host) != copy)
                throw std::runtime_error("host resident storage must own an immutable snapshot");
            auto invalid = *host;
            invalid.shape = {3};
            bool rejected = false;
            try { (void)downloadValue(invalid); }
            catch (const std::runtime_error &) { rejected = true; }
            if (!rejected) throw std::runtime_error("host resident storage accepted invalid extent");
            if (!downloadValue(*retainHostValue({0}, DType::F32, {})).empty())
                throw std::runtime_error("empty resident host storage");
        }
        checkKernelIR();
        checkMatrixContracts();
        using kernel::ScalarExpr;
        using kernel::ScalarValue;
        using kernel::ScalarOp;
        const auto a = ScalarExpr::symbol("a", DType::F32);
        const auto b = ScalarExpr::symbol("b", DType::F32);
        const auto c = ScalarExpr::symbol("c", DType::F32);
        const auto separate = ScalarExpr::apply(ScalarOp::Add, {ScalarExpr::apply(ScalarOp::Multiply, {a,b}), c});
        const auto fused = ScalarExpr::apply(ScalarOp::Fma, {a,b,c});
        const std::unordered_map<std::string, ScalarValue> operands = {
            {"a", ScalarValue::f32(1.0f + std::ldexp(1.0f,-23))},
            {"b", ScalarValue::f32(1.0f - std::ldexp(1.0f,-23))}, {"c", ScalarValue::f32(-1)}};
        if (separate.evaluate(operands).bits != 0 || fused.evaluate(operands).asF32() != -std::ldexp(1.0f,-46))
            throw std::runtime_error("scalar IR lost an explicit rounding boundary");
        if (separate.cuda() != "__fadd_rn(__fmul_rn(a,b),c)")
            throw std::runtime_error("scalar IR CUDA rounding instructions mismatch");
        const auto maximum = ScalarExpr::apply(ScalarOp::Maximum, {a,b});
        const auto minimum = ScalarExpr::apply(ScalarOp::Minimum, {a,b});
        if (maximum.evaluate({{"a",ScalarValue::f32(-0.0f)},{"b",ScalarValue::f32(0)}}).bits != 0 ||
            minimum.evaluate({{"a",ScalarValue::f32(0)},{"b",ScalarValue::f32(-0.0f)}}).bits != 0x80000000U ||
            maximum.evaluate({{"a",{DType::F32,0xffc01234U}},{"b",ScalarValue::f32(1)}}).bits != 0x7fc00000U)
            throw std::runtime_error("scalar IR extrema bit contract mismatch");
        const auto half = ScalarExpr::literal(ScalarValue::f64(1.0 + std::ldexp(1.0,-24)));
        if (ScalarExpr::cast(DType::F32,half).evaluate({}).asF32() != 1.0f ||
            a.substitute("a",ScalarExpr::literal({DType::F32,1})).evaluate({}).bits != 1)
            throw std::runtime_error("scalar IR cast or subnormal mismatch");
        if (!std::isinf(ScalarExpr::apply(ScalarOp::Divide,{a,b}).evaluate({{"a",ScalarValue::f32(1)},{"b",ScalarValue::f32(0)}}).asF32()))
            throw std::runtime_error("physical division must preserve IEEE semantics");
        auto rejectScalar = [](auto action) {
            try { action(); } catch (const std::exception &) { return; }
            throw std::runtime_error("accepted invalid scalar expression");
        };
        rejectScalar([&]{ (void)ScalarExpr::apply(ScalarOp::Add,{a}); });
        rejectScalar([&]{ (void)ScalarExpr::apply(ScalarOp::Add,{a,half}); });
        rejectScalar([&]{ (void)a.substitute("a",half); });
        rejectScalar([&]{ (void)a.evaluate({}); });
        rejectScalar([&]{ (void)a.evaluate({{"a",ScalarValue::f64(1)}}); });
        rejectScalar([&]{ (void)ScalarExpr::symbol("a;return",DType::F32); });
        std::cout << "tiles scalar expressions: typed rounding, FMA and IEEE bits passed\n";
        using kernel::IndexExpr;
        const auto i = IndexExpr::variable("i");
        // Transpose a 2x3 array, then apply a strided slice of the flattened view.
        const auto transposed = (i % IndexExpr(2)) * IndexExpr(3) + i / IndexExpr(2);
        const auto composed = transposed.substitute("i", IndexExpr(1) + i * IndexExpr(2));
        const uint64_t transposeExpected[] = {0, 3, 1, 4, 2, 5};
        const uint64_t sliceExpected[] = {3, 4, 5};
        for (uint64_t at = 0; at < 6; ++at)
            if (transposed.evaluate({{"i", at}}) != transposeExpected[at])
                throw std::runtime_error("structured transpose mapping mismatch");
        for (uint64_t at = 0; at < 3; ++at)
            if (composed.evaluate({{"i", at}}) != sliceExpected[at])
                throw std::runtime_error("structured composed mapping mismatch");
        auto rejected = [](auto action) {
            try { action(); } catch (const std::exception &) { return; }
            throw std::runtime_error("accepted invalid index expression");
        };
        rejected([&] { (void)(i / IndexExpr(0)); });
        rejected([&] { (void)(i % IndexExpr(0)); });
        rejected([&] { (void)IndexExpr::variable("i;return"); });
        rejected([&] { (void)i.evaluate({}); });
        rejected([&] { (void)(i + IndexExpr(1)).evaluate({{"i", UINT64_MAX}}); });
        rejected([&] { (void)(IndexExpr(UINT64_MAX) * IndexExpr(2)); });
        if ((IndexExpr(3) * IndexExpr(7)).cuda() != "21ULL")
            throw std::runtime_error("index constant folding mismatch");
        std::cout << "tiles index expressions: composition and checked evaluation passed\n";
        using kernel::Predicate;
        const auto bounded = Predicate::less(i, IndexExpr(3));
        const kernel::Access access{0, transposed, bounded};
        const auto tiled = access.substitute("i", IndexExpr::variable("base") + i);
        for (uint64_t base = 0; base < 5; ++base) {
            for (uint64_t lane = 0; lane < 4; ++lane) {
                const auto actual = tiled.evaluate({{"base", base}, {"i", lane}});
                if (actual.has_value() != (base + lane < 3) ||
                    (actual && *actual != transposeExpected[base + lane]))
                    throw std::runtime_error("masked access substitution mismatch");
            }
        }
        const kernel::Access masked{0, i + IndexExpr(1), Predicate(false)};
        if (masked.evaluate({{"i", UINT64_MAX}}))
            throw std::runtime_error("masked address was evaluated");
        const auto invalid = Predicate::less(IndexExpr(1) / i, IndexExpr(3));
        if ((Predicate(false) && invalid).evaluate({{"i", 0}}) ||
            !(Predicate(true) || invalid).evaluate({{"i", 0}}))
            throw std::runtime_error("predicate short circuit mismatch");
        rejected([&] { (void)invalid.evaluate({{"i", 0}}); });
        if ((!Predicate::equal(i, IndexExpr(2))).evaluate({{"i", 2}}))
            throw std::runtime_error("predicate equality mismatch");
        std::cout << "tiles accesses: masked composition and lazy evaluation passed\n";
        Program fusedReduction;
        fusedReduction.nodes = {{.op="input", .shape={4,2}, .data={1,2,3,4,5,6,7,8}},
                         {.op="input", .shape={}, .data={0.5}},
                         {.op="multiply", .shape={4,2}, .inputs={0,1}},
                         {.op="sum_axis", .shape={4}, .inputs={2}, .attributes={1}}};
        fusedReduction.outputs = {3};
        schedule(fusedReduction);
        if (fusedReduction.launches.size() != 1 || fusedReduction.storage.slots[2] != SIZE_MAX ||
            fusedReduction.launches[0].implementation != "ordered_reduce")
            throw std::runtime_error("producer was not fused into ordered reduction");
        rejects(fusedReduction, [](auto &x) { x.launches[0].blockX = 256; }, "ordered reduction operation or block");
        rejects(fusedReduction, [](auto &x) { x.nodes[3].op = "max_axis"; }, "ordered reduction operation or block");
        auto epilogue = fusedReduction;
        epilogue.nodes.push_back(Node{.op="add", .shape={4}, .inputs={3,1}});
        epilogue.outputs = {4};
        schedule(epilogue);
        if (epilogue.launches.size() != 1 || epilogue.storage.slots[3] != SIZE_MAX)
            throw std::runtime_error("reduction epilogue was materialized");
        rejects(epilogue, [](auto &x) {
            x.nodes[4] = Node{.op="reshape", .shape={1,4}, .inputs={3}};
        }, "ordered reduction epilogue domain");
        rejects(epilogue, [](auto &x) {
            x.nodes[4] = Node{.op="sum_axis", .shape={4}, .inputs={2}, .attributes={1}};
        }, "ordered reduction operation or block");
        fusedReduction.outputs = {2,3};
        schedule(fusedReduction);
        if (fusedReduction.launches.size() != 2 || fusedReduction.storage.slots[2] == SIZE_MAX)
            throw std::runtime_error("reduction fused an observable producer output");
        std::cout << "tiles ordered reductions: fusion boundaries and plan mutations passed\n";
        Program scalarInput;
        scalarInput.nodes = {{.op="input", .shape={}, .scalarBits=0x3ff0000000000001ULL, .dtype=DType::F64}};
        scalarInput.outputs = {0};
        schedule(scalarInput);
        if (bytes(scalarInput.nodes[0]) != 8 || !hostData(scalarInput.nodes[0]) ||
            !scalarInput.launches.empty())
            throw std::runtime_error("host scalar input representation mismatch");
        rejects(scalarInput, [](auto &x) { x.nodes[0].scalarBits.reset(); }, "invalid scalar input payload");
        rejects(scalarInput, [](auto &x) { x.nodes[0].data = {1.0}; }, "invalid scalar input payload");
        rejects(scalarInput, [](auto &x) { x.nodes[0].dtype = DType::F32; }, "invalid input payload");
        rejects(scalarInput, [](auto &x) { x.nodes[0].shape = {1}; }, "input payload dtype");
        std::cout << "tiles scalar inputs: typed payload and shape validation passed\n";
        auto scalarCode = scalarInput;
        scalarCode.nodes.push_back(Node{.op="constant64", .shape={}, .attributes={0x3ff0000000000000LL}, .dtype=DType::F64});
        scalarCode.nodes.push_back(Node{.op="scalar_add", .shape={}, .inputs={0,1}, .dtype=DType::F64});
        scalarCode.outputs = {2};
        generate(scalarCode);
        const auto firstSource = scalarCode.source;
        const auto firstStats = kernel::generationCacheStats();
        scalarCode.nodes[0].scalarBits = 0x4000000000000000ULL;
        generate(scalarCode);
        if (kernel::generationCacheStats().hits != firstStats.hits + 1 || scalarCode.source != firstSource)
            throw std::runtime_error("runtime scalar changed source-template identity");
        scalarCode.nodes[1].attributes[0] = 0x4000000000000000LL;
        generate(scalarCode);
        if (kernel::generationCacheStats().misses != firstStats.misses + 1 || scalarCode.source == firstSource)
            throw std::runtime_error("graph literal reused incompatible source template");
        const auto beforePermission = kernel::generationCacheStats();
        scalarCode.nodes.back().permissions = camel::core::sem::kContract;
        generate(scalarCode);
        if (kernel::generationCacheStats().misses != beforePermission.misses + 1)
            throw std::runtime_error("numeric permissions missing from source-template key");
        auto &operators = camel::core::OperatorTable::instance();
        namespace sem = camel::core::sem;
        const std::string maximumUri = "tilestest:maximum";
        const auto maximumId = operators.intern(maximumUri);
        sem::ReductionLaw maximumLaw{
            .name="maximum_f32", .kind=sem::LawKind::Max,
            .input=camel::core::type::TypeCode::Float32,
            .accumulator=camel::core::type::TypeCode::Float32,
            .result=camel::core::type::TypeCode::Float32,
            .initBits=0xff800000U, .empty=sem::LawEmpty::Fault, .exactAssociative=true, .exactCommutative=true,
            .hasIdentity=true, .identityBits=0xff800000U};
        sem::OperatorSemantics maximumDeclaration{.key=sem::OperatorKey::of(maximumUri), .laws={maximumLaw}};
        operators.declareSemantics(maximumUri,maximumDeclaration);
        Program maximumPlan;
        maximumPlan.contracts.push_back({.uri=maximumUri, .key=maximumDeclaration.key, .declaration=operators.semantics(maximumId)});
        maximumPlan.nodes = {{.op="input", .shape={1,128}, .data=std::vector<float>(128)},
                             {.op="max_axis", .shape={1}, .inputs={0}, .attributes={1}, .semantic=0}};
        maximumPlan.outputs={1};
        sem::IndexedInstance instance;
        const auto row=instance.exprs.dim(0), axis=instance.exprs.dim(1);
        instance.domain.dims={{"row",instance.exprs.constant(1),sem::DimRole::Parallel},
                              {"axis",instance.exprs.constant(128),sem::DimRole::Reduction}};
        instance.norm={{.kind=sem::AccessKind::Affine, .coords={row,axis}}};
        instance.output={.coords={row}, .mode=sem::WriteMode::Combine};
        instance.law=0;
        maximumPlan.nodes[1].indexing=instance;
        generate(maximumPlan);
        if (maximumPlan.launches[0].implementation != "parallel_max")
            throw std::runtime_error("core maximum law did not authorize parallel reduction");
        rejects(maximumPlan, [](auto &x){ x.nodes[1].indexing.reset(); }, "parallel maximum lacks");
        rejects(maximumPlan, [](auto &x){ x.nodes[1].indexing->law=42; }, "invalid core index instance");
        auto rowPlan = maximumPlan;
        rowPlan.nodes[1].shape = {1,1};
        rowPlan.nodes[1].indexing->output.coords.push_back(rowPlan.nodes[1].indexing->exprs.constant(0));
        rowPlan.nodes.push_back(Node{.op="subtract", .shape={1,128}, .inputs={0,1}});
        rowPlan.nodes.push_back(Node{.op="multiply", .shape={1,128}, .inputs={2,2}});
        rowPlan.outputs = {3};
        generate(rowPlan);
        if (rowPlan.launches.size()!=1 || rowPlan.launches[0].implementation!="row_region" ||
            rowPlan.storage.slots[1]!=SIZE_MAX || rowPlan.storage.slots[2]!=SIZE_MAX)
            throw std::runtime_error("shared row values must stay inside one block");
        rejects(rowPlan, [](auto &x){ x.launches[0].blockX=64; }, "invalid row region plan");
        rejects(rowPlan, [](auto &x){ x.launches[0].members.erase(x.launches[0].members.begin()); }, "invalid row region plan");
        rejects(rowPlan, [](auto &x){ x.nodes[1].indexing.reset(); }, "invalid row region plan");
        auto mappedRow = rowPlan;
        mappedRow.nodes[1].indexing->norm[0].coords[1]=mappedRow.nodes[1].indexing->exprs.constant(0);
        rejects(mappedRow, [](auto &){}, "invalid row region plan");
        generate(mappedRow);
        if(mappedRow.launches.size()<2 || mappedRow.launches[0].implementation!="parallel_max" ||
           mappedRow.source==rowPlan.source)
            throw std::runtime_error("noncanonical reduction incorrectly reused row-local loads");
        std::cout << "tiles row region: shared values and invalid launch mutations checked\n";
        maximumPlan.nodes[1].indexing.reset();
        generate(maximumPlan);
        if (maximumPlan.launches[0].implementation != "standard")
            throw std::runtime_error("undeclared reduction reused parallel source");
        std::cout << "tiles reduction laws: declared freedom required for parallel maximum\n";
        const std::string contractUri = "tilestest:versioned";
        const auto contractId = operators.intern(contractUri);
        auto declaration = sem::OperatorSemantics{
            .key=sem::OperatorKey::of(contractUri),
            .numeric={.cls=sem::NumericClass::Exact, .sensitiveTo=sem::kNoPermissions}};
        auto versioned = scalarInput;
        versioned.contracts.push_back({.uri=contractUri});
        generate(versioned); // Absence is itself a dependency.
        auto stale = [&](const Program &old) {
            try { validatePlan(old); }
            catch (const std::runtime_error &e) {
                if (std::string(e.what()).find("recompile the graph") != std::string::npos) return;
                throw;
            }
            throw std::runtime_error("stale operator declaration was accepted");
        };
        auto capture = [&] {
            versioned.contracts[0].key = operators.key(contractId);
            versioned.contracts[0].declaration = operators.semantics(contractId);
        };
        operators.declareSemantics(contractUri, declaration);
        stale(versioned);
        capture();
        generate(versioned);
        const auto beforeReplacement = kernel::generationCacheStats();
        operators.declareSemantics(contractUri, declaration); // Same version, new revision.
        stale(versioned);
        capture();
        generate(versioned);
        if (kernel::generationCacheStats().misses != beforeReplacement.misses + 1)
            throw std::runtime_error("declaration revision missing from source cache identity");
        declaration.key.minor = 1;
        operators.declareSemantics(contractUri, declaration);
        stale(versioned);
        capture();
        generate(versioned);
        declaration.key.major = 2;
        declaration.key.minor = 0;
        operators.declareSemantics(contractUri, declaration);
        stale(versioned);
        // A newer record with an old incompatible key still fails core rebinding.
        versioned.contracts[0].declaration = operators.semantics(contractId);
        stale(versioned);
        Program guarded;
        guarded.nodes = {{.op="input", .data={1}}, {.op="input", .data={2}},
                         {.op="multiply", .inputs={0,1}, .permissions=sem::kContract},
                         {.op="add", .inputs={2,0}, .permissions=sem::kContract}};
        guarded.outputs = {3};
        generate(guarded);
        if (guarded.source.find("=fmaf(") != std::string::npos)
            throw std::runtime_error("contracted arithmetic without a core declaration");
        const std::string opaqueUri = "tilestest:opaque";
        const auto opaqueId = operators.intern(opaqueUri);
        auto opaque = sem::OperatorSemantics{.key=sem::OperatorKey::of(opaqueUri)};
        operators.declareSemantics(opaqueUri, opaque);
        guarded.contracts.push_back({.uri=opaqueUri, .key=opaque.key, .declaration=operators.semantics(opaqueId)});
        guarded.nodes[2].semantic = guarded.nodes[3].semantic = 0;
        generate(guarded);
        if (guarded.source.find("=fmaf(") != std::string::npos)
            throw std::runtime_error("contracted an opaque operator");
        opaque.key.minor = 1;
        opaque.numeric.cls = sem::NumericClass::CorrectlyRounded;
        opaque.numeric.compute = camel::core::type::TypeCode::Float32;
        opaque.numeric.sensitiveTo = sem::kContract;
        operators.declareSemantics(opaqueUri, opaque);
        guarded.contracts[0].key = opaque.key;
        guarded.contracts[0].declaration = operators.semantics(opaqueId);
        generate(guarded);
        if (guarded.source.find("=fmaf(") == std::string::npos)
            throw std::runtime_error("ignored the supported arithmetic declaration");
        std::cout << "tiles operator contracts: absent, replaced and incompatible declarations rejected\n";
        std::weak_ptr<DeviceStorage> cacheOwner;
        {
            Program borrowed;
            auto value = std::make_shared<DeviceValue>();
            auto owner = retainHostValue({1}, DType::F32, std::as_bytes(std::span(&hostSentinel, 1)))->owner;
            cacheOwner = owner;
            value->owner = owner;
            value->address = 1;
            value->shape = {2};
            borrowed.nodes = {{.op="input", .shape={2}, .resident=value},
                              {.op="neg", .shape={2}, .inputs={0}}};
            borrowed.outputs = {1};
            generate(borrowed);
            borrowed.nodes[0].resident.reset();
            borrowed.nodes[0].data = {1,2};
            const auto beforeHost = kernel::generationCacheStats();
            generate(borrowed);
            if (kernel::generationCacheStats().hits != beforeHost.hits + 1 || borrowed.storage.slots[0] == SIZE_MAX)
                throw std::runtime_error("source cache reused borrowed storage plan");
        }
        if (!cacheOwner.expired())
            throw std::runtime_error("source cache retained device input ownership");
        for (int64_t bits = 0; bits < 40; ++bits) {
            Program literal;
            literal.nodes = {{.op="constant64", .shape={}, .attributes={bits}, .dtype=DType::F64}};
            generate(literal);
        }
        const auto beforeEvicted = kernel::generationCacheStats();
        generate(scalarCode);
        const auto afterEvicted = kernel::generationCacheStats();
        if (afterEvicted.entries > 32 || afterEvicted.retainedBytes > 16 * 1024 * 1024 ||
            afterEvicted.misses != beforeEvicted.misses + 1)
            throw std::runtime_error("source cache eviction failed");
        auto expectedCode = scalarCode;
        expectedCode.nodes[1].attributes[0] = 12345;
        schedule(expectedCode);
        kernel::emitCUDA(expectedCode);
        std::barrier ready(4);
        std::atomic<bool> concurrentFailure = false;
        std::vector<std::thread> workers;
        for (unsigned worker = 0; worker < 4; ++worker)
            workers.emplace_back([&, worker] {
                auto local = expectedCode;
                ready.arrive_and_wait();
                try {
                    for (unsigned iteration = 0; iteration < 16; ++iteration) {
                        local.nodes[0].scalarBits = worker * 16 + iteration;
                        generate(local);
                        if (local.source != expectedCode.source) concurrentFailure = true;
                    }
                } catch (const std::exception &) { concurrentFailure = true; }
            });
        for (auto &worker : workers) worker.join();
        if (concurrentFailure) throw std::runtime_error("concurrent source cache generation failed");
        std::cout << "tiles source cache: values, literals, storage ownership and eviction passed\n";
        Program p;
        p.nodes = {{.op="input", .shape={4}, .data={1,2,3,4}},
                   {.op="add", .shape={4}, .inputs={0,0}},
                   {.op="relu", .shape={4}, .inputs={1}},
                   {.op="sum_axis", .shape={}, .inputs={2}, .attributes={0}},
                   {.op="add", .shape={4}, .inputs={2,3}}};
        p.outputs = {4};
        schedule(p);
        validatePlan(p);
        if (p.launches.size() != 3 || p.storage.slots[1] != SIZE_MAX)
            throw std::runtime_error("expected chain fusion and shared-result boundary");
        rejects(p, [](auto &x) { x.nodes[1].inputs[0] = 4; }, "topologically earlier");
        rejects(p, [](auto &x) { x.nodes[1].shape = {5}; }, "binary result shape");
        rejects(p, [](auto &x) { x.nodes[3].attributes = {2}; }, "reduction axis");
        rejects(p, [](auto &x) { x.storage.slots[2] = x.storage.slots[0]; }, "overlapping buffer lifetimes");
        rejects(p, [](auto &x) { x.storage.bytes[x.storage.slots[0]] = 1; }, "undersized/missing allocation");
        rejects(p, [](auto &x) { x.launches[0].members.push_back(2); }, "duplicate/invalid scheduled value");
        rejects(p, [](auto &x) { x.launches[0].elements = 3; }, "launch extent");
        rejects(p, [](auto &x) { x.storage.slots[2] = SIZE_MAX; }, "register value escapes stage");
        rejects(p, [](auto &x) { x.outputs = {1}; }, "output has no materialized result");
        rejects(p, [](auto &x) { x.launches[0].blockX = 0; }, "launch dimensions");
        rejects(p, [](auto &x) { x.launches[0].implementation = "parallel_max"; }, "parallel maximum operation");
        rejects(p, [](auto &x) { x.launches[0].implementation = "unknown"; }, "unknown implementation");
        Program indexed;
        indexed.nodes = {{.op="input", .shape={2,3}, .data={1,2,3,4,5,6}},
                         {.op="reindex", .shape={3,2}, .inputs={0}, .strides={1,3}}};
        indexed.outputs = {1};
        schedule(indexed);
        validatePlan(indexed);
        rejects(indexed, [](auto &x) { x.nodes[1].strides = {1}; }, "index map rank");
        rejects(indexed, [](auto &x) { x.nodes[1].offset = 1; }, "index map outside source domain");
        rejects(indexed, [](auto &x) { x.nodes[1].strides[0] = SIZE_MAX; }, "index map address overflow");
        rejects(p, [](auto &x) { x.nodes[1].strides = {1}; }, "unexpected index map");
        rejects(p, [](auto &x) { x.nodes[2].op = "add"; x.nodes[2].inputs = {1,1}; }, "fused value must have one use");
        rejects(p, [](auto &x) { x.outputs = {}; }, "missing output");
        rejects(p, [](auto &x) { x.outputs = {4,99}; }, "missing output");
        rejects(p, [](auto &x) { x.outputs = {0,4}; }, "overlapping buffer lifetimes");
        auto multi = p;
        multi.outputs = {1,4,1,0};
        schedule(multi);
        validatePlan(multi);
        if (multi.storage.slots[1] == SIZE_MAX || multi.storage.slots[0] == multi.storage.slots[4])
            throw std::runtime_error("returned intermediates must remain live through download");
        auto reduced = indexed;
        reduced.nodes[1] = Node{.op="sum_to", .shape={1,3}, .inputs={0}};
        schedule(reduced);
        validatePlan(reduced);
        rejects(reduced, [](auto &x) { x.nodes[1].shape = {1,2,3}; }, "sum_to broadcast domain");
        auto scalar = indexed;
        scalar.nodes[1] = Node{.op="sum_all", .shape={}, .inputs={0}, .dtype=DType::F64};
        schedule(scalar);
        validatePlan(scalar);
        rejects(scalar, [](auto &x) { x.storage.bytes[x.storage.slots[1]] = 4; }, "undersized/missing allocation");
        rejects(scalar, [](auto &x) { x.nodes[1].dtype = DType::F32; }, "scalar reduction type");
        rejects(scalar, [](auto &x) { x.nodes[1].op = "relu"; }, "unsupported operation dtype");
        auto borrowed = indexed;
        auto resident = std::make_shared<DeviceValue>();
        resident->shape = {2,3};
        resident->address = 1; // Validator-only sentinel, never passed to CUDA.
        resident->owner = retainHostValue({1}, DType::F32, std::as_bytes(std::span(&hostSentinel, 1)))->owner;
        borrowed.nodes[0].resident = resident;
        borrowed.nodes[0].data.clear();
        schedule(borrowed);
        validatePlan(borrowed);
        if (borrowed.storage.slots[0] != SIZE_MAX || borrowed.storage.bytes.size() != 1)
            throw std::runtime_error("borrowed input must not allocate writable storage");
        rejects(borrowed, [](auto &x) { x.storage.slots[0] = 0; }, "borrowed input has writable allocation");
        rejects(borrowed, [](auto &x) { x.nodes[0].data = {1}; }, "invalid resident input");
        rejects(borrowed, [](auto &x) { x.nodes[1].resident = x.nodes[0].resident; }, "computed value borrows input storage");
        rejects(borrowed, [](auto &x) { x.nodes[0].resident = std::make_shared<DeviceValue>(); }, "invalid resident input");
        rejects(borrowed, [](auto &x) {
            auto value = std::make_shared<DeviceValue>(*x.nodes[0].resident);
            value->dtype = DType::F64;
            x.nodes[0].resident = value;
        }, "invalid resident input");
        rejects(borrowed, [](auto &x) { x.nodes[0].dtype = DType::F64; }, "input payload dtype");
        Program scalarOps;
        scalarOps.nodes = {{.op="constant64", .attributes={0}, .dtype=DType::F64},
                           {.op="constant64", .attributes={0}, .dtype=DType::F64},
                           {.op="scalar_add", .inputs={0,1}, .dtype=DType::F64},
                           {.op="scalar_neg", .inputs={2}, .dtype=DType::F64}};
        scalarOps.outputs = {3};
        schedule(scalarOps);
        if (scalarOps.launches.size() != 1 || scalarOps.storage.bytes != std::vector<size_t>{8})
            throw std::runtime_error("scalar chain did not fuse into one f64 result");
        rejects(scalarOps, [](auto &x) { x.nodes[2].dtype = DType::F32; }, "scalar arithmetic type or arity");
        rejects(scalarOps, [](auto &x) { x.nodes[2].inputs = {0}; }, "scalar arithmetic type or arity");
        rejects(scalarOps, [](auto &x) { x.nodes[0] = Node{.op="input", .data={1}}; }, "scalar operand type");
        std::cout << "tiles plan validation: 33 rejected mutations\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

