/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The definition graphs of operators (docs/design/op-semantics, section 9; design principles
 * 7.1: "a definition graph is normative").
 *
 * An operator's module declares a definition (camel/core/operator_semantics.h, Definition;
 * OperatorTable::declareDefinition): a function written in Camel, exported by a library
 * module, that computes exactly what the operator computes, and how the operator's operands
 * bind the function's parameters. This file builds and expands them.
 *
 *   DefinitionStore          per context (Context::definitions()). definitionOf(oper) compiles
 *                            the library the first time one of its definitions is asked for (a
 *                            builder of its own, after the program is built:
 *                            UserDefinedModule::compileAsLibrary), encodes the function's graph,
 *                            checks it and keeps it: the graph and everything it calls are
 *                            pinned in the context's graph manager (GCGraphManager::pin) for as
 *                            long as the context; a rewrite that expands one encodes a copy into
 *                            the program. The checks, each refusing the definition with a reason:
 *                              - the function's signature: one ordinary parameter per
 *                                declared parameter, no with parameter, no capture; not generic;
 *                              - its effects are within the operator's: a pure operator has a
 *                                definition that neither has an external effect nor writes a
 *                                variable; an operator declared never to fault has one that
 *                                cannot fault;
 *                              - recursion: no definition reaches its own operator, through the
 *                                operators its graph uses and their definitions (a cycle refuses
 *                                every definition on it).
 *                            Versioning: the definition is part of the operator's semantics, so
 *                            declaring it bumps the key (a fact added); the built graph records
 *                            its portable content hash (portableGraphHash), the key a cache of
 *                            expansions or of kernels checked against it keys on.
 *   expandDefinition         rewrites one OPER node of a draft into the definition: a direct
 *                            call (FUNC) of the definition graph with the node's operands bound
 *                            to its parameters (a left-out optional operand gets its declared
 *                            value), then, unless asked not to, the call inlined. The nodes of
 *                            the definition itself get the expanded node's permissions in
 *                            addition to their own (design 6.4: the same operation in another
 *                            form); calls of other functions keep theirs. Used by nothing by
 *                            default: no pass and no back end expands early (design 9.3); a
 *                            back end may expand in its own private copy, a test pass does to
 *                            compare kernels with their definitions.
 */

#pragma once

#include "camel/core/operator_semantics.h"
#include "camel/core/operator_table.h"
#include "camel/runtime/graph.h"

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::core::context {
class Context;
} // namespace camel::core::context

namespace camel::core::module {
class UserDefinedModule;
} // namespace camel::core::module

namespace camel::runtime {

class GraphDraft;
class RuntimeGraphDraftSession;

/// A definition built: the graph of its function, frozen and kept by the context.
struct DefinitionGraph {
    camel::core::OperId oper = camel::core::kNoOper;
    /// The operator's key when it was built.
    camel::core::sem::OperatorKey key;
    camel::core::sem::Definition definition;
    /// The function's graph (what it calls is reachable from it), and the program that holds
    /// them.
    GCGraph *graph = nullptr;
    const Program *program = nullptr;
    /// portableGraphHash of the graph: what it computes, in a form that is the same in every
    /// process. A change of the definition changes it.
    uint64_t contentHash = 0;
};

class DefinitionStore {
  public:
    explicit DefinitionStore(camel::core::context::Context &context);
    ~DefinitionStore();
    DefinitionStore(const DefinitionStore &)            = delete;
    DefinitionStore &operator=(const DefinitionStore &) = delete;

    /// The definition of the operator `oper`, built the first time it is asked for; nullptr
    /// when the operator declares none or it cannot be built (the library does not compile,
    /// a check refuses it), and then `problem` (when given) says why. A refusal is remembered:
    /// asking again gives the same answer without compiling again.
    const DefinitionGraph *definitionOf(camel::core::OperId oper, std::string *problem = nullptr);

  private:
    struct Entry {
        /// The key of the declaration it was built for (nullopt: the operator had none).
        std::optional<camel::core::sem::OperatorKey> key;
        /// Immutable process-lifetime record: equal versions can still replace a declaration.
        const camel::core::sem::OperatorSemantics *declaration = nullptr;
        /// The graph and its facts, once built (also when a cycle refuses it since).
        std::unique_ptr<DefinitionGraph> built;
        /// Why it is refused; empty when it is accepted.
        std::string problem;
        /// The operators its graph uses (the edges of the recursion check).
        std::vector<camel::core::OperId> uses;
        /// The declaration epoch the recursion check last ran at.
        uint64_t checkedEpoch = 0;
    };

    /// Builds `oper`'s definition graph into entries_ with the checks of one definition (the
    /// signature, the effects); the recursion check is definitionOf's.
    Entry &build(camel::core::OperId oper);
    /// The operators from `oper` back to `oper` through the definitions they use, `oper`
    /// first and last; empty when there is no such cycle.
    std::vector<camel::core::OperId> cycleThrough(camel::core::OperId oper);
    /// The library module `name`, compiled; nullptr with `problem` set when it cannot be.
    std::shared_ptr<camel::core::module::UserDefinedModule>
    library(const std::string &name, std::string &problem);

    camel::core::context::Context &context_;
    std::recursive_mutex mutex_;
    std::unordered_map<camel::core::OperId, Entry> entries_;
    std::map<std::string, std::shared_ptr<camel::core::module::UserDefinedModule>> libraries_;
};

struct ExpandOptions {
    /// Inline the call of the definition graph; otherwise the node becomes the call.
    bool inlineBody = true;
};

struct ExpandResult {
    /// The node now holding the operator's value: the inlined result, or the FUNC node;
    /// kInvalidNodeRef when nothing was expanded.
    gc_node_ref_t value = kInvalidNodeRef;
    /// Why nothing was expanded.
    std::string problem;

    explicit operator bool() const { return value != kInvalidNodeRef; }
};

/// Expands the OPER node `node` of `draft` (a draft of `session`) into its operator's
/// definition (see the file comment). Nothing changes when the operator has no definition, it
/// cannot be built, or a required operand is missing.
ExpandResult expandDefinition(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t node,
    ExpandOptions options = {});

} // namespace camel::runtime
