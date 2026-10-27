// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Function types: the signature of a function, and the type of a function value.
 *
 * A signature is the `with` and the ordinary parameters (each a type and a `var` mark), the parameter names and the result
 * type. It is an immutable object, interned in the type table (camel/core/type/intern.h):
 * equal signatures are one object. Parameter names do not take part in compatibility (equals
 * and assignableFrom ignore them); they are part of the interning key because a type prints
 * them (`(a: int, b: int) => int`) in diagnostics and graph dumps, and what is printed must not
 * depend on which of two compatible signatures a node happens to carry.
 *
 * Captures are not part of a function type: which names a graph captures is a fact about the
 * graph (GraphDraft::captureNames, GCGraph::captureNames), not about the type of its values.
 *
 * Open signatures. The front end creates the type of a function it is about to build before
 * it knows the result type when none is written: the body decides it. That one object is the
 * function's identity while it is built (a forward declaration and its definition share it),
 * and values of the function already carry it. Such a type is an OpenFunctionType: a
 * FunctionType that is not interned and whose result type is set once the body is built.
 * Only the constructors that say so (FunctionTypeFactory::buildOpen, OpenFunctionType::copyOf,
 * instantiateFunctionType) make one, and the type table never holds one, so an interned
 * signature cannot be modified by construction: setExitType() exists on OpenFunctionType only.
 *
 * Storage. One allocation in the permanent space: the fixed part below, then the parameter
 * types, their type codes, their names (SymIds, when the signature has names) and their `var`
 * marks, `with` parameters first, in that order.
 */

#pragma once

#include "camel/core/func.h"
#include "camel/core/resource_semantics.h"
#include "camel/core/symbol.h"
#include "camel/core/type/composite/composite.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace camel::core::type {

using param_t           = std::pair<Type *, bool>; // bool indicates whether the parameter is `var`.
using param_init_list_t = std::initializer_list<param_t>;
using param_vec_t       = std::vector<param_t>;

class FunctionType;
class OpenFunctionType;

// Collects a signature, then builds it: interned, or open (see the file comment).
class FunctionTypeFactory {
    friend class FunctionType;

  public:
    FunctionTypeFactory() = default;

    void setExitType(Type *type) { exitType_ = type; }
    /// The resources the signature publishes (design effects plan 2.2); Generic when not set.
    void setResources(const sem::ResourceSignature &resources) { resources_ = resources; }

    /// Adds a `with` parameter. `recordsValue` marks a parameter whose argument must be a value
    /// the compiler computes: the value is part of what the declaration means (a fact is computed
    /// and cached for one argument, not for another), so a call that gives it something the
    /// compiler cannot compute is refused where the argument is written.
    void addWithArg(const std::string &ident, Type *type, bool isVar, bool recordsValue = false) {
        if (recordsValue) {
            withConstIndices_.push_back(static_cast<uint16_t>(withTypes_.size()));
        }
        withTypes_.emplace_back(type, isVar);
        argNames_.push_back(ident);
    }

    void addNormArg(const std::string &ident, Type *type, bool isVar) {
        normTypes_.emplace_back(type, isVar);
        argNames_.push_back(ident);
    }

    /// The last ordinary parameter is a rest parameter (`..ts`): it stands for the arguments
    /// left over at a call and is expanded there (design principles 5.5). It is added after
    /// every other ordinary parameter, as a `void` parameter that no call ever passes a value
    /// for; `hasRest` tells it apart from an ordinary `void` parameter.
    void addRestArg(const std::string &ident) {
        hasRest_ = true;
        normTypes_.emplace_back(Type::Void(), false);
        argNames_.push_back(ident);
    }

    /// The `with` parameters end in a pack (`with <..args>`): it stands for the `with` arguments
    /// a call gives beyond the fixed ones, of any types, and is added after every other `with`
    /// parameter as a `void` parameter no call passes a value for. Only a declaration without a
    /// body (the rule of a native operator, which takes its arguments as they come) and a
    /// function type write one.
    void addWithRest(const std::string &ident) {
        hasWithRest_ = true;
        withTypes_.emplace_back(Type::Void(), false);
        argNames_.push_back(ident);
    }

    /// A `constas n: T` parameter: it records a compile-time value that is part of the
    /// declaration's identity, not a runtime argument (design principles 5.4). Its type stays in
    /// the signature so that the call site is checked against it, and the recorded index tells it
    /// apart from an ordinary parameter of the same type. A declaration may have any number of
    /// them, in any position among the ordinary parameters.
    void addConstasArg(const std::string &ident, Type *type) {
        constasIndices_.push_back(static_cast<uint16_t>(withTypes_.size() + normTypes_.size()));
        normTypes_.emplace_back(type, false);
        argNames_.push_back(ident);
    }

    bool hasConstas() const { return !constasIndices_.empty(); }

    /// Whether the signature records the parameter names given to addWithArg/addNormArg (the
    /// default). Without them it prints the parameter types only.
    void setHasCompileInfo(bool has) { hasNames_ = has; }

    /// The interned signature.
    FunctionType *build();
    /// A new open signature (see the file comment): not interned, its result type settable.
    OpenFunctionType *buildOpen();

  private:
    std::vector<param_t> withTypes_;
    std::vector<param_t> normTypes_;
    Type *exitType_ = nullptr;
    bool hasNames_  = true;
    bool hasRest_   = false;
    bool hasWithRest_ = false;
    std::vector<std::string> argNames_;
    sem::ResourceSignature resources_;
    /// The parameters written with `constas`, as indices over all parameters (`with` first).
    std::vector<uint16_t> constasIndices_;
    /// The `with` parameters whose argument must be a value the compiler computes, as indices
    /// among the `with` parameters.
    std::vector<uint16_t> withConstIndices_;
};

class FunctionType : public CompositeType {
    friend class FunctionTypeFactory;
    friend class OpenFunctionType;

  public:
    FunctionType(const FunctionType &)            = delete;
    FunctionType &operator=(const FunctionType &) = delete;

    /// The interned signature without parameters and result type, recording (no) names.
    static FunctionType *create();
    /// The interned signature with these parameters and result type, without names.
    static FunctionType *create(
        const param_init_list_t &withTypes, const param_init_list_t &normTypes, Type *returnType);
    static FunctionType *create(
        const param_vec_t &withTypes, const param_vec_t &normTypes, Type *returnType);

    /// The interned signature equal to this one in everything but the result type.
    FunctionType *withExitType(Type *exitType) const;

    /// The resources this function type publishes (design effects plan 2.2).
    const sem::ResourceSignature &resources() const { return resources_; }
    /// The interned signature equal to this one in everything but its resources.
    FunctionType *withResources(const sem::ResourceSignature &resources) const;

    /// This signature as an OpenFunctionType, or nullptr when it is not open.
    OpenFunctionType *asOpen() const;
    bool isOpen() const { return open_; }

    size_t withTypesCount() const { return withCount_; }
    size_t normTypesCount() const { return normCount_; }
    Type *withTypeAt(size_t index) const {
        ASSERT(index < withCount_, "Index out of range");
        return types()[index];
    }
    TypeCode withTypeCodeAt(size_t index) const {
        ASSERT(index < withCount_, "Index out of range");
        return codes()[index];
    }
    bool withIsVarAt(size_t index) const {
        ASSERT(index < withCount_, "Index out of range");
        return varMarks()[index] != 0;
    }
    Type *normTypeAt(size_t index) const {
        ASSERT(index < normCount_, "Index out of range");
        return types()[withCount_ + index];
    }
    TypeCode normTypeCodeAt(size_t index) const {
        ASSERT(index < normCount_, "Index out of range");
        return codes()[withCount_ + index];
    }
    bool normIsVarAt(size_t index) const {
        ASSERT(index < normCount_, "Index out of range");
        return varMarks()[withCount_ + index] != 0;
    }

    /// Whether the last ordinary parameter is a rest parameter (`..ts`). Its type in this
    /// signature is `void`; the tuple of the arguments it receives is made at the call.
    bool hasRest() const { return hasRest_; }

    /// Whether the last `with` parameter is a pack (`with <..args>`); its type here is `void`.
    bool hasWithRest() const { return hasWithRest_; }

    /// Whether the signature has a `constas n: T` parameter, which ones they are (indices over
    /// all parameters, `with` parameters first), and whether a given parameter is one.
    bool hasConstas() const { return !constasIndices_.empty(); }
    size_t constasCount() const { return constasIndices_.size(); }
    const std::vector<uint16_t> &constasIndices() const { return constasIndices_; }
    bool isConstasAt(size_t index) const {
        return std::find(constasIndices_.begin(), constasIndices_.end(), index) !=
               constasIndices_.end();
    }
    bool normIsConstasAt(size_t index) const {
        ASSERT(index < normCount_, "Index out of range");
        return isConstasAt(withCount_ + index);
    }

    /// Whether the `with` parameter at `index` must be given a value the compiler computes.
    bool withNeedsValueAt(size_t index) const {
        return std::find(withConstIndices_.begin(), withConstIndices_.end(), index) !=
               withConstIndices_.end();
    }

    std::span<Type *const> withTypesSpan() const { return {types(), withCount_}; }
    std::span<Type *const> normTypesSpan() const { return {types() + withCount_, normCount_}; }
    std::span<const TypeCode> withTypeCodesSpan() const { return {codes(), withCount_}; }
    std::span<const TypeCode> normTypeCodesSpan() const {
        return {codes() + withCount_, normCount_};
    }

    /// The result type; void when none is known (see hasExitType).
    Type *exitType() const;
    /// False while the body of an open signature has not decided the result type.
    bool hasExitType() const { return exitType_ != nullptr; }

    /// Whether the signature records parameter names (one per parameter, `with` first).
    bool hasArgNames() const { return hasNames_; }
    size_t argNamesCount() const { return hasNames_ ? paramCount() : 0; }
    std::string_view argNameAt(size_t idx) const;
    std::vector<std::string_view> argNames() const;

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    /// The interned signature with the same parts, the components cloned when `deep`.
    virtual Type *clone(bool deep = false) const override;
    /// This signature as a type: itself when it is interned, and for an open signature (the
    /// object a compiler is still writing) the interned signature of its parts as they stand.
    virtual Type *closed() const override;
    virtual bool equals(Type *type) const override;
    virtual Type *unify(Type *other) const override;
    virtual CastSafety castSafetyFrom(Type *sourceType) const override;
    virtual bool assignableFrom(Type *sourceType) const override;

  protected:
    /// Everything a signature consists of. `names` has one entry per parameter (`with` first)
    /// when `hasNames`, and is empty otherwise.
    struct Parts {
        std::vector<param_t> params; // `with` parameters first
        size_t withCount = 0;
        Type *exitType   = nullptr;
        bool hasNames    = false;
        bool hasRest     = false;
        bool hasWithRest = false;
        std::vector<uint16_t> constasIndices;
        /// The `with` parameters whose argument must be a compile-time value (see addWithArg).
        std::vector<uint16_t> withConstIndices;
        std::vector<SymId> names;
        sem::ResourceSignature resources;
    };

    FunctionType(const Parts &parts, bool open);

    /// The parts of this signature.
    Parts parts() const;
    /// A new object of class T (FunctionType or OpenFunctionType) holding `parts`.
    template <typename T> static T *allocate(const Parts &parts);
    /// The interned object holding `parts`.
    static FunctionType *intern(const Parts &parts);

    // Written by the constructor, and for an open signature by OpenFunctionType::setExitType.
    Type *exitType_ = nullptr;

  private:
    /// Bytes of the parameter arrays that follow the fixed part.
    static size_t trailingBytes(size_t params, bool hasNames);

    std::byte *trailing() const {
        return const_cast<std::byte *>(reinterpret_cast<const std::byte *>(this)) +
               sizeof(FunctionType);
    }
    size_t paramCount() const { return static_cast<size_t>(withCount_) + normCount_; }
    Type **types() const { return reinterpret_cast<Type **>(trailing()); }
    TypeCode *codes() const {
        return reinterpret_cast<TypeCode *>(trailing() + paramCount() * sizeof(Type *));
    }
    SymId *names() const {
        return reinterpret_cast<SymId *>(
            trailing() + paramCount() * (sizeof(Type *) + sizeof(TypeCode)));
    }
    uint8_t *varMarks() const {
        return reinterpret_cast<uint8_t *>(
            trailing() +
            paramCount() * (sizeof(Type *) + sizeof(TypeCode) + (hasNames_ ? sizeof(SymId) : 0)));
    }

    uint16_t withCount_ = 0;
    uint16_t normCount_ = 0;
    bool hasNames_      = false;
    bool hasRest_       = false;
    bool hasWithRest_   = false;
    /// The parameters written with `constas`, as indices over all parameters (`with` first):
    /// a compile-time value at the call, not a runtime parameter (design principles 5.4).
    std::vector<uint16_t> constasIndices_;
    /// The `with` parameters whose argument must be a compile-time value, as indices among the
    /// `with` parameters (see FunctionTypeFactory::addWithArg).
    std::vector<uint16_t> withConstIndices_;
    bool open_          = false;
    /// The resources the signature publishes (Generic when it says nothing).
    sem::ResourceSignature resources_;
    // Followed in memory by the parameter arrays (see the file comment).
};

/// A signature whose result type is decided after it was created: the type of a function the
/// front end is building (see the file comment). Never interned and never shared by two
/// functions. It adds no data to FunctionType, only the one mutator.
class OpenFunctionType final : public FunctionType {
    friend class FunctionType;
    friend class FunctionTypeFactory;

  public:
    /// Sets the result type, once the body that decides it is built.
    void setExitType(Type *type) { exitType_ = type; }
    /// Sets the resources once the body (or the declaration) decides them.
    void setResources(const sem::ResourceSignature &resources) { resources_ = resources; }

    /// A new open signature with the same parts as `type`, its parameter names included.
    static OpenFunctionType *copyOf(const FunctionType *type);

  private:
    OpenFunctionType(const Parts &parts) : FunctionType(parts, true) {}
};

/// Whether a value of type `value` may be the result of a function declared to give `declared`.
/// A function declared `void` discards whatever its body ends with; this is the one position
/// where `void` takes any type (`Type::assignableFrom` gives `void` to `void` only).
inline bool acceptsAsResult(Type *declared, Type *value) {
    return declared != nullptr &&
           (declared->code() == TypeCode::Void || declared->assignableFrom(value));
}

} // namespace camel::core::type
