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
 * What the table guarantees. A constructor that interns asks the table with a key made of the
 * shape (which constructor) and the words that determine the type: the addresses of its
 * component types and whatever else distinguishes it (field names, `var` marks, modifiers).
 * Two requests with equal keys get the same object. Because the key holds component
 * *addresses*, this is sound whether or not the components are themselves interned: equal
 * keys denote the same type however the components are compared. It is complete (equal types
 * are one object) exactly when all components are interned.
 *
 * What is interned:
 *  - tuple types (TupleType::create), frame and static-area layouts among them, and array
 *    types (ArrayType::create). Converting a value to its own type is legal and does nothing
 *    (design principles 5.1, question Q11 of the design), so `v as T` between two of them
 *    is decided by their structure, never by which object each is, and sharing equal types
 *    changes no program's meaning;
 *  - struct types (StructType);
 *  - function types (FunctionType::create, FunctionTypeFactory::build), parameter names
 *    included: the type an operator's type rule gives a call, a function type a program
 *    writes, the signature of a branch arm, and every signature a rewrite re-infers.
 * Variable cells, `T?`, `T!`, maps, sets and tensor types were already shared by rules of
 * their own; cells and tagged forms take a TypeId from the table.
 *
 * What is not (the fallback of the design's section 10.1):
 *  - The open signature of a function the front end is building (OpenFunctionType, see
 *    camel/core/type/composite/func.h): its result type is decided by its body after values
 *    of the function exist, and it is the function's identity while it is built. One per
 *    function the program defines; never in the table, so never shared.
 *  - Union types (mutable sets) and type variables.
 * A type that contains one of these is still interned by the address of that component.
 *
 * An interned object is immutable from the moment the table returns it. Code must never
 * modify a type it did not just allocate itself; the only mutator a type class has is the
 * result type of an open signature, which the table never holds.
 *
 * TypeId. Every interned type, and every primitive type singleton, has a dense non-zero id;
 * `Type::typeId()` is 0 for a type that is not in the table. For types in the table, equal
 * ids mean the same object. Ids are process-local and never reused. Nothing stores a TypeId
 * in a graph yet: the frozen-block layout (migration step 3) will.
 *
 * Ownership and lifetime: the table owns nothing but its index. Type objects live in the
 * permanent space and are never freed, so entries are never removed.
 *
 * Thread-safety: intern() and adopt() take the table's mutex (type construction happens while
 * compiling and rewriting, and occasionally from operator type rules on scheduler threads).
 * get() and Type::typeId() take no lock.
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

class TypeTable {
  public:
    /// Which constructor a key belongs to. Part of the key: different constructors can have
    /// the same components.
    enum class Shape : uint32_t {
        Tuple,    // TupleType::create (layouts included); not cells or tagged forms
        Array,    // ArrayType::create
        Stream,   // StreamType::create
        Struct,   // StructType
        Function, // FunctionType::create, FunctionTypeFactory::build
    };

    static TypeTable &instance();

    /// The type registered under (`shape`, `words`), or the object `make()` returns, which is
    /// then registered under that key and given a TypeId. `make` runs under the table's lock
    /// and must not call back into the table.
    Type *
    intern(Shape shape, std::span<const uintptr_t> words, const std::function<Type *()> &make);

    /// Gives a type that its constructor already keeps unique (a primitive singleton, a variable
    /// cell, a `T?` or `T!`) its TypeId. Does nothing when the type has one.
    void adopt(Type *type);

    /// The type with this id. Lock-free. `id` must come from Type::typeId() and be non-zero.
    Type *get(TypeId id) const { return byId_[id]; }

    /**
     * The canonical extent list of a shape: one entry per distinct shape text
     * (`ExtentForm::toString`), which is normalized by construction (terms sorted and merged,
     * zero coefficients dropped). The same shape written two ways therefore has one address,
     * and an array type's intern key is four words instead of a variable-length shape. Empty
     * input has no entry: it means "no dimensions", not a one-dimension shape, and maps to
     * nullptr. The returned vector lives in the table and is immutable.
     */
    const std::vector<ExtentForm> *internExtents(std::span<const ExtentForm> extents);

    /// An interned text (a dimension pack name), by value. Empty maps to nullptr.
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

    /// Appends `type` to byId_ and stores the id in the object. Caller holds mutex_.
    void assignId(Type *type);

    mutable std::mutex mutex_;
    std::unordered_map<Key, Type *, KeyHash, KeyEqual> byKey_;
    camel::utils::AppendOnlyTable<Type *> byId_; // index 0 is reserved (kNoTypeId)
    std::unordered_map<std::string, std::vector<ExtentForm>> extents_;
    std::unordered_map<std::string, std::string> texts_;
    size_t hits_ = 0;
};

} // namespace camel::core::type
