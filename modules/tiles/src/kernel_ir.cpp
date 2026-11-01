// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Validation and final CUDA printing for structured physical statements.

#include "tiles/internal/kernel_ir.h"
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

namespace camel::tiles::kernel {
namespace {
struct Binding {
    bool index;
    ScalarType type;
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
    // An index read is spelled by its loop variables, so it is checked as an index rather than
    // as a symbol: a name that is not a loop in scope is still an unbound index.
    if (const auto *index = expr.index()) {
        indices(*index, scope);
        return;
    }
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
std::string typeName(ScalarType type) {
    switch (type.kind) {
    case ScalarType::F32:
        return "float";
    case ScalarType::F64:
        return "double";
    case ScalarType::I32:
        return "int";
    case ScalarType::U32:
        return "unsigned";
    case ScalarType::I64:
        return "long long";
    case ScalarType::F16:
    case ScalarType::BF16:
        // A narrow element is a physical type the body holds two bytes of. What it has no spelling
        // for is a device register: `__nv_bfloat16` would also be a device contract nobody wrote yet,
        // so the printer refuses rather than naming one.
        throw std::runtime_error("no CUDA spelling for a narrow element yet");
    }
    throw std::runtime_error("invalid physical scalar type");
}
// Whether the body can hold elements of this type at all, which is a different question from
// whether the printer can spell one: validation asks this one.
bool physical(ScalarType type) {
    switch (type.kind) {
    case ScalarType::F32:
    case ScalarType::F64:
    case ScalarType::F16:
    case ScalarType::BF16:
    case ScalarType::I32:
    case ScalarType::U32:
    case ScalarType::I64:
        return true;
    }
    return false;
}
std::string location(const KernelBody &kernel, const Access &access) {
    const auto &b = kernel.buffers[access.buffer];
    const auto base =
        b.space == MemorySpace::Constant ? "constant_" + std::to_string(access.buffer)
        : b.space == MemorySpace::Shared
            ? "shared_" + std::to_string(access.buffer)
            : "reinterpret_cast<" + typeName(b.type) + "*>(v[" + std::to_string(b.slot) + "])";
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
        require(physical(b.type), "invalid physical scalar type");
        require(
            b.space == MemorySpace::Global || b.space == MemorySpace::Shared ||
                b.space == MemorySpace::Constant,
            "memory space");
        require(b.space != MemorySpace::Shared || b.extent > 0, "empty shared buffer");
        if (b.space == MemorySpace::Constant) {
            require(
                !b.writable && b.constants.size() == b.extent && b.extent > 0,
                "invalid static table");
            for (const auto &value : b.constants)
                require(value.type == b.type, "static table element type");
        } else
            require(b.constants.empty(), "nonconstant buffer has static payload");
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
        else if (b.space == MemorySpace::Constant) {
            out << "const " << typeName(b.type) << " constant_" << i << "[" << b.extent << "]={";
            for (size_t j = 0; j < b.constants.size(); ++j) {
                if (j)
                    out << ',';
                out << ScalarExpr::literal(b.constants[j]).cuda();
            }
            out << "};\n";
        }
    }
    printBlock(out, body, body.statements);
    return out.str();
}
} // namespace camel::tiles::kernel
