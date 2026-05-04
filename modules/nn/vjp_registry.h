/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 04, 2026
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Reverse-mode VJP registry for nn macro graph synthesis.
 *
 * The registry maps primitive operator URIs or function graph names to their
 * vector-Jacobian-product rule. Builtin rules emit backward graph fragments
 * directly. Decorator-registered user rules are recorded as graph metadata and
 * will be consumed by the generic function-call autograd path as that path
 * expands beyond this demo.
 */

#pragma once

#include "camel/runtime/graph.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class Function;

namespace camel::core::type {
class Type;
}

namespace camel::runtime {
class GraphDraft;
}

namespace camel::nn {

struct ParameterGradient {
    camel::runtime::gc_node_ref_t parameter = camel::runtime::kInvalidNodeRef;
    camel::runtime::gc_node_ref_t gradient  = camel::runtime::kInvalidNodeRef;
};

struct VjpPrimitiveCall {
    std::string_view key;
    std::span<const camel::runtime::gc_node_ref_t> inputs;
    camel::runtime::gc_node_ref_t output = camel::runtime::kInvalidNodeRef;
};

class VjpBuildContext {
  public:
    explicit VjpBuildContext(camel::runtime::GraphDraft &draft);

    camel::runtime::GraphDraft &draft() { return draft_; }
    camel::core::type::Type *nodeType(camel::runtime::gc_node_ref_t node) const;

    camel::runtime::gc_node_ref_t addStaticFloat(double value);
    camel::runtime::gc_node_ref_t addOper(
        camel::core::type::Type *type, std::string_view uri,
        std::span<const camel::runtime::gc_node_ref_t> normInputs);

    void seedGradient(camel::runtime::gc_node_ref_t primal, camel::runtime::gc_node_ref_t gradient);
    void accumulateGradient(
        camel::runtime::gc_node_ref_t primal, camel::runtime::gc_node_ref_t gradient);
    std::optional<camel::runtime::gc_node_ref_t>
    gradientOf(camel::runtime::gc_node_ref_t primal) const;

    void accumulateParameterGradient(
        camel::runtime::gc_node_ref_t parameter, camel::runtime::gc_node_ref_t gradient);
    std::vector<ParameterGradient> parameterGradients() const;

  private:
    camel::runtime::GraphDraft &draft_;
    std::unordered_map<camel::runtime::gc_node_ref_t, camel::runtime::gc_node_ref_t> gradients_;
    std::unordered_map<camel::runtime::gc_node_ref_t, camel::runtime::gc_node_ref_t>
        parameterGradients_;
};

using BuiltinVjpRule = void (*)(VjpBuildContext &, const VjpPrimitiveCall &);

enum class VjpRuleKind {
    Builtin,
    Function,
};

struct VjpRule {
    VjpRuleKind kind                       = VjpRuleKind::Builtin;
    BuiltinVjpRule builtin                 = nullptr;
    camel::runtime::GCGraph *functionGraph = nullptr;
    std::string label;
};

class VjpRegistry {
  public:
    static VjpRegistry &instance();

    void registerBuiltin(std::string key, BuiltinVjpRule rule, std::string label = {});
    void registerFunction(std::string key, camel::runtime::GCGraph *functionGraph);
    const VjpRule *lookup(std::string_view key) const;
    bool has(std::string_view key) const { return lookup(key) != nullptr; }

  private:
    std::unordered_map<std::string, VjpRule> rules_;
};

void ensureBuiltinVjpRulesRegistered();
void applyVjpRule(
    VjpBuildContext &ctx, std::string_view key,
    std::span<const camel::runtime::gc_node_ref_t> inputs, camel::runtime::gc_node_ref_t output);
void applyVjpRule(
    VjpBuildContext &ctx, std::string_view key,
    std::initializer_list<camel::runtime::gc_node_ref_t> inputs,
    camel::runtime::gc_node_ref_t output);
void registerFunctionVjp(::Function *target, ::Function *vjp);

} // namespace camel::nn
