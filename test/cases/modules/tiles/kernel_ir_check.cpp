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
    std::cout<<"tiles kernel bodies: scopes, typed memory and uniform barriers passed\n";
}
