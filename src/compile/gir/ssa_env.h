// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "camel/common/scope.h"
#include "camel/compile/gct.h"
#include "camel/compile/gir/types.h"
#include "camel/core/source/ids.h"

namespace camel::compile::gir {

using node_scope_t     = Scope<std::string, node_handle_t>;
using node_scope_ptr_t = std::shared_ptr<node_scope_t>;

/// The SSA environment of one build: which `var`s are built as values, and which declaration each
/// name means where.
///
/// **The unit of SSA information is a declaration, not a name.** A region declares a name at most
/// once (`Builder::insertNode` refuses the second), so "the current value of `x`" is really "the
/// current value of the declaration this region's `x` denotes" -- and two declarations that happen
/// to share a name (`var x` in a block, another `var x` in a branch arm inside it) are two facts,
/// not one. The environment is therefore keyed by the declaration: `decl_key_t` is one number per
/// declaration, minted here, and a name only says *which* declaration a use site means
/// (`find` resolves it through the region chain, which is the language's own name binding).
///
/// Every fact SSA adds to a build is here, and nothing else owns it:
///
///   * which declaration each region's names mean, and what a write of one must enter (`Entry`);
///   * the loop head that carries a declaration as a value: the body's port (`Entry::port`);
///   * the value each declaration had where a branch was entered, taken as the first arm writes it
///     (`before_`, one per declaration, the outermost branch's value);
///   * what each arm of a branch wrote (`ArmWrites`, one record per arm, which is a region).
///
/// No value is stored a second time. The current value of a declaration is the value its region
/// binds its name to -- `nodeScope_`'s map, the binding every read of the name already goes through,
/// SSA or not -- and `valueOf` is how a reader that has only the declaration asks for it. `rebind`
/// is the one place a write moves it, and that write is the whole of the change: there is no second
/// table to keep in step, and so no way for the two readings of one name to part.
class SsaEnv {
  public:
    using Type       = camel::core::type::Type;
    using decl_key_t = uint32_t;

    /// One declaration: who it is, and what a write of it must enter. It holds no value: what a
    /// declaration stands for now is the region's own binding of its name (`valueOf`), which is the
    /// binding every read and every write of that region already goes through.
    struct Entry {
        /// The type the cell would have held, so a later write enters it as a store into that cell
        /// did. A loop's port carries the state's type instead (the type it started from).
        Type *type = nullptr;
        /// The port of a loop body's region, when a loop head carries this declaration as a value:
        /// a write inside the body is the round's next state, recorded against it. Set
        /// on the body's own declaration of the name, so a write inside the body finds the
        /// innermost head and a write after the loop finds the enclosing one. This is which head
        /// carries the declaration, not what it holds: it stays put while the value moves.
        node_handle_t port = nullptr;
        /// The name the region declares for it, which is the name a JOIN hands on, and the key of
        /// the binding that holds its value.
        std::string name;
        /// The GCT node the declaration was written as. Null for a port, which is the region's own
        /// binding of a name and no declaration of the program.
        const GCT::Node *decl = nullptr;
        /// The region the declaration belongs to: one region's fact, and the one binding of the name
        /// whose value is this declaration's.
        node_scope_ptr_t region;
        decl_key_t key = 0;
    };

    /// The writes one arm of a branch made, as the arm ended: the declaration each wrote and the
    /// value it ended with, in the order they were first written. The arm owns this record -- an
    /// arm is a region, and this is what the region wrote -- and the JOIN is what reads it.
    class ArmWrites {
      public:
        void record(decl_key_t decl, node_handle_t value);
        /// The value this arm ended the declaration with, null when the arm did not write it.
        node_handle_t valueOf(decl_key_t decl) const;
        /// The declarations the arm wrote, in the order it first wrote each.
        const std::vector<decl_key_t> &order() const { return order_; }

      private:
        std::unordered_map<decl_key_t, node_handle_t> values_;
        std::vector<decl_key_t> order_;
    };

    /// What a branch's JOIN carries: the declaration its arms wrote, the type every arm hands it
    /// in, and the value each arm hands it -- the value the declaration had before the branch for
    /// an arm that wrote nothing -- in the join's order.
    struct BranchCarry {
        std::string name;
        Type *type = nullptr;
        std::vector<node_handle_t> values;
    };

    /// How the graph builder asks what a value's type is. The environment holds values, not types
    /// of nodes, so the one question it cannot answer alone is asked of its owner.
    using type_of_t = std::function<Type *(node_handle_t)>;

    /// `var x = e` built as a value: `region` declares `x`, and the value it was built with is the
    /// value `region` already binds the name to. `decl` is the GCT node the declaration was written
    /// as (null when the region binds a name to a port of its own). Answers the declaration's key.
    decl_key_t declare(
        const node_scope_ptr_t &region, const std::string &name, const GCT::Node *decl, Type *type);
    /// A loop body's declaration of the name of a `var` the head carries as a value: the region
    /// binds the name to the port, and a write of it is the round's next state of that port.
    void declarePort(
        const node_scope_ptr_t &region, const std::string &name, node_handle_t port, Type *type);

    /// The declaration `name` denotes from `region` outwards; null when the name is no SSA value
    /// here.
    const Entry *find(const node_scope_ptr_t &from, const std::string &name) const;
    /// The same, writable: the entry a write replaces.
    Entry *mutableFind(const node_scope_ptr_t &from, const std::string &name);
    /// The value a declaration stands for now: the binding of its name in the region that declared
    /// it. One place, and the one every read of the name already takes.
    node_handle_t valueOf(const Entry *entry) const;
    /// The port a loop head carries `name` with, from `region` outwards; null when no head does.
    node_handle_t portOf(const node_scope_ptr_t &from, const std::string &name) const;
    /// The ports of the loop heads around `region`: what a loop written inside another loop's body
    /// has to hand its record back to when it is done.
    std::vector<node_handle_t> portsInScope(const node_scope_ptr_t &from) const;

    /// A write of `name`: the declaration's value becomes `value`, and `arm` -- the arm whose build
    /// is innermost, if there is one -- remembers that it wrote it. The value is moved
    /// in one place, the region's own binding of the name, which is what every read of it takes.
    void rebind(
        const node_scope_ptr_t &from, const std::string &name, node_handle_t value,
        ArmWrites *arm = nullptr);

    /// A branch begins: every write from here on records the value its declaration had before the
    /// branch, once (a branch written inside an arm is inside the same gathering: the outermost
    /// one's values are the ones the joins hand an arm that wrote nothing).
    void beginBranch();
    void endBranch();
    /// What the branch whose arms these are carries: the one declaration they wrote, at the type
    /// they all hand it in. Null when nothing is carried -- no arm wrote, more than one declaration
    /// was written, a declaration was written whose value before the branch is unknown, or the
    /// declaration is no longer the one these names denote (an arm's own declaration dies with it).
    std::optional<BranchCarry> branchCarry(
        const std::vector<const ArmWrites *> &arms, const node_scope_ptr_t &from,
        const type_of_t &typeOf) const;

    /// A write to a `var` built as a value stores nothing, so the value nobody reads is a value
    /// node with no user: the dropped-value report reads this, and it is the store's own report
    /// (the assignment as written) said of the value instead of the store.
    void noteWrite(node_handle_t value, camel::source::origin_id_t origin);
    const std::unordered_map<node_handle_t, camel::source::origin_id_t> &writes() const {
        return writes_;
    }

  private:
    /// One region's part of the environment: the declarations it made, by the name it made each
    /// with. A name is one declaration here, so the name only selects one.
    struct Region {
        std::unordered_map<std::string, decl_key_t> bindings;
        std::unordered_map<decl_key_t, Entry> entries;
    };

    /// The entry of `key`, or null when no declaration carries it.
    const Entry *entryOf(decl_key_t key) const;
    Entry *entryOf(decl_key_t key);
    /// The entry of `key`, when the live names still denote it (`find(from, entry->name)` is it).
    const Entry *liveEntryOf(const node_scope_ptr_t &from, decl_key_t key) const;

    /// The regions, by their scope object. A region's table outlives the build of its body: a
    /// template's or a decorated graph's scope object is entered again for every instance of it, so
    /// the declarations it made are what the next build of the same body starts from -- the same
    /// scope the reads are resolved in, and the same lifetime.
    std::map<node_scope_ptr_t, Region> regions_;
    /// Every declaration ever made in this build, by its key. The entries live in `regions_`; this
    /// is how a declaration is found without knowing which name or region it came from (the JOIN
    /// asks that of the declaration it carries).
    std::unordered_map<decl_key_t, Entry *> decls_;
    /// The value each declaration had where the outermost open branch was entered, taken as its
    /// first arm writes it: the write has already replaced the binding by the time the join is
    /// built, so the value it has to hand the arm that wrote nothing can only be taken here.
    std::unordered_map<decl_key_t, node_handle_t> before_;
    decl_key_t nextKey_ = 1;
    int branchDepth_    = 0;
    std::unordered_map<node_handle_t, camel::source::origin_id_t> writes_;
};

} // namespace camel::compile::gir