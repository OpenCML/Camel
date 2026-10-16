/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Validation and final CUDA printing for structured physical statements.
#include "tiles/internal/kernel_ir.h"
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace camel::tiles::kernel {
namespace {
struct Binding {
    bool index;
    DType type;
    bool uniform;
};
using Scope          = std::unordered_map<std::string, Binding>;
const Scope builtins = {
    {"thread_x", {true, DType::F32, false}},
    {"thread_y", {true, DType::F32, false}},
    {"thread_z", {true, DType::F32, false}},
    {"block_x", {true, DType::F32, true}},
    {"block_y", {true, DType::F32, true}},
    {"block_z", {true, DType::F32, true}}};
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(std::string("kernel body: ") + message);
}
void declare(Scope &scope, const std::string &name, Binding binding) {
    (void)IndexExpr::variable(name); // Validate spelling before emitting any source.
    require(name.starts_with("k_"), "local names must use k_ prefix");
    require(scope.emplace(name, binding).second, "duplicate binding");
}
template <class Expr> bool indices(const Expr &expr, const Scope &scope) {
    bool uniform = true;
    for (const auto &name : expr.symbols()) {
        auto it = scope.find(name);
        require(it != scope.end() && it->second.index, "unbound index");
        uniform = uniform && it->second.uniform;
    }
    return uniform;
}
void scalar(const ScalarExpr &expr, const Scope &scope) {
    for (const auto &[name, type] : expr.symbols()) {
        auto it = scope.find(name);
        require(it != scope.end() && !it->second.index, "unbound scalar");
        require(it->second.type == type, "scalar binding type");
    }
}
const Buffer &access(const KernelBody &kernel, const Access &a, const Scope &scope) {
    require(a.buffer < kernel.buffers.size(), "buffer index");
    indices(a.address, scope);
    indices(a.mask, scope);
    return kernel.buffers[a.buffer];
}
void validateBlock(const KernelBody &kernel, const Block &block, Scope scope, bool uniform) {
    for (const auto &statement : block)
        std::visit(
            [&](const auto &s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, IndexBinding>) {
                    const bool u = indices(s.value, scope);
                    declare(scope, s.name, {true, DType::F32, u});
                } else if constexpr (std::is_same_v<T, ScalarBinding>) {
                    scalar(s.value, scope);
                    declare(scope, s.name, {false, s.value.type(), false});
                } else if constexpr (std::is_same_v<T, Assignment>) {
                    scalar(s.value, scope);
                    auto it = scope.find(s.name);
                    require(
                        it != scope.end() && !it->second.index && it->second.type == s.value.type(),
                        "assignment type or binding");
                } else if constexpr (std::is_same_v<T, Load>) {
                    const auto &buffer = access(kernel, s.source, scope);
                    require(buffer.type == s.fallback.type, "load fallback type");
                    declare(scope, s.name, {false, buffer.type, false});
                } else if constexpr (std::is_same_v<T, Store>) {
                    const auto &buffer = access(kernel, s.target, scope);
                    scalar(s.value, scope);
                    require(buffer.writable, "store to read-only buffer");
                    require(buffer.type == s.value.type(), "store type");
                } else if constexpr (std::is_same_v<T, Loop>) {
                    const bool beginUniform = indices(s.begin, scope),
                               endUniform   = indices(s.end, scope);
                    require(s.step > 0, "zero loop step");
                    auto nested = scope;
                    declare(nested, s.index, {true, DType::F32, beginUniform && endUniform});
                    validateBlock(
                        kernel,
                        s.body,
                        std::move(nested),
                        uniform && beginUniform && endUniform);
                } else if constexpr (std::is_same_v<T, Conditional>) {
                    const bool u = std::visit(
                        [&](const auto &condition) {
                            using C = std::decay_t<decltype(condition)>;
                            if constexpr (std::is_same_v<C, Predicate>)
                                return indices(condition, scope);
                            else {
                                scalar(condition.lhs, scope);
                                scalar(condition.rhs, scope);
                                (void)
                                    condition.cuda(); // Validate operand types and comparison kind.
                                return false;         // Mutable scalar data is conservatively
                                                      // thread-dependent.
                            }
                        },
                        s.condition);
                    validateBlock(kernel, s.body, scope, uniform && u);
                    validateBlock(kernel, s.otherwise, scope, uniform && u);
                } else if constexpr (std::is_same_v<T, Barrier>) {
                    require(uniform, "barrier under divergent control flow");
                } else if constexpr (std::is_same_v<T, ReportFault>) {
                    require(kernel.faultSlot.has_value(), "fault resource is not declared");
                    require(s.source != UINT32_MAX, "reserved fault source");
                }
            },
            statement);
}
std::string typeName(DType type) { return type == DType::F32 ? "float" : "double"; }
std::string location(const KernelBody &kernel, const Access &access) {
    const auto &b   = kernel.buffers[access.buffer];
    const auto base = b.space == MemorySpace::Shared ? "shared_" + std::to_string(access.buffer)
                                                     : "reinterpret_cast<" + typeName(b.type) +
                                                           "*>(v[" + std::to_string(b.slot) + "])";
    return base + "[" + access.address.cuda() + "]";
}
void printBlock(std::ostream &out, const KernelBody &kernel, const Block &block) {
    for (const auto &statement : block)
        std::visit(
            [&](const auto &s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, IndexBinding>)
                    out << "const unsigned long long " << s.name << "=" << s.value.cuda() << ";\n";
                else if constexpr (std::is_same_v<T, ScalarBinding>)
                    out << typeName(s.value.type()) << " " << s.name << "=" << s.value.cuda()
                        << ";\n";
                else if constexpr (std::is_same_v<T, Assignment>)
                    out << s.name << "=" << s.value.cuda() << ";\n";
                else if constexpr (std::is_same_v<T, Load>)
                    out << typeName(s.fallback.type) << " " << s.name << "=("
                        << s.source.mask.cuda() << "?" << location(kernel, s.source) << ":"
                        << ScalarExpr::literal(s.fallback).cuda() << ");\n";
                else if constexpr (std::is_same_v<T, Store>)
                    out << "if(" << s.target.mask.cuda() << ") " << location(kernel, s.target)
                        << "=" << s.value.cuda() << ";\n";
                else if constexpr (std::is_same_v<T, Loop>) {
                    if (s.unroll)
                        out << "#pragma unroll\n";
                    out << "for(unsigned long long " << s.index << "=" << s.begin.cuda() << ";"
                        << s.index << "<" << s.end.cuda() << ";" << s.index << "+=" << s.step
                        << "ULL){\n";
                    printBlock(out, kernel, s.body);
                    out << "}\n";
                } else if constexpr (std::is_same_v<T, Conditional>) {
                    out << "if("
                        << std::visit(
                               [](const auto &condition) { return condition.cuda(); },
                               s.condition)
                        << "){\n";
                    printBlock(out, kernel, s.body);
                    if (!s.otherwise.empty()) {
                        out << "}else{\n";
                        printBlock(out, kernel, s.otherwise);
                    }
                    out << "}\n";
                } else if constexpr (std::is_same_v<T, Barrier>)
                    out << "__syncthreads();\n";
                else if constexpr (std::is_same_v<T, ReportFault>)
                    out << "atomicMin(reinterpret_cast<unsigned*>(v[" << *kernel.faultSlot << "]),"
                        << s.source << "U);\n";
                else if constexpr (std::is_same_v<T, Return>)
                    out << "return;\n";
            },
            statement);
}
// Without a reconvergence proof, any early exit may strand peers at a later
// barrier (even when the return itself is nested in a different branch).
void controlKinds(const Block &block, bool &returns, bool &barriers) {
    for (const auto &statement : block)
        std::visit(
            [&](const auto &s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, Return>)
                    returns = true;
                else if constexpr (std::is_same_v<T, Barrier>)
                    barriers = true;
                else if constexpr (std::is_same_v<T, Loop> || std::is_same_v<T, Conditional>)
                    controlKinds(s.body, returns, barriers);
                if constexpr (std::is_same_v<T, Conditional>)
                    controlKinds(s.otherwise, returns, barriers);
            },
            statement);
}
} // namespace
void validateBody(const KernelBody &body) {
    for (const auto &b : body.buffers) {
        require(b.type == DType::F32 || b.type == DType::F64, "buffer type");
        require(b.space == MemorySpace::Global || b.space == MemorySpace::Shared, "memory space");
        require(b.space != MemorySpace::Shared || b.extent > 0, "empty shared buffer");
    }
    bool returns = false, barriers = false;
    controlKinds(body.statements, returns, barriers);
    require(
        !(returns && barriers),
        "early return in a barrier kernel requires reconvergence proof");
    if (body.faultSlot)
        for (const auto &buffer : body.buffers)
            require(
                buffer.space != MemorySpace::Global || buffer.slot != *body.faultSlot,
                "fault resource aliases a value buffer");
    validateBlock(body, body.statements, builtins, true);
}
std::string emitBody(const KernelBody &body) {
    validateBody(body);
    std::ostringstream out;
    for (const std::string name :
         {"thread_x", "thread_y", "thread_z", "block_x", "block_y", "block_z"}) {
        const bool thread = name.starts_with("thread");
        out << "const unsigned long long " << name << "=" << (thread ? "threadIdx." : "blockIdx.")
            << name.back() << ";\n";
    }
    for (size_t i = 0; i < body.buffers.size(); ++i) {
        const auto &b = body.buffers[i];
        if (b.space == MemorySpace::Shared)
            out << "__shared__ " << typeName(b.type) << " shared_" << i << "[" << b.extent
                << "];\n";
    }
    printBlock(out, body, body.statements);
    return out.str();
}
} // namespace camel::tiles::kernel
