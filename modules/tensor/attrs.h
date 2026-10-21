/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license:
 * you may not use this software except in compliance with the License.
 * You may obtain a copy of the License at:
 * [https://opensource.org/license/mit]
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions or
 * limitations under the License.
 *
 * Author: Camel Contributors
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The node attributes of the tensor module (design principles 9.6, 7.3).
 *
 * Fusion and the choice of a library implementation are implementation choices: they are recorded
 * on the nodes of the graph, and the graph keeps the program's operators (`matmul`, `add`,
 * `relu`, ...). Two keys say how a node runs:
 *
 *   tensor.kernel   "library" | "canonical": the matmul runs on the library GEMM or on the
 *                   canonical algorithm. Absent means canonical; tensor::fuse writes the value
 *                   it chose, so a dump says how the node runs instead of saying nothing.
 *   tensor.fusion   "linear" | "linear_relu": this node is one of the nodes a backend may run as
 *                   one group (matmul + add, optionally relu). Every node of the group carries
 *                   the same value; the group's shape is the graph's, so a backend that runs the
 *                   group finds it from the attribute and the node's users.
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