// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The node attributes of the tensor module (design principles 9.6, 7.3).
 *
 * Fusion and the choice of a library implementation are implementation choices: they are recorded
 * on the nodes of the graph, and the graph keeps the program's operators (`matmul`, `add`,
 * `relu`, ...). Two keys say how a node runs:
 *
 *   tensor.kernel   "library" | "canonical": the matmul runs on the library GEMM or on the
 *                   canonical algorithm. Absent means canonical; tensor::fuse writes the value
 *                   it chose, so a dump says how the node runs instead of saying nothing. On
 *                   tensor:linear the key is the choice its VJP carries to matmul_grad_lhs/rhs;
 *                   the forward linear kernel stays canonical.
 *   tensor.fusion   "linear" | "linear_relu" | "vjp_div_neg": this node is one of the nodes a
 *                   backend may run as one group (matmul + add, optionally relu; or the divide,
 *                   multiply and neg a division's VJP emits). Every node of the group carries the
 *                   same value; the group's shape is the graph's, so a backend that runs the
 *                   group finds it from the attribute and the node's users.
 *
 * A group grants nothing numeric: the backend that runs one must produce the bits its operators
 * produce under the permissions in force (7.3, 7.4), or run the nodes one by one. The
 * "vjp_div_neg" kernel does exactly the operators' roundings, so it is licensed under `#strict`
 * and the safe mode as well; a backend that cannot keep that promise declines the group.
 *
 * The core registers neither key and reads neither: the module registers them, the pass writes
 * them, and the executor hands the attribute set of the node it calls to the kernel
 * (ArgsView::attrs).
 */

#pragma once

#include "camel/core/node_attrs.h"

#include <string_view>

namespace camel::tensor {

/// The names of the keys, as they appear in a dump and in a cache: the identity outside the
/// process (camel/core/node_attrs.h).
inline constexpr std::string_view kKernelAttr = "tensor.kernel";
inline constexpr std::string_view kFusionAttr = "tensor.fusion";

inline constexpr std::string_view kLibraryKernel   = "library";
inline constexpr std::string_view kCanonicalKernel = "canonical";
inline constexpr std::string_view kLinearFusion    = "linear";
inline constexpr std::string_view kLinearReluFusion = "linear_relu";
/// The group a division's VJP emits: `divide(dy, rhs)`, `multiply(that, y)`, `neg(that)`.
inline constexpr std::string_view kDivideMulNegFusion = "vjp_div_neg";

/// Registers both keys with their rules (Symbol values; two nodes keep an attribute only when
/// their values agree, which is the default rule and the one a group needs). Called once, when
/// the module loads.
void registerTensorAttrKeys();

/// The key ids; interned on first use, so they are available even if registration did not run.
core::attr::AttrKey kernelAttrKey();
core::attr::AttrKey fusionAttrKey();

/// Whether `attrs` tells matmul to run on the library GEMM.
bool usesLibraryGemm(core::attr::AttrSetId attrs);

} // namespace camel::tensor