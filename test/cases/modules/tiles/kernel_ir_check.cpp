/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Structural mutation tests for physical kernel types, scopes and convergence.
#include "tiles/internal/kernel_ir.h"
#include "tiles/internal/codegen_cache.h"
#include "tiles/program.h"
#include "camel/core/operator_table.h"
#include <iostream>
#include <stdexcept>
using namespace camel::tiles;
using namespace camel::tiles::kernel;

void checkKernelIR() {
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
    declaration.laws[0].kind=sem::LawKind::MulAdd;
    table.declareSemantics(uri,declaration);
    p.contracts[0].declaration=table.semantics(id);
    rejected(p,"ascending f32 FMA");
    std::cout<<"tiles matrix contracts: core maps, FMA law and cache dependency passed\n";
}
