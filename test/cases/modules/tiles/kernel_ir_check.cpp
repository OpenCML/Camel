/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Structural mutation tests for physical kernel types, scopes and convergence.
#include "tiles/internal/kernel_ir.h"
#include "tiles/internal/codegen_cache.h"
#include "tiles/program.h"
#include "tiles/internal/regions.h"
#include "camel/core/operator_table.h"
#include <iostream>
#include <stdexcept>
using namespace camel::tiles;
using namespace camel::tiles::kernel;

void checkPointwiseContracts();
void checkReductionContracts();
void checkGeneralReductions();
void checkRegions();
void checkReductionDomains();
void checkCoreViews();
void attachNativeReduction(Program &program, size_t node);

namespace {
void checkScalarI64() {
    const auto literal = [](int64_t value) { return ScalarExpr::literal(ScalarValue::i64(value)); };
    const auto element = ScalarExpr::symbol("element", ScalarType::I64);
    const auto converted = ScalarExpr::cast(ScalarType::F64, element);
    // Explicit IEEE encodings lock in nearest-even rounding independently of
    // the host conversion used by the reference evaluator.
    const std::vector<std::pair<int64_t, uint64_t>> cases = {
        {0, 0}, {1, 0x3ff0000000000000ULL}, {-1, 0xbff0000000000000ULL},
        {2147483649LL, 0x41e0000000200000ULL}, {-2147483649LL, 0xc1e0000000200000ULL},
        {9007199254740991LL, 0x433fffffffffffffULL},
        {9007199254740992LL, 0x4340000000000000ULL},
        {9007199254740993LL, 0x4340000000000000ULL},
        {9007199254740994LL, 0x4340000000000001ULL},
        {9007199254740995LL, 0x4340000000000002ULL},
        {-9007199254740993LL, 0xc340000000000000ULL},
        {-9007199254740995LL, 0xc340000000000002ULL},
        {INT64_MAX, 0x43e0000000000000ULL}, {INT64_MIN, 0xc3e0000000000000ULL},
        {INT64_MIN + 1, 0xc3e0000000000000ULL},
        {INT64_MAX - 512, 0x43dfffffffffffffULL},
        {INT64_MAX - 511, 0x43e0000000000000ULL}};
    for (const auto &[value, expected] : cases) {
        const std::unordered_map<std::string, ScalarValue> bindings = {{"element", ScalarValue::i64(value)}};
        if (literal(value).evaluate({}).asI64() != value ||
            element.evaluate(bindings).asI64() != value ||
            converted.evaluate(bindings).bits != expected ||
            ScalarExpr::cast(ScalarType::F64, literal(value)).evaluate({}).bits != expected ||
            element.substitute("element", literal(value)).evaluate({}).asI64() != value ||
            ScalarExpr::cast(ScalarType::I64, literal(value)).evaluate({}).asI64() != value)
            throw std::runtime_error("physical i64 payload or nearest-even cast mismatch");
        for (const auto &[other, ignored] : cases) {
            for (const auto compare : {ScalarCompare::Equal, ScalarCompare::NotEqual,
                                       ScalarCompare::Less, ScalarCompare::LessEqual}) {
                const bool expectedComparison = compare == ScalarCompare::Equal ? value == other
                    : compare == ScalarCompare::NotEqual ? value != other
                    : compare == ScalarCompare::Less ? value < other : value <= other;
                if (ScalarPredicate{compare, element, literal(other)}.evaluate(bindings) != expectedComparison)
                    throw std::runtime_error("physical i64 comparison lost integer precision");
            }
        }
    }
    if (converted.cuda() != "__ll2double_rn(element)" ||
        literal(INT64_MIN).cuda() != "tiles_i64(9223372036854775808ULL)" ||
        literal(INT64_MAX).cuda() != "tiles_i64(9223372036854775807ULL)" ||
        element.symbols().at("element") != ScalarType::I64)
        throw std::runtime_error("physical i64 CUDA type lost");
    auto rejected = [](auto action) {
        try { action(); } catch (const std::invalid_argument &) { return; }
        throw std::runtime_error("accepted unsupported physical i64 operation");
    };
    const auto wide = literal(INT64_MAX), one = literal(1);
    for (const auto op : {ScalarOp::Add, ScalarOp::Subtract, ScalarOp::Multiply,
                          ScalarOp::Divide, ScalarOp::Minimum, ScalarOp::Maximum,
                          ScalarOp::BitAnd, ScalarOp::BitOr, ScalarOp::BitXor,
                          ScalarOp::LogicalShiftRight})
        rejected([&] { ScalarExpr::apply(op, {wide, one}); });
    for (const auto op : {ScalarOp::Neg, ScalarOp::Abs, ScalarOp::Exp, ScalarOp::Log, ScalarOp::Sqrt})
        rejected([&] { ScalarExpr::apply(op, {wide}); });
    rejected([&] { ScalarExpr::apply(ScalarOp::Fma, {wide, wide, one}); });
    for (const auto type : {ScalarType::F32, ScalarType::F64, ScalarType::I32, ScalarType::U32}) {
        const auto other = ScalarExpr::symbol("other", type);
        rejected([&] { ScalarExpr::apply(ScalarOp::Add, {wide, other}); });
        rejected([&] { ScalarExpr::cast(ScalarType::I64, other); });
        if (type != ScalarType::F64)
            rejected([&] { ScalarExpr::cast(type, wide); });
        rejected([&] { ScalarExpr::bitcast(type, wide); });
        rejected([&] { ScalarExpr::bitcast(ScalarType::I64, other); });
        rejected([&] { ScalarPredicate{ScalarCompare::Equal, wide, other}.evaluate({}); });
        rejected([&] { ScalarPredicate{ScalarCompare::Less, wide, other}.cuda(); });
    }
    rejected([&] { ScalarValue::f64(1).asI64(); });
    rejected([&] { element.substitute("element", ScalarExpr::literal(ScalarValue::f64(0))); });
    std::cout << "tiles scalar i64: exact payloads, comparisons and nearest-even f64 casts passed\n";
}
} // namespace

void checkKernelIR() {
    checkScalarI64();
    checkPointwiseContracts();
    checkReductionContracts();
    checkGeneralReductions();
    checkReductionDomains();
    checkCoreViews();
    checkRegions();
    const auto integer = [](uint32_t value) { return ScalarExpr::literal(ScalarValue::u32(value)); };
    const auto signedInteger = [](int32_t value) { return ScalarExpr::literal(ScalarValue::i32(value)); };
    const auto bits = ScalarExpr::bitcast(ScalarType::U32, ScalarExpr::symbol("x",DType::F32));
    const auto exponent = ScalarExpr::apply(ScalarOp::LogicalShiftRight,
        {ScalarExpr::apply(ScalarOp::BitAnd,{bits,integer(0x7fffffffU)}),integer(23)});
    for(const uint32_t payload : {0U,0x80000000U,1U,0x007fffffU,0x00800000U,0x3f800000U,0x7f800000U,0x7fc00042U}) {
        const std::unordered_map<std::string,ScalarValue> values={{"x",{DType::F32,payload}}};
        if(exponent.evaluate(values).asU32()!=((payload&0x7fffffffU)>>23) ||
           ScalarExpr::bitcast(DType::F32,bits).evaluate(values).bits!=payload)
            throw std::runtime_error("physical bitcast/exponent mismatch");
    }
    if(ScalarExpr::apply(ScalarOp::Add,{signedInteger(INT32_MAX),signedInteger(1)}).evaluate({}).asI32()!=INT32_MIN ||
       ScalarExpr::apply(ScalarOp::Subtract,{integer(0),integer(1)}).evaluate({}).asU32()!=UINT32_MAX ||
       ScalarExpr::apply(ScalarOp::Multiply,{integer(UINT32_MAX),integer(2)}).evaluate({}).asU32()!=UINT32_MAX-1 ||
       ScalarExpr::apply(ScalarOp::LogicalShiftRight,{integer(0x80000000U),integer(63)}).evaluate({}).asU32()!=1 ||
       ScalarExpr::apply(ScalarOp::Minimum,{signedInteger(-149),signedInteger(104)}).evaluate({}).asI32()!=-149)
        throw std::runtime_error("physical integer arithmetic mismatch");
    auto invalidInteger=[](auto action) {
        try { action(); } catch(const std::invalid_argument &) { return; }
        throw std::runtime_error("accepted invalid physical integer expression");
    };
    invalidInteger([&]{ScalarExpr::apply(ScalarOp::Divide,{integer(1),integer(0)});});
    invalidInteger([&]{ScalarExpr::apply(ScalarOp::Add,{integer(1),signedInteger(1)});});
    invalidInteger([&]{ScalarExpr::bitcast(ScalarType::U32,ScalarExpr::literal(ScalarValue::f64(1)));});
    invalidInteger([&]{ScalarExpr::literal({ScalarType::U32,0x100000000ULL});});
    KernelBody integerBody{{{MemorySpace::Shared,ScalarType::I32,0,32,true}},
        {ScalarBinding{"k_low",signedInteger(-149)},
         Store{{0,IndexExpr::variable("thread_x"),Predicate(true)},ScalarExpr::symbol("k_low",ScalarType::I32)},Barrier{}}};
    validateBody(integerBody);
    if(emitBody(integerBody).find("__shared__ int")==std::string::npos)
        throw std::runtime_error("physical integer buffer type lost");
    const auto zero=ScalarExpr::literal(ScalarValue::f32(0));
    const auto i=IndexExpr::variable("k_i");
    KernelBody body{{{MemorySpace::Global,DType::F32,0,17,false},
                     {MemorySpace::Global,DType::F32,1,17,true}},
                   {Loop{"k_i",IndexExpr(0),IndexExpr(17),1,{
                       Load{"k_value",{0,i,Predicate::less(i,IndexExpr(17))},ScalarValue::f32(0)},
                       Store{{1,i,Predicate(true)},ScalarExpr::symbol("k_value",DType::F32)}}},
                    Barrier{}}};
    validateBody(body);
    if(emitBody(body)!=emitBody(body)) throw std::runtime_error("nondeterministic kernel body");
    auto rejected=[&](KernelBody bad,const char *reason) {
        try { validateBody(bad); }
        catch(const std::exception &e) {
            if(std::string(e.what()).find(reason)!=std::string::npos) return;
            throw;
        }
        throw std::runtime_error(std::string("accepted invalid body: ")+reason);
    };
    auto bad=body;
    bad.statements.push_back(Store{{1,IndexExpr(0),Predicate(true)},ScalarExpr::symbol("k_value",DType::F32)});
    rejected(bad,"unbound scalar");
    bad=body; bad.statements.push_back(IndexBinding{"k_bad",i}); rejected(bad,"unbound index");
    bad=body; bad.statements.push_back(Store{{0,IndexExpr(0),Predicate(true)},zero}); rejected(bad,"read-only");
    bad=body; bad.statements.push_back(Store{{2,IndexExpr(0),Predicate(true)},zero}); rejected(bad,"buffer index");
    bad=body; bad.statements.push_back(Store{{1,IndexExpr(0),Predicate(true)},ScalarExpr::literal(ScalarValue::f64(0))}); rejected(bad,"store type");
    bad=body; bad.statements.push_back(Assignment{"k_missing",zero}); rejected(bad,"assignment");
    bad=body; bad.statements.push_back(Loop{"k_zero",IndexExpr(0),IndexExpr(1),0,{}}); rejected(bad,"zero loop step");
    bad=body; bad.statements.push_back(Conditional{Predicate::less(IndexExpr::variable("thread_x"),IndexExpr(1)),{Barrier{}}}); rejected(bad,"divergent");
    bad=body; bad.statements.push_back(IndexBinding{"k_lane",IndexExpr::variable("thread_x")});
    bad.statements.push_back(Loop{"k_j",IndexExpr(0),IndexExpr::variable("k_lane"),1,{Barrier{}}}); rejected(bad,"divergent");
    body.statements.push_back(Conditional{Predicate::equal(IndexExpr::variable("block_x"),IndexExpr(0)),{Barrier{}}});
    validateBody(body);
    const auto f64zero=ScalarExpr::literal(ScalarValue::f64(0));
    KernelBody branches{{}, {ScalarBinding{"k_result",zero},
        Conditional{Predicate::equal(IndexExpr::variable("thread_x"),IndexExpr(0)),
            {ScalarBinding{"k_arm",zero},Assignment{"k_result",ScalarExpr::symbol("k_arm",DType::F32)}},
            {ScalarBinding{"k_arm",ScalarExpr::literal(ScalarValue::f32(1))},Assignment{"k_result",ScalarExpr::symbol("k_arm",DType::F32)}}}}};
    validateBody(branches);
    if(emitBody(branches).find("}else{")==std::string::npos)
        throw std::runtime_error("conditional lost alternative arm");
    auto branchBad=branches;
    std::get<Conditional>(branchBad.statements[1]).otherwise={Barrier{}};
    rejected(branchBad,"divergent");
    branchBad=branches;
    std::get<Conditional>(branchBad.statements[1]).otherwise={Assignment{"k_result",ScalarExpr::symbol("k_arm",DType::F32)}};
    rejected(branchBad,"unbound scalar");
    branchBad=branches;
    std::get<Conditional>(branchBad.statements[1]).otherwise={Return{}};
    branchBad.statements.push_back(Barrier{});
    rejected(branchBad,"reconvergence proof");
    branchBad=branches;
    branchBad.statements.push_back(Assignment{"k_result",ScalarExpr::symbol("k_arm",DType::F32)});
    rejected(branchBad,"unbound scalar");
    const auto x=ScalarExpr::symbol("k_x",DType::F64);
    const ScalarPredicate isZero{ScalarCompare::Equal,x,f64zero};
    if(!isZero.evaluate({{"k_x",ScalarValue::f64(-0.0)}}) ||
       isZero.evaluate({{"k_x",{DType::F64,0x7ff8000000000042ULL}}}))
        throw std::runtime_error("floating fault predicate lost signed-zero or NaN rules");
    KernelBody fault{{{MemorySpace::Global,DType::F64,0,1,false}},
        {Load{"k_x",{0,IndexExpr(0),Predicate(true)},ScalarValue::f64(0)},
         Conditional{isZero,{ReportFault{7},Return{}}}},1};
    validateBody(fault);
    auto faultBad=fault;faultBad.faultSlot.reset();rejected(faultBad,"fault resource is not declared");
    faultBad=fault;faultBad.faultSlot=0;rejected(faultBad,"aliases a value buffer");
    faultBad=fault;faultBad.statements.push_back(ReportFault{UINT32_MAX});rejected(faultBad,"reserved fault source");
    faultBad=fault;faultBad.statements.push_back(Barrier{});rejected(faultBad,"reconvergence proof");
    faultBad=fault;faultBad.statements.push_back(Conditional{ScalarPredicate{ScalarCompare::Equal,x,zero},{}});rejected(faultBad,"comparison type mismatch");
    faultBad=fault;faultBad.statements.push_back(Conditional{ScalarPredicate{ScalarCompare::Less,ScalarExpr::symbol("k_missing",DType::F64),f64zero},{}});rejected(faultBad,"unbound scalar");
    std::cout<<"tiles kernel faults: IEEE conditions, resources and early-exit convergence passed\n";
    std::cout<<"tiles kernel bodies: scopes, typed memory and uniform barriers passed\n";
}

void checkMatrixContracts() {
    namespace sem=camel::core::sem;
    auto &table=camel::core::OperatorTable::instance();
    const std::string uri="tilestest:matrix_contract";
    const auto id=table.intern(uri);
    const auto f32=camel::core::type::TypeCode::Float32;
    sem::OperatorSemantics declaration{.key=sem::OperatorKey::of(uri),
        .laws={{.name="fma",.kind=sem::LawKind::FmaChain,.input=f32,.accumulator=f32,.result=f32}}};
    table.declareSemantics(uri,declaration);
    Program p;
    p.contracts={{.uri=uri,.key=declaration.key,.declaration=table.semantics(id)}};
    p.nodes={{.op="input",.shape={2,2},.data={1,2,3,4}},
             {.op="input",.shape={2,2},.data={5,6,7,8}},
             {.op="matmul",.shape={2,2},.inputs={0,1},.semantic=0}};
    p.outputs={2};
    sem::IndexedInstance inst;
    const auto m=inst.exprs.dim(0),n=inst.exprs.dim(1),k=inst.exprs.dim(2);
    const auto two=inst.exprs.constant(2);
    inst.domain.dims={{"m",two,sem::DimRole::Parallel},{"n",two,sem::DimRole::Parallel},{"k",two,sem::DimRole::Reduction}};
    inst.norm={{.kind=sem::AccessKind::Affine,.coords={m,k}},{.kind=sem::AccessKind::Affine,.coords={k,n}}};
    inst.output={.coords={m,n},.mode=sem::WriteMode::Combine};inst.law=0;
    p.nodes[2].indexing=inst;
    generate(p);
    const auto original=p.source;
    const auto before=generationCacheStats();
    auto transposed=p;
    transposed.nodes[2].indexing->norm[0].coords={k,m};
    generate(transposed);
    if(generationCacheStats().misses!=before.misses+1 || transposed.source==original)
        throw std::runtime_error("core access map was ignored by generation or cache");
    auto originalLaunch=p.launches.front(), transposedLaunch=transposed.launches.front();
    const auto originalBody=matmulBody(p,originalLaunch), transposedBody=matmulBody(transposed,transposedLaunch);
    const auto &originalLoad=std::get<Load>(std::get<Loop>(originalBody.statements[3]).body[0]);
    const auto &transposedLoad=std::get<Load>(std::get<Loop>(transposedBody.statements[3]).body[0]);
    const std::unordered_map<std::string,uint64_t> point={{"k_row",0},{"k_base",0},{"thread_x",1}};
    if(originalLoad.source.evaluate(point)!=1 || transposedLoad.source.evaluate(point)!=2)
        throw std::runtime_error("physical access does not follow core transposition");
    auto rejected=[&](Program bad,const char *reason) {
        try { validatePlan(bad); }
        catch(const std::exception &e) {if(std::string(e.what()).find(reason)!=std::string::npos)return;throw;}
        throw std::runtime_error(std::string("accepted matrix contract: ")+reason);
    };
    auto bad=p;bad.nodes[2].indexing->norm[0].coords={m,n};rejected(bad,"cross-output");
    bad=p;bad.nodes[2].indexing->norm[0].coords[1]=bad.nodes[2].indexing->exprs.add(k,two);rejected(bad,"outside operand");
    bad=p;bad.nodes[2].indexing->norm[0].valid.push_back({m,sem::Constraint::Rel::Lt,two});rejected(bad,"unconditional affine");
    bad=p;bad.nodes[2].indexing->domain.constraints.push_back({two,sem::Constraint::Rel::Lt,two});rejected(bad,"unsatisfied domain");
    bad=p;bad.nodes[2].indexing.reset();rejected(bad,"missing index instance");
    bad=p;bad.nodes[2].indexing->output.coords={n,m};rejected(bad,"direct matrix domain");
    // The mixed GEMM is the one computation the compiler moves to a lower precision on its own, so
    // its refusal names the permission the node is missing instead of saying "mixed GEMM
    // permissions" -- the rule (docs/handover/reviews/questions-2026-10-23.md,
    // "precision-diagnostic-removal"): "`tiles` 拒绝"编译器引入的低精度"时，消息点名缺的是哪个许可
    // （`change_precision` 等），而不是笼统的 "mixed GEMM permissions"。"
    auto mixed=p;
    mixed.nodes[2].permissions=static_cast<sem::PermissionMask>(
        sem::kReassociate|sem::kChangePrecision|sem::kFiniteMath);
    generate(mixed);
    if(mixed.launches.size()!=1||mixed.launches.front().implementation!="cublas_bf16")
        throw std::runtime_error("the three permissions did not select the mixed GEMM");
    auto missingPermission=mixed;
    missingPermission.nodes[2].permissions=static_cast<sem::PermissionMask>(
        sem::kReassociate|sem::kFiniteMath);
    rejected(missingPermission,"missing change_precision");
    declaration.laws[0].kind=sem::LawKind::MulAdd;
    table.declareSemantics(uri,declaration);
    p.contracts[0].declaration=table.semantics(id);
    rejected(p,"ascending f32 FMA");
    std::cout<<"tiles matrix contracts: core maps, FMA law and cache dependency passed\n";
}

void checkPointwiseContracts() {
    namespace sem=camel::core::sem;
    auto &table=camel::core::OperatorTable::instance();
    const std::string uri="tilestest:pointwise_contract";
    const auto id=table.intern(uri);
    sem::OperatorSemantics declaration{.key=sem::OperatorKey::of(uri)};
    table.declareSemantics(uri,declaration);
    Program p;
    p.contracts={{.uri=uri,.key=declaration.key,.declaration=table.semantics(id)}};
    p.nodes={{.op="input",.shape={2,2},.data={1,2,3,4}},
             {.op="add",.shape={2,2},.inputs={0,0},.semantic=0}};
    p.outputs={1};
    sem::IndexedInstance inst;
    const auto row=inst.exprs.dim(0), col=inst.exprs.dim(1), two=inst.exprs.constant(2);
    inst.domain.dims={{"row",two,sem::DimRole::Parallel},{"col",two,sem::DimRole::Parallel}};
    inst.norm={{.kind=sem::AccessKind::Affine,.coords={row,col}},
               {.kind=sem::AccessKind::Affine,.coords={row,col}}};
    inst.output={.coords={row,col}};
    p.nodes[1].indexing=inst;
    generate(p);
    if(!canonicalPointwise(p,p.nodes[1])) throw std::runtime_error("canonical core broadcast rejected");
    auto transposed=p;
    transposed.nodes[1].indexing->norm[1].coords={col,row};
    const auto before=generationCacheStats();
    generate(transposed);
    if(transposed.source==p.source || generationCacheStats().misses!=before.misses+1)
        throw std::runtime_error("pointwise core map ignored by source cache");
    if(pointwiseAddress(transposed,transposed.nodes[1],0,IndexExpr(1)).evaluate({})!=1 ||
       pointwiseAddress(transposed,transposed.nodes[1],1,IndexExpr(1)).evaluate({})!=2 ||
       canonicalPointwise(transposed,transposed.nodes[1]))
        throw std::runtime_error("operand-position mapping or row-fusion eligibility lost");
    auto rejected=[&](Program bad,const char *message) {
        try { validatePlan(bad); } catch(const std::exception &e) {
            if(std::string(e.what()).find(message)!=std::string::npos) return;
            throw;
        }
        throw std::runtime_error(std::string("accepted invalid pointwise contract: ")+message);
    };
    auto bad=p;
    bad.nodes[1].indexing->norm[0].coords[0]=bad.nodes[1].indexing->exprs.add(row,two);
    rejected(bad,"outside operand domain");
    bad=p;bad.nodes[1].indexing->output.coords={col,row};
    rejected(bad,"output is not the pointwise domain");
    bad=p;bad.nodes[1].indexing->norm[0].valid.push_back({row,sem::Constraint::Rel::Lt,two});
    rejected(bad,"unconditional affine");
    Program reduction=p;
    reduction.nodes[0].shape={2,2,2};reduction.nodes[0].data={1,2,3,4,5,6,7,8};
    reduction.nodes[1]=Node{.op="sum_axis",.shape={2,2},.inputs={0},.attributes={2}};
    auto epilogue=p.nodes[1];epilogue.op="neg";epilogue.inputs={1};epilogue.indexing->norm.resize(1);
    reduction.nodes.push_back(epilogue);reduction.outputs={2};
    attachNativeReduction(reduction,1);
    generate(reduction);
    if(reduction.launches.size()!=1 || reduction.launches[0].implementation!="ordered_reduce")
        throw std::runtime_error("canonical reduction epilogue did not fuse");
    reduction.nodes[2].indexing->norm[0].coords={col,row};
    rejected(reduction,"ordered reduction epilogue domain");
    generate(reduction);
    if(reduction.launches.size()!=2)
        throw std::runtime_error("cross-row epilogue incorrectly fused");
    std::cout<<"tiles pointwise contracts: operand maps, bounds and row eligibility passed\n";
}

void checkReductionContracts() {
    namespace sem=camel::core::sem;
    const auto f32=camel::core::type::TypeCode::Float32;
    const auto f64=camel::core::type::TypeCode::Float64;
    const std::string uri="tilestest:axis_contract";
    auto &table=camel::core::OperatorTable::instance();
    const auto id=table.intern(uri);
    sem::OperatorSemantics declaration{.key=sem::OperatorKey::of(uri)};
    declaration.laws={{.name="sum",.kind=sem::LawKind::Add,.input=f32,.accumulator=f64,.result=f32}};
    table.declareSemantics(uri,declaration);
    Program p;
    p.contracts={{.uri=uri,.key=declaration.key,.declaration=table.semantics(id)}};
    p.nodes={{.op="input",.shape={2,2},.data={1,2,3,4}},
             {.op="sum_axis",.shape={2,1},.inputs={0},.attributes={1},.semantic=0}};
    p.outputs={1};
    sem::IndexedInstance inst;
    const auto row=inst.exprs.dim(0), col=inst.exprs.dim(1), two=inst.exprs.constant(2), zero=inst.exprs.constant(0);
    inst.domain.dims={{"row",two,sem::DimRole::Parallel},{"column",two,sem::DimRole::Reduction}};
    inst.norm={{.kind=sem::AccessKind::Affine,.coords={row,col}},{.kind=sem::AccessKind::None}};
    inst.output={.coords={row,zero},.mode=sem::WriteMode::Combine};inst.law=0;
    p.nodes[1].indexing=inst;
    generate(p);
    auto transposed=p;transposed.nodes[1].indexing->norm[0].coords={col,row};
    const auto before=generationCacheStats();generate(transposed);
    if(p.source==transposed.source || generationCacheStats().misses!=before.misses+1 ||
       !canonicalAxisReduction(p,p.nodes[1]) || canonicalAxisReduction(transposed,transposed.nodes[1]))
        throw std::runtime_error("reduction access map ignored by source or row eligibility");
    for(size_t output=0;output<2;++output)
        for(size_t r=0;r<2;++r)
            if(reductionAccess(transposed,transposed.nodes[1],IndexExpr(output),IndexExpr(r)).address.evaluate({})!=r*2+output)
                throw std::runtime_error("reduction mapped traversal is incorrect");
    auto rejected=[](Program bad,const char *message) {
        try { validatePlan(bad); } catch(const std::exception &e) {
            if(std::string(e.what()).find(message)!=std::string::npos) return;
            throw;
        }
        throw std::runtime_error(std::string("accepted invalid reduction contract: ")+message);
    };
    auto bad=p;bad.nodes[1].indexing->norm[0].coords[0]=bad.nodes[1].indexing->exprs.add(row,two);
    rejected(bad,"outside operand domain");
    bad=p;bad.nodes[1].indexing->output.coords={zero,row};rejected(bad,"constant output coordinate has nonunit extent");
    bad=p;bad.nodes[1].indexing->norm[0].valid.push_back({row,sem::Constraint::Rel::Lt,two});
    rejected(bad,"unconditional affine");
    auto otherLaw=declaration;otherLaw.laws[0].accumulator=f32;
    bad=p;bad.contracts[0].declaration=&otherLaw;
    bool lawRejected=false;
    try { (void)reductionAccess(bad,bad.nodes[1],IndexExpr(0),IndexExpr(0)); }
    catch(const std::exception &e) { lawRejected=std::string(e.what()).find("unsupported numerical law")!=std::string::npos; }
    if(!lawRejected) throw std::runtime_error("accepted incompatible accumulator law");
    std::cout<<"tiles axis contracts: mapped traversal, numerical law and row eligibility passed\n";
}

void checkGeneralReductions() {
    namespace sem=camel::core::sem;
    const auto f32=camel::core::type::TypeCode::Float32, f64=camel::core::type::TypeCode::Float64;
    auto &table=camel::core::OperatorTable::instance();
    auto make=[&](bool to) {
        const std::string uri=to ? "tilestest:sum_to_map" : "tilestest:sum_all_map";
        const auto id=table.intern(uri);
        sem::OperatorSemantics decl{.key=sem::OperatorKey::of(uri)};
        decl.laws={{.name="sum",.kind=sem::LawKind::Add,.input=f32,.accumulator=to?f32:f64,.result=to?f32:f64}};
        table.declareSemantics(uri,decl);
        Program p;
        p.contracts={{.uri=uri,.key=decl.key,.declaration=table.semantics(id)}};
        p.nodes={{.op="input",.shape={2,3,2},.data=std::vector<float>(12)},
                 {.op=to?"sum_to":"sum_all",.shape=to?Shape{3,1}:Shape{},.inputs={0},.dtype=to?DType::F32:DType::F64,.semantic=0}};
        p.outputs={1};
        sem::IndexedInstance inst;
        const auto a=inst.exprs.dim(0), b=inst.exprs.dim(1), c=inst.exprs.dim(2);
        const auto two=inst.exprs.constant(2), three=inst.exprs.constant(3), zero=inst.exprs.constant(0);
        inst.domain.dims={{"a",two,sem::DimRole::Reduction},{"b",three,to?sem::DimRole::Parallel:sem::DimRole::Reduction},{"c",two,sem::DimRole::Reduction}};
        inst.norm={{.kind=sem::AccessKind::Affine,.coords={a,b,c}}};
        if(to) inst.norm.push_back({.kind=sem::AccessKind::None});
        inst.output={.coords=to?std::vector<sem::ExprId>{b,zero}:std::vector<sem::ExprId>{},.mode=sem::WriteMode::Combine};
        inst.law=0;p.nodes[1].indexing=inst;
        return p;
    };
    for(bool to:{false,true}) {
        auto p=make(to);generate(p);
        auto changed=p;
        auto &inst=*changed.nodes[1].indexing;
        std::swap(inst.norm[0].coords[0],inst.norm[0].coords[2]);
        const auto before=generationCacheStats();generate(changed);
        if(p.source==changed.source || generationCacheStats().misses!=before.misses+1)
            throw std::runtime_error("general reduction maps not represented in CUDA/cache");
        const size_t width=to?4:12, rows=to?3:1;
        for(size_t row=0;row<rows;++row) for(size_t r=0;r<width;++r) {
            const auto ordinary=reductionAccess(p,p.nodes[1],IndexExpr(row),IndexExpr(r));
            const auto mapped=reductionAccess(changed,changed.nodes[1],IndexExpr(row),IndexExpr(r));
            const auto i=to?r/2:r/6, j=to?row:(r/2)%3, k=r%2;
            if(ordinary.count!=width || mapped.count!=width || ordinary.address.evaluate({})!=i*6+j*2+k || mapped.address.evaluate({})!=k*6+j*2+i)
                throw std::runtime_error("general reduction traversal lost row-major contribution order");
        }
        auto bad=p;bad.nodes[1].indexing->domain.dims[0].role=sem::DimRole::Parallel;
        bool rejected=false;try { validatePlan(bad); } catch(const std::exception &) { rejected=true; }
        if(!rejected) throw std::runtime_error("general reduction accepted incompatible dimension roles");
    }
    std::cout<<"tiles general reductions: scalar and sum-to core maps preserve contribution order\n";
}

void checkRegions() {
    auto require=[](bool ok) {if(!ok) throw std::runtime_error("invalid dependency-convex region partition");};
    // IDs are deliberately not topological; duplicated operands remain one boundary value.
    // `order` is empty here: these graphs order nothing for ordering's own sake.
    std::vector<RegionNode> chain={{{2,2},{},true,true},{{},{},false,false},{{1},{},true,false},{{},{},true,false}};
    auto regions=partitionRegions(chain);
    require(regions.size()==1 && regions[0].members==std::vector<size_t>{2,0} &&
            regions[0].inputs==std::vector<size_t>{1} && regions[0].outputs==std::vector<size_t>{0});
    // Contracting 1 and 3 would create R -> CPU2 -> R. The direct edge cannot justify it.
    std::vector<RegionNode> detour={{{},{},false,false},{{0},{},true,false},{{1},{},false,false},{{1,2},{},true,true}};
    regions=partitionRegions(detour);
    require(regions.size()==2 && regions[0].members==std::vector<size_t>{1} &&
            regions[1].members==std::vector<size_t>{3} && regions[1].inputs==std::vector<size_t>({1,2}));
    auto transfers=planRegionTransfers(detour,regions);
    require(transfers.uploads==std::vector<size_t>({0,2}) &&
            transfers.downloads==std::vector<size_t>({1,3}) && transfers.resident.size()==1);
    require(transfers.resident[0].value==1 && transfers.resident[0].producer==0 && transfers.resident[0].consumer==1);
    auto invalidTransfers=[&](std::vector<Segment> bad) {
        try { (void)planRegionTransfers(detour,bad); }
        catch(const std::exception&) { return; }
        throw std::runtime_error("invalid transfer boundary accepted");
    };
    auto badRegions=regions; badRegions[0].outputs.clear(); invalidTransfers(badRegions);
    badRegions=regions; badRegions[1].members.push_back(1); invalidTransfers(badRegions);
    // Multiple regions share one host input; unused CPU consumers must not trigger downloads.
    std::vector<RegionNode> split={{{},{},false,false},{{0},{},true,false},{{0,1},{},true,true},{{1},{},false,false}};
    std::vector<Segment> splitRegions={{{1},{0},{1}},{{2},{0,1},{2}}};
    transfers=planRegionTransfers(split,splitRegions);
    require(transfers.uploads==std::vector<size_t>{0} && transfers.downloads==std::vector<size_t>{2});
    split[0].embedded=true;
    require(planRegionTransfers(split,splitRegions).uploads.empty());
    // Shared intermediates exported to CPU remain exits even when the GPU chain fuses.
    std::vector<RegionNode> shared={{{},{},false,false},{{0},{},true,false},{{1},{},true,true},{{1},{},false,true}};
    regions=partitionRegions(shared);
    require(regions.size()==1 && regions[0].outputs==std::vector<size_t>({1,2}));
    // A CPU diamond prevents a contraction only if it leaves and re-enters the same region.
    std::vector<RegionNode> diamond={{{},{},false,false},{{0},{},true,false},{{0},{},false,false},{{1,2},{},true,true}};
    regions=partitionRegions(diamond);require(regions.size()==1 && regions[0].inputs==std::vector<size_t>({0,2}));
    auto rejects=[&](std::vector<RegionNode> graph) {try{(void)partitionRegions(graph);}catch(const std::exception&){return;}throw std::runtime_error("invalid region graph accepted");};
    rejects({{{1},{},true,true}});rejects({{{0},{},true,true}});rejects({{{1},{},true,true},{{0},{},false,false}});
    require(partitionRegions({{{},{},true,false}}).empty());
    // An ordering edge is a dependency without a value: it keeps the node it points at live and
    // ordered like any other edge, and a region boundary carries no value across it.
    std::vector<RegionNode> ordered={{{},{},true,false},{{},{0},true,true}};
    regions=partitionRegions(ordered);
    if(!(regions.size()==2 &&
         std::any_of(regions.begin(),regions.end(),[](const Segment &r){return r.members==std::vector<size_t>{0};})))
        throw std::runtime_error("an ordering edge did not keep its predecessor live");
    // Two control regions never contract into one, however eligible the nodes are.
    std::vector<RegionNode> arms={{{},{},true,false},{{0},{},true,true}};
    arms[0].control=1;arms[1].control=2;
    regions=partitionRegions(arms);
    if(regions.size()!=2) throw std::runtime_error("a region spanned two control regions");
    arms[1].control=1;
    regions=partitionRegions(arms);
    if(regions.size()!=1 || regions[0].members.size()!=2) throw std::runtime_error("one control region did not contract");
    std::cout<<"tiles regions: convexity, boundaries, liveness and dependency order passed\n";
}
