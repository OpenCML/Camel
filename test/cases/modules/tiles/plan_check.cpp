/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Mutation tests for plan validation: failures must precede device execution.
#include "tiles/program.h"
#include "camel/core/operator_table.h"
#include "tiles/device.h"
#include "tiles/internal/index_expr.h"
#include "tiles/internal/access.h"
#include "tiles/internal/codegen_cache.h"
#include "tiles/internal/kernel_ir.h"
#include "tiles/internal/scalar_expr.h"
#include "tiles/internal/serial_execute.h"
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
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
void attachNativeReduction(Program &program, size_t node);

namespace {
void checkMultiResultStorage() {
    auto region = std::make_shared<LoopRegion>();
    region->domain = {1, 2, 3};
    region->stateInputs = {0, 1};
    region->nextStates = {1, 0};
    region->body.nodes = {{.op = "input", .shape = {2}}, {.op = "input", .shape = {2}}};
    region->body.outputs = {1, 0};
    Program p;
    p.nodes = {{.op = "input", .shape = {2}, .data = {1, 2}},
               {.op = "input", .shape = {2}, .data = {3, 4}},
               {.op = "loop", .shape = {2}, .inputs = {0, 1}, .loop = region,
                .results = {{{2}, DType::F32}, {{2}, DType::F32}}},
               {.op = "result", .shape = {2}, .inputs = {2}, .attributes = {0}},
               {.op = "result", .shape = {2}, .inputs = {2}, .attributes = {1}},
               {.op = "add", .shape = {2}, .inputs = {3, 4}}};
    p.outputs = {4, 5};
    generate(p);
    if (p.launches.size() != 2 || p.launches[0].outputs != std::vector<size_t>{3, 4} ||
        p.storage.slots[2] != SIZE_MAX || p.storage.slots[3] == SIZE_MAX ||
        p.storage.slots[4] == SIZE_MAX)
        throw std::runtime_error("multi-result producer did not own one stage with separate values");
    rejects(p, [](auto &x) { x.storage.slots[4] = x.storage.slots[3]; }, "overlapping buffer lifetimes");
    rejects(p, [](auto &x) { x.storage.slots[3] = x.storage.slots[0]; }, "overlapping buffer lifetimes");
    rejects(p, [](auto &x) { x.storage.slots[2] = x.storage.slots[3]; }, "register-only value has allocation");
    rejects(p, [](auto &x) { x.launches[0].outputs = {4, 3}; }, "stage result projection mismatch");
    rejects(p, [](auto &x) { x.launches[0].outputs.pop_back(); }, "multi-result stage arity");
    rejects(p, [](auto &x) { x.nodes[4].attributes[0] = 2; }, "result projection index");
    rejects(p, [](auto &x) { x.nodes[4].attributes[0] = 0; }, "stage result projection mismatch");
    auto one = p;
    one.outputs = {3}; // The unreturned recurrent state still has a writable launch result.
    generate(one);
    if (one.launches[0].outputs != std::vector<size_t>{3, 4} || one.storage.slots[4] == SIZE_MAX)
        throw std::runtime_error("partial result use discarded a recurrent state");
    std::cout << "tiles multi-result storage: one producer, distinct writes and inclusive read lifetimes passed\n";
}

void checkLoopSourceCache() {
    namespace sem = camel::core::sem;
    auto &table = camel::core::OperatorTable::instance();
    const std::string uri = "tilestest:loop_cache";
    const auto id = table.intern(uri);
    sem::OperatorSemantics declaration{.key = sem::OperatorKey::of(uri)};
    table.declareSemantics(uri, declaration);
    auto recurrence = std::make_shared<LoopRegion>();
    recurrence->domain = {-2147483649LL, 9007199254740993LL};
    recurrence->stateInputs = {0};
    recurrence->captureInputs = {1, 2};
    recurrence->nextStates = {6};
    auto &body = recurrence->body;
    body.contracts = {{.uri = uri, .key = declaration.key, .declaration = table.semantics(id)}};
    body.nodes = {{.op = "input"}, {.op = "input"}, {.op = "input"},
                  {.op = "input", .data = {-0.0f}},
                  {.op = "add", .inputs = {0, 3}, .semantic = 0},
                  {.op = "multiply", .inputs = {4, 1}},
                  {.op = "subtract", .inputs = {5, 2}}};
    sem::IndexedInstance index;
    index.norm = {{.kind = sem::AccessKind::Affine}, {.kind = sem::AccessKind::Affine}};
    body.nodes[4].indexing = index;
    body.outputs = {6};
    Program original;
    original.nodes = {{.op = "input", .data = {1}}, {.op = "input", .data = {2}},
                      {.op = "input", .data = {3}},
                      {.op = "loop", .inputs = {0, 1, 2}, .loop = recurrence, .results = {{{}, DType::F32}}},
                      {.op = "result", .inputs = {3}, .attributes = {0}}};
    original.outputs = {4};
    generate(original);
    auto rebound = original;
    rebound.nodes[0].data = {4};
    rebound.nodes[1].data = {5};
    rebound.nodes[2].data = {6};
    const auto beforeRebind = kernel::generationCacheStats();
    generate(rebound);
    if (kernel::generationCacheStats().hits != beforeRebind.hits + 1 || rebound.source != original.source)
        throw std::runtime_error("loop runtime inputs changed source cache identity");
    auto mutationRejected = [&](auto mutate, const char *reason) {
        auto changed = original;
        auto region = std::make_shared<LoopRegion>(*original.nodes[3].loop);
        changed.nodes[3].loop = region;
        // The self-cycle mutation must be released even when validation fails.
        struct Cleanup {
            LoopRegion &region;
            ~Cleanup() { for (auto &value : region.body.nodes) value.loop.reset(); }
        } cleanup{*region};
        mutate(changed, *region);
        try { validate(changed); }
        catch (const std::runtime_error &error) {
            if (std::string(error.what()).find(reason) != std::string::npos) return;
            throw;
        }
        throw std::runtime_error("accepted malformed loop structure");
    };
    mutationRejected([](auto &, auto &region) { region.captureInputs[0] = region.stateInputs[0]; },
                     "duplicate body input binding");
    mutationRejected([](auto &, auto &region) { region.captureInputs[1] = region.captureInputs[0]; },
                     "duplicate body input binding");
    mutationRejected([](auto &changed, auto &region) { region.body.nodes[0].loop = changed.nodes[3].loop; },
                     "nested device loops");
    mutationRejected([](auto &changed, auto &) { changed.nodes[0].loop = changed.nodes[3].loop; },
                     "nonloop node owns a recurrence");
    auto longer = original;
    auto longerRegion = std::make_shared<LoopRegion>(*original.nodes[3].loop);
    longerRegion->domain.push_back(INT64_MIN);
    longer.nodes[3].loop = longerRegion;
    generate(longer);
    const auto occurrences = [](const std::string &text, const std::string &needle) {
        size_t count = 0, position = 0;
        while ((position = text.find(needle, position)) != std::string::npos) {
            ++count;
            position += needle.size();
        }
        return count;
    };
    for (const auto *program : {&original, &longer})
        if (occurrences(program->source, "for(unsigned long long k_iteration=") != 1 ||
            occurrences(program->source, "__fadd_rn(") != 1)
            throw std::runtime_error("loop source expanded its arithmetic by trip count");
    if (original.source.find("k_iteration<2ULL") == std::string::npos ||
        longer.source.find("k_iteration<3ULL") == std::string::npos)
        throw std::runtime_error("loop source lost the exact device trip count");
    auto mutationMisses = [&](auto mutate, bool changesSource) {
        auto changed = original;
        auto region = std::make_shared<LoopRegion>(*original.nodes[3].loop);
        changed.nodes[3].loop = region;
        mutate(*region);
        const auto before = kernel::generationCacheStats();
        generate(changed);
        if (kernel::generationCacheStats().misses != before.misses + 1 ||
            (changesSource && changed.source == original.source))
            throw std::runtime_error("loop structure reused an incompatible source cache entry");
    };
    // Adjacent integers above 2^53 must differ even though float(k) rounds alike.
    mutationMisses([](auto &region) { region.domain[1] = 9007199254740992LL; }, true);
    mutationMisses([](auto &region) { region.body.nodes[3].data[0] = 0.0f; }, true);
    mutationMisses([](auto &region) { region.body.nodes[4].op = "subtract"; }, true);
    mutationMisses([](auto &region) { region.body.nodes[4].permissions = sem::kContract; }, false);
    mutationMisses([](auto &region) { region.body.nodes[4].indexing->exprs.constant(17); }, false);
    mutationMisses([](auto &region) { region.captureInputs = {2, 1}; }, true);
    mutationMisses([](auto &region) { region.stateInputs = {1}; region.captureInputs = {0, 2}; }, true);
    mutationMisses([](auto &region) { region.nextStates = {5}; region.body.outputs = {5}; }, true);
    table.declareSemantics(uri, declaration);
    mutationMisses([&](auto &region) { region.body.contracts[0].declaration = table.semantics(id); }, false);
    std::cout << "tiles loop source cache: exact domains, embedded bits, bindings and recursive contracts passed\n";
}

/// `tensor:cast_like` never reaches a plan. The compiler aliases it when the element
/// types already agree and raises UnsupportedComputation for a conversion, which keeps that
/// region on the CPU (notes/core-semantics-map.md). A plan that names it is either a scheduler
/// extension or a compiler regression; step 5 replaces this expectation with the device
/// conversion test.
void checkCastLikeBoundary() {
    Program same;
    same.nodes = {{.op = "input", .shape = {2}, .data = {1.0f, 2.0f}},
                  {.op = "input", .shape = {2}, .data = {3.0f, 4.0f}},
                  {.op = "cast_like", .shape = {2}, .inputs = {0, 1}}};
    same.outputs = {2};
    rejects(same, [](auto &) {}, "unknown operation");
    // Only f64 is the other element type today; bool/int64 tensor storage arrives with
    // step 5, and the same rejection must hold for them.
    auto conversion = same;
    conversion.nodes[0] = Node{.op = "input", .dtype = DType::F64, .scalarBits = 0};
    rejects(conversion, [](auto &) {}, "operand dtype mismatch");
    std::cout << "tiles cast_like boundary: plans never name it, and a conversion is rejected\n";
}

/// The bytes the device is handed come from `hostStorage`, at the node's own width. A scalar leaf
/// keeps its value in `scalarBits`, not in the f32 array, so an f64 leaf must contribute its eight
/// bytes there: reading the empty array instead left zeros in the staging buffer, and a device handed
/// zeros returned `0.0` for `2.0 + 3.0` while the same call on the host returned `5.0`. The other
/// widths keep their rule: an f32 tensor contributes its bits, an f64 value held as numbers widens
/// exactly, and an element width the function has no rule for is refused instead of zero-filled.
void checkHostStorage() {
    const auto word = [](double value) {
        uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    };
    const auto reading = [](const std::vector<std::byte> &storage, size_t width) {
        uint64_t bits = 0;
        if (storage.size() != width)
            throw std::runtime_error(
                "a host copy of an element is " + std::to_string(width) + " bytes");
        std::memcpy(&bits, storage.data(), width);
        return bits;
    };
    // The two operands the red case adds, as the host passes them: f64 scalars.
    for (const double value : {2.0, 3.0, -0.0}) {
        Node scalar{.op = "input", .scalarBits = word(value)};
        scalar.dtype = DType::F64;
        if (reading(hostStorage(scalar), sizeof(uint64_t)) != word(value))
            throw std::runtime_error("an f64 scalar keeps its eight bytes");
    }
    // An f64 value the host copy holds as numbers widens exactly, and an f32 tensor keeps its bits.
    Node widened{.op = "input", .shape = {1}, .data = {2.5f}, .dtype = DType::F64};
    if (reading(hostStorage(widened), sizeof(uint64_t)) != word(2.5))
        throw std::runtime_error("an f64 value held as numbers widens exactly");
    Node single{.op = "input", .shape = {1}, .data = {0.5f}, .dtype = DType::F32};
    if (reading(hostStorage(single), sizeof(uint32_t)) != 0x3F000000ULL)
        throw std::runtime_error("an f32 tensor keeps its bits");
    std::cout << "tiles host storage: an f64 scalar keeps its eight bytes (2.0 is 0x4000000000000000,"
                 " 3.0 is 0x4008000000000000), an f64 value held as numbers widens exactly, and an"
                 " f32 tensor keeps its bits\n";
}

size_t elementWidth(kernel::ScalarType type) {
    return type == kernel::ScalarType::F16 || type == kernel::ScalarType::BF16     ? 2
           : type == kernel::ScalarType::F32 || type == kernel::ScalarType::I32 ||
                     type == kernel::ScalarType::U32                               ? 4
                                                                                   : 8;
}

// One pointwise program over three elements, multiplied either by the constant two or by itself, with
// the product stored in `store`: the store's type is what decides whether the narrowing happens.
Program narrowProduct(DType element, DType store, bool square) {
    const std::vector<float> values = {0.1f, 0.25f, 1.5f};
    Program p;
    p.nodes = {
        {.op = "input", .shape = {3}, .dtype = element, .data = values},
        {.op = "input", .shape = {3}, .dtype = element, .data = {2.0f, 2.0f, 2.0f}},
        {.op     = "multiply",
         .shape  = {3},
         .inputs = square ? std::vector<size_t>{0, 0} : std::vector<size_t>{0, 1},
         .dtype  = store}};
    return p;
}

// Every value the body's stores wrote, in store order: the whole kernel, run on the host.
std::vector<kernel::ScalarValue> serialStores(const Program &p) {
    Launch launch{.node = 2, .members = {2}, .elements = 3, .name = "k_row"};
    const auto body = kernel::serialBody(p, launch);
    std::vector<kernel::SerialBuffer> buffers;
    for (const auto &node : p.nodes)
        // An input's bytes are the host copy's, rounded once into its element type; an intermediate
        // starts as the zeros the body is about to write over.
        buffers.push_back(
            {kernel::ScalarType(node.dtype),
             node.data.empty() ? std::vector<std::byte>(bytes(node)) : hostStorage(node)});
    const auto run = kernel::serialExecute(
        body, {launch.blockX, launch.blockY, launch.gridX, launch.gridY}, std::move(buffers));
    std::vector<kernel::ScalarValue> stored;
    for (const auto &write : run.writes)
        stored.push_back(write.value);
    return stored;
}

void requireStored(
    const std::vector<kernel::ScalarValue> &stored, size_t index, uint64_t bits, const char *what) {
    if (stored.size() <= index || stored[index].bits != bits)
        throw std::runtime_error(std::string("narrow serial value mismatch: ") + what);
}

/// A narrow element has no kernel on any device yet, so no device can report what one computes. The
/// body the lowering already produces can, because the serial executor walks that body's own
/// statements: the readings below are the three numbers the language defines for `x * 2.0` on a bf16
/// and an f16 element, the product in f32 and the element each type writes back. Every pinned bit is
/// derived by hand from the two rules -- arithmetic in f32, one round-to-nearest-even into the
/// element -- and not copied from a run, so a lowering that rounded twice or truncated reds here.
void checkNarrowSerialRun() {
    using namespace camel::tiles::kernel;
    // 0.1 reads as bf16 0x3DCD and f16 0x2E66: the load is the one rounding of the input. Doubling
    // moves the exponent only, so the product is exact in f32 and in both element types.
    const auto bf16Intermediate = serialStores(narrowProduct(DType::BF16, DType::F32, false));
    const auto f16Intermediate  = serialStores(narrowProduct(DType::F16, DType::F32, false));
    const auto bf16Stored       = serialStores(narrowProduct(DType::BF16, DType::BF16, false));
    const auto f16Stored        = serialStores(narrowProduct(DType::F16, DType::F16, false));
    requireStored(bf16Intermediate, 0, 0x3E4D0000ULL, "2 * bf16(0.1) in f32 is 0.2001953125");
    requireStored(f16Intermediate, 0, 0x3E4CC000ULL, "2 * f16(0.1) in f32 is 0.199951171875");
    requireStored(bf16Stored, 0, 0x3E4DULL, "the bf16 write-back is the same number, in bf16");
    requireStored(f16Stored, 0, 0x3266ULL, "the f16 write-back is the same number, in f16");
    // The other elements are exact in both types: 0.5 and 3.0, so only the first element is a reading.
    requireStored(bf16Stored, 1, 0x3F00ULL, "bf16 0.5");
    requireStored(bf16Stored, 2, 0x4040ULL, "bf16 3.0");
    // The written bits are printed from the run itself, so the line a reader sees cannot disagree
    // with the value the executor stored: the assertion above already proved they are the hand ones.
    const auto written = [](const kernel::ScalarValue &value) {
        std::ostringstream out;
        out << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << value.bits;
        return out.str();
    };
    std::cout << "tiles narrow serial: f32 intermediate 0.2001953125 with a bf16 element and "
                 "0.199951171875 with an f16 element, bf16 wrote "
              << written(bf16Stored.at(0)) << " 0.2001953125, f16 wrote " << written(f16Stored.at(0))
              << " 0.199951171875\n";
    // The same elements squared, where the store really rounds: 0.0100193... is nearer to bf16
    // 0x3C24 than to its neighbours, and 0.00999511... is nearer to f16 0x211E, one rounding each.
    const auto bf16Squared = serialStores(narrowProduct(DType::BF16, DType::BF16, true));
    const auto f16Squared  = serialStores(narrowProduct(DType::F16, DType::F16, true));
    requireStored(bf16Squared, 0, 0x3C24ULL, "0.0100193 rounded once into bf16 is 0.010009765625");
    requireStored(f16Squared, 0, 0x211EULL, "0.00999511 rounded once into f16 is 0.0099945068359375");
    std::cout << "tiles narrow serial: a store that rounds writes bf16 " << written(bf16Squared.at(0))
              << " 0.010009765625 and f16 " << written(f16Squared.at(0))
              << " 0.0099945068359375, one nearest-even rounding each\n";
    // An f64 reaches a narrow element only through a reduction, and that single rounding from f64 is
    // not written yet: the conversion refuses it rather than rounding twice through an f32.
    bool refusedDouble = false;
    try {
        (void)kernel::ScalarExpr::cast(
            kernel::ScalarType::F16, kernel::ScalarExpr::literal(kernel::ScalarValue::f64(0.1)));
    } catch (const std::exception &) {
        refusedDouble = true;
    }
    if (!refusedDouble)
        throw std::runtime_error("a narrow conversion accepted an f64 operand");
    // Every register type the layer names, one at a time: an element type the executor cannot hold
    // must be counted here and refused, not executed at a neighbour's width.
    size_t refused = 0;
    for (const auto type : {ScalarType::F32, ScalarType::F64, ScalarType::F16, ScalarType::BF16,
                            ScalarType::I32, ScalarType::U32, ScalarType::I64}) {
        const KernelBody body{
            {{MemorySpace::Global, type, 0, 1, true}},
            {Store{{0, IndexExpr(0), Predicate(true)}, ScalarExpr::literal({type, 0})}}};
        std::vector<SerialBuffer> buffers = {{type, std::vector<std::byte>(elementWidth(type))}};
        try {
            (void)serialExecute(body, SerialGrid{}, std::move(buffers));
        } catch (const std::exception &) {
            ++refused;
        }
    }
    if (refused != 0)
        throw std::runtime_error(
            "the serial executor refused " + std::to_string(refused) + " numeric element types");
    std::cout << "tiles narrow serial: 7 numeric element types, 0 refused\n";
}
} // namespace

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
        checkLoopSourceCache();
        checkMultiResultStorage();
        checkCastLikeBoundary();
        checkHostStorage();
        checkNarrowSerialRun();
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
        attachNativeReduction(fusedReduction,3);
        schedule(fusedReduction);
        if (fusedReduction.launches.size() != 1 || fusedReduction.storage.slots[2] != SIZE_MAX ||
            fusedReduction.launches[0].implementation != "ordered_reduce")
            throw std::runtime_error("producer was not fused into ordered reduction");
        rejects(fusedReduction, [](auto &x) { x.launches[0].blockX = 256; }, "ordered reduction operation or block");
        rejects(fusedReduction, [](auto &x) { x.nodes[3].op = "max_axis"; }, "unsupported numerical law");
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
            attachNativeReduction(x,4);
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
        rejects(maximumPlan, [](auto &x){ x.nodes[1].indexing.reset(); }, "missing index instance");
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
        rejects(rowPlan, [](auto &x){ x.nodes[1].indexing.reset(); }, "missing index instance");
        auto mappedRow = rowPlan;
        mappedRow.nodes[1].indexing->norm[0].coords[1]=mappedRow.nodes[1].indexing->exprs.constant(0);
        rejects(mappedRow, [](auto &){}, "invalid row region plan");
        generate(mappedRow);
        if(mappedRow.launches.size()<2 || mappedRow.launches[0].implementation!="parallel_max" ||
           mappedRow.source==rowPlan.source)
            throw std::runtime_error("noncanonical reduction incorrectly reused row-local loads");
        std::cout << "tiles row region: shared values and invalid launch mutations checked\n";
        maximumPlan.nodes[1].indexing.reset();
        bool missingReductionInstance = false;
        try { generate(maximumPlan); }
        catch (const std::runtime_error &error) {
            missingReductionInstance = std::string(error.what()).find("missing index instance") != std::string::npos;
        }
        if (!missingReductionInstance)
            throw std::runtime_error("undeclared reduction acquired private traversal semantics");
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
            Program unbound;
            unbound.nodes = {{.op="input", .shape={1048576}},
                             {.op="neg", .shape={1048576}, .inputs={0}}};
            unbound.outputs = {1};
            generate(unbound, {}, InputBindings::Deferred);
            if (!unbound.nodes[0].data.empty() || unbound.source.empty())
                throw std::runtime_error("shape-only planning allocated an input payload");
            bool refused = false;
            try { validatePlan(unbound); } catch (const std::exception &) { refused = true; }
            if (!refused)
                throw std::runtime_error("executable validation accepted an unbound input");
            unbound.nodes[0].data.resize(1048576, 3.0f);
            const auto beforeBinding = kernel::generationCacheStats();
            generate(unbound);
            if (kernel::generationCacheStats().hits != beforeBinding.hits + 1)
                throw std::runtime_error("binding input data changed the compiled plan identity");
            unbound.nodes[0].data.resize(1);
            refused = false;
            try { generate(unbound, {}, InputBindings::Deferred); }
            catch (const std::exception &) { refused = true; }
            if (!refused)
                throw std::runtime_error("deferred binding hid an invalid provided payload");
        }
        std::cout << "tiles deferred inputs: payload-free planning and strict execution checks passed\n";
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
        attachNativeReduction(p,3);
        schedule(p);
        validatePlan(p);
        if (p.launches.size() != 3 || p.storage.slots[1] != SIZE_MAX)
            throw std::runtime_error("expected chain fusion and shared-result boundary");
        rejects(p, [](auto &x) { x.nodes[1].inputs[0] = 4; }, "topologically earlier");
        rejects(p, [](auto &x) { x.nodes[1].shape = {5}; }, "binary result shape");
        rejects(p, [](auto &x) { x.nodes[3].indexing->domain.dims[0].role = camel::core::sem::DimRole::Parallel; }, "missing from reduction projection");
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
        attachNativeReduction(reduced,1);
        schedule(reduced);
        validatePlan(reduced);
        rejects(reduced, [](auto &x) { x.nodes[1].shape = {1,2,3}; }, "unsupported output map");
        auto scalar = indexed;
        scalar.nodes[1] = Node{.op="sum_all", .shape={}, .inputs={0}, .dtype=DType::F64};
        attachNativeReduction(scalar,1);
        schedule(scalar);
        validatePlan(scalar);
        rejects(scalar, [](auto &x) { x.storage.bytes[x.storage.slots[1]] = 4; }, "undersized/missing allocation");
        // The annotation is the elements' own type (design principles 7.3), so an f32 scalar
        // reduction is now a valid plan: what the schedule still refuses is one that is not a
        // scalar at all.
        rejects(scalar, [](auto &x) { x.nodes[1].shape = {1}; }, "scalar reduction type");
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
        // The device's widths are f32 and f64 and a scalar operation now takes either (the element
        // type a whole-tensor reduction answers divides a gradient by a count), so what this
        // mutation can still pin is the shape half of the rule: an operation that is not a scalar
        // at all is refused, and so is one whose operands are at another width (below).
        rejects(scalarOps, [](auto &x) { x.nodes[2].shape = {1}; }, "scalar arithmetic type or arity");
        rejects(scalarOps, [](auto &x) { x.nodes[2].inputs = {0}; }, "scalar arithmetic type or arity");
        rejects(scalarOps, [](auto &x) { x.nodes[0] = Node{.op="input", .data={1}}; }, "scalar operand type");
        std::cout << "tiles plan validation: 33 rejected mutations\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

