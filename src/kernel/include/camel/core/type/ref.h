// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `Ref<T>`: the core's handle type for one mutable box, one resource per value.
 *
 * A reference is a handle (design principles 3.3): `get` of it is a read, `set` and `update` are
 * writes, and the resource a signature names is `state::Ref`. Two references made by two `ref`
 * calls are two resources, so their operations keep no order between them; two operations on one
 * reference keep the order they are written in. The type lives here, in the core, because that is
 * what makes the two rules about it -- one resource per value, and a content that holds no
 * reference -- rules of the language: the state module (`modules/state`) provides the operators
 * and the box, and `stdlib/state.cml` declares the type among its own.
 *
 * The content is any type but one that holds a `Ref`. The boxes are reclaimed by counting the
 * handles that point at them, so two boxes holding each other's handles would keep each other's
 * count above zero for ever. The compiler refuses such a content where the reference is made
 * (builder_refs.cpp), and it looks one layer at a time into what `T` holds, a closure's captures
 * included -- a function type does not spell them, but the value that carries the closures does.
 */

#pragma once

#include "camel/core/type/other.h"

#include <span>
#include <string>

namespace camel::core::type {

class RefType final : public OtherType {
  public:
    /// The bare `Ref`: a reference whose content type the program did not state.
    static RefType *get();
    /// `Ref<T>`, one object per content type.
    static RefType *of(Type *content);
    static bool is(const Type *type) { return type != nullptr && type->code() == get()->code(); }
    static const RefType *as(const Type *type) {
        return is(type) ? static_cast<const RefType *>(type) : nullptr;
    }

    /// The type `T` of the box's content, or null when it is written without one.
    Type *contentType() const { return paramCount_ == 1 ? params_[0] : nullptr; }

    /// A reference is a state its operations change: a handle type, one resource per value, named
    /// `state::Ref` in a signature's `uses` clause (the name a native handle type gives, as
    /// `graph::Txn` does).
    bool isResourceHandle() const override { return true; }
    std::string resourceHandleName() const override { return "state::Ref"; }

    /// The content type is the type's one argument, so `Ref<T>` against `Ref<int>` binds `T`.
    bool typeArguments() const override { return true; }

    std::string toString() const override;
    std::string mangle() const override;
    Type *clone(bool deep = false) const override;
    Type *unify(Type *other) const override;
    bool equals(Type *other) const override;
    CastSafety castSafetyFrom(Type *source) const override;
    bool assignableFrom(Type *source) const override;
    OtherType *cloneWithParams(std::span<Type *const> params) const override;
    Type *cloneWithArguments(std::span<const TypeArgument> arguments, std::string &error)
        const override;

  private:
    RefType(TypeCode code, Type *content);
};

} // namespace camel::core::type