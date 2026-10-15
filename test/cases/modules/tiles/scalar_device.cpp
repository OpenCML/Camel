/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Differential check of typed arithmetic against the generated CUDA instructions.
#include "tiles/internal/scalar_expr.h"
#include "tiles/internal/cuda_backend.h"
#include "tiles/internal/cuda_resources.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using namespace camel::tiles;
using namespace camel::tiles::kernel;
using namespace camel::tiles::cuda_detail;

void testScalarDevice() {
    auto backend = std::make_shared<Backend>();
    auto &api = backend->api;
    Current current(api, backend->context);
    for (const auto type : {DType::F32, DType::F64}) {
        const bool f32 = type == DType::F32;
        const auto a = ScalarExpr::symbol("a",type), b = ScalarExpr::symbol("b",type), c = ScalarExpr::symbol("c",type);
        const std::vector<ScalarExpr> expressions = {
            ScalarExpr::apply(ScalarOp::Add,{a,b}), ScalarExpr::apply(ScalarOp::Subtract,{a,b}),
            ScalarExpr::apply(ScalarOp::Multiply,{a,b}), ScalarExpr::apply(ScalarOp::Divide,{a,b}),
            ScalarExpr::apply(ScalarOp::Sqrt,{ScalarExpr::apply(ScalarOp::Abs,{a})}),
            ScalarExpr::apply(ScalarOp::Fma,{a,b,c}),
            ScalarExpr::apply(ScalarOp::Add,{ScalarExpr::apply(ScalarOp::Multiply,{a,b}),c}),
            ScalarExpr::apply(ScalarOp::Maximum,{a,b}), ScalarExpr::apply(ScalarOp::Minimum,{a,b}),
            ScalarExpr::cast(type, ScalarExpr::cast(f32 ? DType::F64 : DType::F32,a))};
        constexpr size_t count = 256;
        std::vector<uint64_t> input(count*3), output(count*expressions.size());
        uint64_t random = 0x47c23a918f056dbeULL;
        for (auto &bits : input) {
            random ^= random << 13; random ^= random >> 7; random ^= random << 17;
            bits = f32 ? random & 0xff7fffffULL : random & 0xffefffffffffffffULL;
        }
        const std::array<uint64_t,10> special = f32
            ? std::array<uint64_t,10>{0,0x80000000U,1,0x007fffffU,0x00800000U,0x7f7fffffU,0x7f800000U,0xff800000U,0x7fc01234U,0xffc05678U}
            : std::array<uint64_t,10>{0,0x8000000000000000ULL,1,0x000fffffffffffffULL,0x0010000000000000ULL,0x7fefffffffffffffULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff8000000001234ULL,0xfff8000000005678ULL};
        for (size_t i=0;i<special.size();++i) for (size_t j=0;j<special.size();++j) {
            const size_t row=i*special.size()+j;
            input[row*3]=special[i]; input[row*3+1]=special[j]; input[row*3+2]=special[(i+j)%special.size()];
        }
        std::string source = scalarCudaPreamble()+"\nextern \"C\" __global__ void scalar_probe(const unsigned long long* x,unsigned long long* y){unsigned i=threadIdx.x;\n";
        for (size_t arg=0;arg<3;++arg) {
            source += std::string(f32 ? "float " : "double ")+char('a'+arg)+"="+
                (f32 ? "__uint_as_float((unsigned)x[" : "__longlong_as_double(x[")+"i*3+"+std::to_string(arg)+"]);\n";
        }
        for (size_t op=0;op<expressions.size();++op)
            source += "y[i*"+std::to_string(expressions.size())+"+"+std::to_string(op)+"]="+
                (f32 ? "__float_as_uint(" : "(unsigned long long)__double_as_longlong(")+expressions[op].cuda()+");\n";
        source += "}\n";
        auto lease = backend->module(source);
        Resources resources(backend);
        auto x=resources.allocate(input.size()*sizeof(uint64_t)), y=resources.allocate(output.size()*sizeof(uint64_t));
        api.check(api.cuMemcpyHtoD(x,input.data(),input.size()*sizeof(uint64_t)),"upload scalar oracle inputs");
        CUfunction kernel;
        api.check(api.cuModuleGetFunction(&kernel,lease.compiled->module,"scalar_probe"),"resolve scalar oracle");
        void *args[]={&x,&y};
        api.check(api.cuLaunchKernel(kernel,1,1,1,count,1,1,0,resources.stream,args,nullptr),"launch scalar oracle");
        api.check(api.cuStreamSynchronize(resources.stream),"complete scalar oracle");
        api.check(api.cuMemcpyDtoH(output.data(),y,output.size()*sizeof(uint64_t)),"read scalar oracle");
        for (size_t i=0;i<count;++i) {
            const std::unordered_map<std::string,ScalarValue> bindings = {{"a",{type,input[i*3]}},{"b",{type,input[i*3+1]}},{"c",{type,input[i*3+2]}}};
            for (size_t op=0;op<expressions.size();++op) {
                const auto expected=expressions[op].evaluate(bindings);
                const ScalarValue actual{type,output[i*expressions.size()+op]};
                // Only extrema define canonical payloads; other arithmetic compares NaN classification.
                const bool nanPair = op != 7 && op != 8 && (f32
                    ? std::isnan(expected.asF32()) && std::isnan(actual.asF32())
                    : std::isnan(expected.asF64()) && std::isnan(actual.asF64()));
                if (expected.bits != actual.bits && !nanPair)
                    throw std::runtime_error("scalar device mismatch type="+std::to_string(f32?32:64)+" row="+std::to_string(i)+" op="+std::to_string(op));
            }
        }
    }
    std::cout << "tiles scalar device: 5120 typed host/CUDA results matched\n";
}
