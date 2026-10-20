/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Structured physical kernel bodies. No raw-source statement escape hatch.
#pragma once
#include "tiles/internal/access.h"
#include "tiles/internal/scalar_expr.h"
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace camel::tiles {
struct Program;
struct Launch;
struct Node;
} // namespace camel::tiles
namespace camel::tiles::kernel {
enum class MemorySpace { Global, Shared, Constant };
struct Buffer {
    MemorySpace space;
    ScalarType type;
    size_t slot; // Global: the Program node; shared: local identity.
    size_t extent;
    bool writable;
    std::vector<ScalarValue> constants; // Typed immutable static table, no float staging.
};
struct IndexBinding {
    std::string name;
    IndexExpr value;
};
struct ScalarBinding {
    std::string name;
    ScalarExpr value;
};
struct Assignment {
    std::string name;
    ScalarExpr value;
};
struct Load {
    std::string name;
    Access source;
    ScalarValue fallback;
};
struct Store {
    Access target;
    ScalarExpr value;
};
struct Statement;
using Block = std::vector<Statement>;
struct Loop {
    std::string index;
    IndexExpr begin, end;
    uint64_t step;
    Block body;
    bool unroll = false;
};
struct Conditional {
    std::variant<Predicate, ScalarPredicate> condition;
    Block body;
    // Both arms have independent lexical scopes and share the incoming bindings.
    Block otherwise;
};
struct Barrier {};
// Device faults use a separately declared unsigned status word. Smaller source
// ordinals win; reporting and termination are separate structured operations.
struct ReportFault {
    uint32_t source;
};
struct Return {};
struct Statement : std::variant<
                       IndexBinding, ScalarBinding, Assignment, Load, Store, Loop, Conditional,
                       Barrier, ReportFault, Return> {
    using variant::variant;
};
struct KernelBody {
    std::vector<Buffer> buffers;
    Block statements;
    std::optional<size_t> faultSlot;
};
// Checks lexical bindings, types, memory permissions and block-uniform barriers.
// Memory bounds and producer/consumer races remain the physical planner's proof obligation.
void validateBody(const KernelBody &body);
std::string emitBody(const KernelBody &body);
IndexExpr
pointwiseAddress(const Program &program, const Node &node, size_t operand, const IndexExpr &linear);
struct ReductionAccess {
    size_t count;
    IndexExpr address;
};
ReductionAccess reductionAccess(
    const Program &program, const Node &node, const IndexExpr &row, const IndexExpr &reduction);
bool canonicalAxisReduction(const Program &program, const Node &node);
bool canonicalPointwise(const Program &program, const Node &node);
void validateMatmulContract(const Program &program, const Node &node);
KernelBody scalarBody(const Program &program, Launch &launch);
KernelBody loopBody(const Program &program, Launch &launch);
void validateLoopRegion(const Program &program, const Node &node);
KernelBody parallelMaxBody(const Program &program, Launch &launch);
KernelBody serialBody(const Program &program, Launch &launch);
KernelBody matmulBody(const Program &program, Launch &launch);
} // namespace camel::tiles::kernel
