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
 * Created: Oct. 13, 2026
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The operator table: one entry per operator URI, referred to by a 32-bit OperId.
 *
 * Part of the graph data model (docs/design/graph-model, sections 3.2, 3.5 and 3.8). An OPER
 * node holds the OperId of the operator it calls instead of the URI text, and everything a
 * pass may ask about an operator without running it is a field of its entry:
 *
 *   uri            ":op/add_l", "tensor:matmul"
 *   traits         what the operator's module declared about it (OperatorTraits)
 *   resolver       its type rule, re-run by passes that change what is known about arguments
 *   type folder    the value its result has whenever the argument types fix it
 *
 * Generic graph passes (constant folding, CSE, DCE) must not know about any particular
 * module, yet they need to know whether an operator may be evaluated early, merged or
 * dropped. Modules and the builtin operator set declare those properties here when they are
 * loaded; passes only query. An operator nobody declared is treated conservatively: it has
 * side effects and may fault (design principles 2.9).
 *
 * The implementation (the C++ function pointer) is deliberately not in the table: it is
 * resolved per Context by the executor manager.
 *
 * Ownership and lifetime: entries are created on first mention of a URI (by a declaration, a
 * resolver registration, or a graph being built) and are never removed or renumbered, so an
 * OperId is valid until the process exits. Ids are process-local: anything that leaves the
 * process (a module cache) must store URIs and re-intern them when it is loaded.
 *
 * Thread-safety: every member function may be called from any thread. uri() and the trait
 * queries by OperId take no lock (the parallel scheduler asks them while it runs graphs);
 * traits are one atomic byte per entry, so a declaration racing a query yields either the old
 * or the new traits, never a mixture. The lookups by URI text and the resolver/folder fields
 * are guarded by a reader/writer lock; they are compile-time paths.
 */

#pragma once

#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/symbol.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/append_only_table.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace camel::core {

/// Index of an operator in the OperatorTable.
using OperId = uint32_t;

/// "No operator": the value of a zero-initialized OPER payload. Its URI is the empty string and
/// it is never declared, so every query treats it conservatively.
inline constexpr OperId kNoOper = 0;

struct OperatorTraits {
    /// No side effects and no hidden state: the result depends only on the arguments, so a call
    /// may be evaluated at compile time, merged with an identical call, or removed if unused.
    bool pure = false;
    /// Output element i depends only on input elements i (after broadcasting).
    bool elementwise = false;
    /// Not pure, but without an effect outside the program's own variables: it creates or
    /// reads a variable (`:var/new`, `:var/get`). Its result depends on when it runs, so it
    /// is never folded, merged or moved across a write; it is not an effect a user can
    /// observe, so it is not reported as one (effect trace).
    bool stateOnly = false;
    /// The operator can raise a fault (design principles 2.9: division by zero, an index out
    /// of range, a conversion whose value does not fit). True unless the module declares
    /// otherwise. Recorded for consumers of the graph model; no pass in this repository
    /// changes its behaviour on it yet (docs/design/graph-model, question Q1).
    bool mayFault = true;
    /// While it runs, the operator calls function values it received as arguments (the
    /// higher-order operators and `attempt`), so what it does depends on those functions.
    bool callsGraphs = false;
};

/**
 * The value an operator's result has whenever its arguments have the given types, for operators
 * whose result a type can fix: the shape of a tensor whose type carries its shape, its element
 * count, one of its dimensions. `statics` holds the arguments that are constants (aligned with
 * `types`); `allocator` holds any object the value needs. Returns nullopt when the types do not
 * fix the result. std::opt::fold uses these to turn such results into constants once shape
 * specialization has put the facts into the types.
 */
using TypeFolder = std::function<std::optional<slot_t>(
    std::span<type::Type *const> types, type::static_args_t statics, mm::IAllocator &allocator)>;

class OperatorTable {
  public:
    static OperatorTable &instance();

    // ---- Identity -------------------------------------------------------------------------

    /// The id of the operator `uri`, creating an undeclared entry when the URI is new.
    OperId intern(std::string_view uri);

    /// The id of `uri` when the table has an entry for it, nullopt otherwise. Adds nothing.
    std::optional<OperId> lookup(std::string_view uri) const;

    /// The URI of `id`. The view is never invalidated. Lock-free.
    std::string_view uri(OperId id) const { return symbolText(entries_[id].uri); }

    /// Number of entries, kNoOper included.
    uint32_t size() const { return entries_.size(); }

    // ---- Declarations (module load time) --------------------------------------------------

    /// Declares (or replaces) the traits of `uri`, e.g. "tensor:matmul" or ":op/add_i".
    void set(std::string_view uri, OperatorTraits traits);

    /// Declares the purity of the operators `protocol:name`, one `{name, pure}` each. Every
    /// operator of a module has to be declared, the ones with side effects too (design
    /// principles 2.9): an operator nobody declared is treated as having side effects, and
    /// its module is told so when it is loaded. `pure` means the result depends on the
    /// arguments only: not on a clock, a random generator, the environment or a file.
    void declare(
        std::string_view protocol,
        std::initializer_list<std::pair<std::string_view, bool>> operators);

    // ---- Queries --------------------------------------------------------------------------

    /// Declared traits of the operator, or nullopt when nobody declared it.
    std::optional<OperatorTraits> find(OperId id) const;
    std::optional<OperatorTraits> find(std::string_view uri) const;

    /// True when the operator is declared pure.
    bool isPure(OperId id) const;
    bool isPure(std::string_view uri) const;

    /// True when running the operator is an effect visible outside the program's variables:
    /// it is neither pure nor state-only. True for an undeclared operator.
    bool hasExternalEffect(OperId id) const;
    bool hasExternalEffect(std::string_view uri) const;

    // ---- Type rules -----------------------------------------------------------------------

    /// Registers (or replaces) the type resolver of an operator overload. An OPER node records
    /// only the operator its call resolved to; passes that change what is known about the
    /// arguments (binding input shapes, folding a shape to a constant) re-run the overload's
    /// resolver through the table. Operator groups register their overloads when created.
    void setResolver(std::string_view uri, type::resolver_ptr_t resolver);

    /// The resolver of the operator, or nullptr.
    type::resolver_ptr_t resolver(OperId id) const;
    type::resolver_ptr_t resolver(std::string_view uri) const;

    /// Registers (or replaces) the type folder of an operator.
    void setTypeFolder(std::string_view uri, TypeFolder folder);

    /// The type folder of the operator, or nullptr. The pointer stays valid until the folder
    /// of the same operator is replaced, which only happens while modules load.
    const TypeFolder *typeFolder(OperId id) const;
    const TypeFolder *typeFolder(std::string_view uri) const;

  private:
    OperatorTable();

    // Trait bits of an entry. kDeclared distinguishes "declared effectful" from "nobody
    // declared it". kNoFault is stored inverted so that an all-zero byte is the conservative
    // reading of every trait.
    enum : uint8_t {
        kDeclared    = 1 << 0,
        kPure        = 1 << 1,
        kElementwise = 1 << 2,
        kStateOnly   = 1 << 3,
        kNoFault     = 1 << 4,
        kCallsGraphs = 1 << 5,
    };

    struct Entry {
        SymId uri = kEmptySym;
        std::atomic<uint8_t> traits{0};
        // Guarded by mutex_. A folder is heap-allocated once and replaced only at load time, so
        // readers may keep the pointer after releasing the lock.
        type::resolver_ptr_t resolver;
        std::unique_ptr<TypeFolder> folder;
    };

    static uint8_t encode(OperatorTraits traits);
    static OperatorTraits decode(uint8_t bits);

    /// intern() for callers that already hold mutex_ exclusively.
    OperId internLocked(std::string_view uri);
    uint8_t traitBits(OperId id) const {
        return entries_[id].traits.load(std::memory_order_relaxed);
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<SymId, OperId> ids_; // URI symbol -> entry index
    camel::utils::AppendOnlyTable<Entry> entries_;
};

} // namespace camel::core
