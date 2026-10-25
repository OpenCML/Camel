// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The type table: structural interning of type objects, and their 32-bit TypeId.
 *
 * Part of the graph data model (docs/design/graph-model, sections 3.2, 3.8 and 4.3; migration
 * step 2). Before it, every construction of a composite type allocated a new object in the
 * permanent space, which never frees: each graph got its own frame-layout tuples, every
 * resolved operator call its own signature, and every re-encoding of a graph by a rewrite a
 * fresh copy of all of them.
 *
 * A type has no identity, only structure: the same type is one id everywhere. So a frozen node
 * names its type by a TypeId that came from this table, and nothing outside it hands out ids.
 *
 * What the table guarantees. A constructor that interns asks the table with a key made of the
 * shape (which constructor) and the words that determine the type: the **TypeIds** of its
 * component types and whatever else distinguishes it (field names, `var` marks, modifiers,
 * extents, packs). Two requests with equal keys get the same object. Because the key holds
 * component *ids*, this is sound and complete exactly when the components are themselves
 * interned, which the constructors arrange bottom-up: a component is interned before the
 * composite that holds it (see typeKeyWord for the component the table does not hold).
 *
 * What is interned:
 *  - a bare type of one code (Type::create: the primitive singletons, the `typeof` hole);
 *  - tuple types (TupleType::create), frame and static-area layouts among them, and the cell
 *    and the tagged forms (`var T`, `T?`, `T!`);
 *  - array types (ArrayType::make) and stream types (`T..`);
 *  - struct types (StructType): an anonymous struct by its field names and field types, a
 *    named one by its declaration and the canonical ids of its type arguments;
 *  - function types (FunctionType::intern, FunctionTypeFactory::build), parameter names
 *    included: the type an operator's type rule gives a call, a function type a program
 *    writes, the signature of a branch arm, and every signature a rewrite re-infers;
 *  - the compile-time types: a type variable (by its name and its binding mark, see below), a
 *    type function, a `typeof` placeholder, an enum instance, a `Map`/`Set` instance.
 *
 * What is not:
 *  - The open signature of a function the front end is building (OpenFunctionType, see
 *    camel/core/type/composite/func.h): its result type is decided by its body after values
 *    of the function exist, and it is mutable until then, so the table never holds one. A
 *    frozen graph names the closed signature the builder reaches (FunctionType::intern), and
 *    the assertion in `typeIdOf` is what says so.
 *
 * Names and the canonical id. A key may hold names: a dimension name and a pack name in an
 * array's extents, a parameter name in a function type (plan 3.2 keeps them in the key because
 * a type prints them in diagnostics and graph dumps). `equals` ignores them (text.cpp, sameType):
 * `f32[n]` and `f32[?]` are one type, `(a: int) => int` and `(int) => int` are one type. So the
 * table also interns the same structure with those names erased and records that object's id on
 * the named one as its **canonical id** (`Type::canonicalId`). Equal canonical ids mean equal
 * structure, and the canonical id is therefore the structural hash the graph and the optimiser
 * need: the graph's invariants, nodeContentHash, contentHash, sameContent and the e-graph's
 * expression classes compare canonical ids, while printing and diagnostics keep the object that
 * carries the names.
 *
 * An interned object is immutable from the moment the table returns it. Code must never
 * modify a type it did not just allocate itself; the only mutators a type class has are the
 * result type and the resources of an open signature, which the table never holds, and the
 * fields of a named struct being read, which the table holds under a key that does not depend
 * on them (its declaration).
 *
 * TypeId. Every interned type has a dense non-zero id; `Type::typeId()` is 0 for a type that is
 * not in the table. `adopt`, which handed an id to an object it was given, is gone: that is
 * exactly the door object identity came in through. Ids are process-local and never reused.
 *
 * Ownership and lifetime: the table owns nothing but its index. Type objects live in the
 * permanent space and are never freed, so entries are never removed.
 *
 * Thread-safety: intern() takes the table's mutex (type construction happens while compiling
 * and rewriting, and occasionally from operator type rules on scheduler threads). get(),
 * holds(), canonical() and Type::typeId() take no lock. The mutex is recursive because a
 * constructor may need another type of the table while it builds one (`Map<K, V>` needs its item
 * tuple and its two array types): the re-entrant call takes the lock again, and no iterator of a
 * lookup is held across a constructor call.
 */

#pragma once

#include "camel/core/type/base.h"
#include "camel/core/type/extent.h"
#include "camel/utils/append_only_table.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace camel::core::type {

/// The word a key uses for a component type: its TypeId when the table holds it (bit 63 set),
/// its address otherwise. Two interned components that are the same type are one word, which is
/// what makes the key structural; a component the table does not hold is still told apart by its
/// address, which is as much as can be said about it (see the file comment on completeness).
inline uintptr_t typeKeyWord(const Type *type) {
    if (type == nullptr) {
        return 0; // a field a declaration has not typed yet
    }
    return type->isInterned() ? (uintptr_t{1} << 63) | static_cast<uintptr_t>(type->typeId())
                              : reinterpret_cast<uintptr_t>(type);
}

class TypeTable {
  public:
    /// Which constructor a key belongs to. Part of the key: different constructors can have
    /// the same components.
    enum class Shape : uint32_t {
        Primitive,      // Type::create: a bare type of one code
        Tuple,          // TupleType::create (layouts included)
        Cell,           // TupleType::cell (a `var` cell)
        Nullable,       // TupleType::nullable (`T?`)
        Fallible,       // TupleType::fallible (`T!`)
        Array,          // ArrayType::make
        Stream,         // StreamType::create
        Struct,         // StructType::fromFactoryData (anonymous: its fields are the type)
        DeclaredStruct, // StructType::declare/get (named: its declaration is the type)
        Function,       // FunctionType::intern
        TypeVar,        // TypeVarType::create
        TypeFunc,       // TypeFunctionType::create
        TypeOf,         // TypeOfType::create
        Enum,           // EnumType::get
        Collection,     // MapType::get, SetType::get and their prototypes
        Other,          // another module type, one object per kind
    };

    /// The key of the same type with the naming words erased (see the file comment), and the
    /// constructor of that object. An empty `words` says the type is its own canonical form.
    struct Canonical {
        std::span<const uintptr_t> words;
        std::function<Type *()> make;
    };

    static TypeTable &instance();

    /// The one type registered under (`shape`, `words`), or the object `make()` returns, which is
    /// then registered under that key and given a TypeId. When `canonical` names a different key,
    /// the type's name-erased twin is interned too (through `canonical.make`) and its id becomes
    /// this type's canonical id. `make` and `canonical.make` run under the table's lock and must
    /// not call back into the table.
    Type *intern(
        Shape shape, std::span<const uintptr_t> words, const std::function<Type *()> &make,
        const Canonical &canonical = {});

    /// The type with this id. Lock-free. `id` must come from Type::typeId() and be non-zero.
    Type *get(TypeId id) const { return byId_[id]; }

    /// The type registered under this key, or nullptr. For a constructor that must answer a use
    /// of a declaration before it can build the instance (StructType::get).
    Type *find(Shape shape, std::span<const uintptr_t> words);

    /**
     * Interns a module type: a type whose structure its own class states rather than the core's
     * constructors (a `graph::Handle`, a json `Json`, a tiles `Device`, the python `PyObject`).
     * `parts` are the ids of what that class's `equals` compares -- its type arguments, a handle
     * kind -- and a module type that compares nothing but its own code passes none. One object per
     * code and parts, so a module type a graph records has an id like any other.
     */
    Type *internOther(
        TypeCode code, std::span<const uintptr_t> parts, const std::function<Type *()> &make);

    /// Whether the table handed out `id`. Every entry is on the interning path (`adopt` is gone),
    /// so this is the whole check that a type id a graph names came from the table.
    bool holds(TypeId id) const { return id != kNoTypeId && id < byId_.size(); }

    /// The canonical id of `type`: the id of the same structure with the naming words erased (see
    /// the file comment), or kNoTypeId when the table does not hold it. `equals` agrees with it.
    TypeId canonicalId(const Type *type) const {
        return type == nullptr ? kNoTypeId : type->canonicalId_;
    }

    /// The object `canonicalId(type)` names, which is `type` itself unless its key names
    /// something. A type the table does not hold is its own canonical form. Lock-free.
    Type *canonical(const Type *type) const {
        if (type == nullptr) {
            return nullptr;
        }
        const TypeId id = type->canonicalId_;
        return holds(id) ? byId_[id] : const_cast<Type *>(type);
    }

    /// The canonical id of the type `id` names: what the graph's invariants, its content hashes
    /// and the e-graph's expression classes compare. kNoTypeId for an id the table does not hold.
    TypeId canonicalIdOf(TypeId id) const {
        return holds(id) ? byId_[id]->canonicalId_ : kNoTypeId;
    }

    /**
     * The canonical extent list of a shape: one entry per distinct shape text
     * (`ExtentForm::toString`), which is normalized by construction (terms sorted and merged,
     * zero coefficients dropped). The same shape written two ways therefore has one address,
     * and an array type's intern key is four words instead of a variable-length shape. Empty
     * input has no entry: it means "no dimensions", not a one-dimension shape, and maps to
     * nullptr. The returned vector lives in the table and is immutable.
     */
    const std::vector<ExtentForm> *internExtents(std::span<const ExtentForm> extents);

    /// An interned text (a dimension pack name, a type variable's name), by value. Empty maps to
    /// nullptr.
    const std::string *internText(std::string_view text);

    /// Number of ids handed out, the reserved id 0 included.
    uint32_t size() const { return byId_.size(); }

    /// How many intern() calls were answered with an existing object. For statistics.
    size_t hits() const;

  private:
    TypeTable();

    struct Key {
        Shape shape;
        std::vector<uintptr_t> words;
    };
    struct KeyView {
        Shape shape;
        std::span<const uintptr_t> words;
    };
    struct KeyHash {
        using is_transparent = void;
        size_t operator()(const KeyView &key) const;
        size_t operator()(const Key &key) const { return (*this)(KeyView{key.shape, key.words}); }
    };
    struct KeyEqual {
        using is_transparent = void;
        bool operator()(const KeyView &a, const KeyView &b) const;
        bool operator()(const Key &a, const Key &b) const {
            return (*this)(KeyView{a.shape, a.words}, KeyView{b.shape, b.words});
        }
        bool operator()(const Key &a, const KeyView &b) const {
            return (*this)(KeyView{a.shape, a.words}, b);
        }
        bool operator()(const KeyView &a, const Key &b) const {
            return (*this)(a, KeyView{b.shape, b.words});
        }
    };

    /// Appends `type` to byId_ and stores the id in the object. Caller holds mutex_. The canonical
    /// id starts as the type's own; interning a name-erased twin replaces it.
    void assignId(Type *type);
    /// The entry under this key, building it with `make` when the table does not hold one.
    Type *findOrInsert(Shape shape, std::span<const uintptr_t> words, const std::function<Type *()> &make);

    mutable std::recursive_mutex mutex_;
    std::unordered_map<Key, Type *, KeyHash, KeyEqual> byKey_;
    camel::utils::AppendOnlyTable<Type *> byId_; // index 0 is reserved (kNoTypeId)
    std::unordered_map<std::string, std::vector<ExtentForm>> extents_;
    std::unordered_map<std::string, std::string> texts_;
    size_t hits_ = 0;
};

} // namespace camel::core::type