// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

#include "camel/core/error/diagnostics/range.h"
#include "camel/core/operator_semantics.h"
#include "camel/core/operator_table.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_txn.h"

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::core::type {
class FunctionType;
} // namespace camel::core::type

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

    /// A function of a library, encoded into a program of its own (kept for as long as the
    /// context): for a native caller that runs a function written in Camel. `graph` is the root
    /// graph of that program, whose entry is the function: the module-level values of the
    /// library are computed before the function runs (design principles 10.1). Its signature is
    /// the function's.
    struct LibraryFunction {
        const Program *program = nullptr;
        GCGraph *graph         = nullptr;
    };
    /// What a lookup found besides the function: whether the library is there, whether the name
    /// is absent from it, and the module itself so that a caller can say what it does provide
    /// (: a pass named `module::name` whose module is there and provides no such pass).
    struct LibraryLookup {
        /// The library is there as a module written in Camel; it may still fail to load or
        /// compile (that reason is in `problem`, not here).
        bool moduleFound = false;
        /// The lookup stopped at a name that is not there: the library itself is not there (no
        /// file for it, or it is not written in Camel), or the library is there and has no such
        /// function. A library that is there and *refused* (it did not load or compile) is not
        /// absent in this sense: that reason is in `problem`.
        bool absent = false;
        /// `absent` because the library has no such function although the module declares the
        /// name: it declares it without exporting it, which is a different mistake.
        bool declaresUnexported = false;
        /// The module, when it could be imported: its export list says what it provides.
        std::shared_ptr<camel::core::module::UserDefinedModule> module;
    };
    /// The function `function` exported by the library module `module`, compiled and encoded
    /// the first time it is asked for. It must be one non-generic function. Returns nothing
    /// when it cannot be had, and then `problem` says why; the answer is remembered. `lookup`,
    /// when given, receives what the answer says about the module itself: the library not being
    /// there, the name being absent from it, and the module, so that the caller can list what it
    /// does export.
    /// `errorLocation`, when given, receives the library's first error as a secondary location:
    /// its file, range and text (the same sentence the diagnostic table gives it). It is empty
    /// when the library could not be loaded, or when its first error has no position; the
    /// reason then stays in `problem`.
    /// `expected`, when given, makes the function a generic one: the program's entry is its
    /// instance that is a function of that type (a pass `(f: F) => F!` over the function `F`,
    /// design principles 9.1); one program per type is kept.
    std::optional<LibraryFunction> libraryFunction(
        const std::string &module, const std::string &function, std::string &problem,
        std::optional<camel::core::error::DiagLabel> *errorLocation = nullptr,
        LibraryLookup *lookup = nullptr, camel::core::type::FunctionType *expected = nullptr);

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
    /// The library module `name`, imported and compiled as a library; nullptr with `problem` set
    /// when it cannot be.
    std::shared_ptr<camel::core::module::UserDefinedModule>
    library(const std::string &name, std::string &problem);
    /// The library module `name`, imported only: no compile of its own. What a pass written in
    /// Camel needs before it is built as a program of its own (compileAsProgram). The answer is
    /// remembered: a module that could not be loaded is not loaded again.
    struct LibraryEntry {
        std::shared_ptr<camel::core::module::UserDefinedModule> module;
        /// Why the library is not available: its load failed, or it is not written in Camel.
        std::string problem;
        /// The library is not there (no file found for its name, or it is not written in
        /// Camel) rather than there and refused (its load or compile failed).
        bool absent = false;
    };
    const LibraryEntry &importLibrary(const std::string &name);

    camel::core::context::Context &context_;
    std::recursive_mutex mutex_;
    std::unordered_map<camel::core::OperId, Entry> entries_;
    std::map<std::string, LibraryEntry> libraries_;
    struct FunctionEntry {
        std::optional<LibraryFunction> found;
        std::string problem;
        bool absent = false;
        /// The library's first error, when it has one with a position (see libraryFunction).
        std::optional<camel::core::error::DiagLabel> errorLocation;
        /// What the lookup said about the module.
        LibraryLookup lookup;
    };
    std::map<std::string, FunctionEntry> functions_;
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

/// Expands the OPER node `node` of `transaction`'s graph (a graph of `session`) into its
/// operator's definition (see the file comment). Nothing changes when the operator has no
/// definition, it cannot be built, or a required operand is missing.
ExpandResult expandDefinition(
    RuntimeGraphDraftSession &session, GraphTransaction &transaction, gc_node_ref_t node,
    ExpandOptions options = {});

} // namespace camel::runtime
