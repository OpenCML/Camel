// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/type/composite/composite.h"

namespace camel::core::type {

class FunctionType;

/**
 * Immutable StreamType: `T..` (design principles 5.4), the elements of `T` that arrive one
 * after another — a sequence produced on demand, possibly endless.
 *
 * `T..` is a type of its own, not the open-rank array `T[..]` (the rule of
 * 2026-10-24 on the stream surface): `T[..]` is an array whose extents are a prefix, and all
 * its elements exist at once; `T..` is a stream. The suffix is read left to right, without a
 * case for each combination: `T?..` is `(T?)..`, `T!..` is `(T!)..`, `T[3]..` is the stream of
 * `T[3]`, `T[..]..` is the stream of the open-rank array, and `T..?` is the possibly absent
 * stream. The one restriction comes from linearity: a stream is consumed once, so it cannot be
 * the element of an array, a map or a set (a container's elements can be read many times), and
 * the builder that builds the container says so (SemanticDiag::StreamInContainer).
 *
 * Interned: one element type gives one object, so `T..` is one type however it is reached —
 * written as a suffix, named by a `type` alias, instantiated from a generic signature, or read
 * back from a graph. Equality, overload selection, `#impl` signature agreement and every
 * printed form follow from that one object.
 *
 * A stream is a value like any other: its value is its step function together with its state,
 * the closure that answers the next element (design principles 5.4: a stream is a value). So it
 * is written in a signature, passed, returned and `tee`d, and it needs no runtime
 * representation of its own for that, and no node kind of its own. What consumes it -- a `for`
 * that calls the step function once per round and stops when it comes back empty -- reads the
 * same whether the producer is fused into the consumer's loop (an optimisation, taken where
 * producer and consumer are both visible) or reached through the value.
 *
 * The value of `T..` is a function value of the step signature `() => (T, T..)?`
 * (StreamType::stepType): called, it answers the next element together with the rest of the
 * stream, or `null` when the stream is over. A stream and that function value are one value --
 * the state a stream is at lives in the function value's captures -- so a stream is converted
 * to its step signature and back by a cast that moves nothing.
 */
class StreamType : public CompositeType {
  public:
    // Direct construction is forbidden; use create.
    StreamType(const StreamType &)            = delete;
    StreamType &operator=(const StreamType &) = delete;

    /// `T..`: one interned object per element type.
    static StreamType *create(Type *elemType);

    /// The type of one element of the stream.
    Type *elemType() const { return elemType_; }

    /// `() => (T, T..)?`, the signature of one pull: the element and the rest of the stream, or
    /// `null` when there are no more elements. Every value of this type is a value of `T..` and
    /// the other way round; the type is what a consumer calls a stream through.
    FunctionType *stepType() const;

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual Type *unify(Type *other) const override;
    /// The widening a `var` binding needs: the element widens, the stream stays a stream.
    virtual Type *widened() const override;
    virtual CastSafety castSafetyFrom(Type *sourceType) const override;
    virtual bool assignableFrom(Type *sourceType) const override;
    /// A stream fixes no numeric type for a literal beside it: `x * 2.0` with `x: f32..` is not
    /// an operation on an element that has not arrived.
    virtual Type *literalOperandType() const override { return nullptr; }

  private:
    // Private constructor: called by create.
    explicit StreamType(Type *elemType) : CompositeType(TypeCode::Stream), elemType_(elemType) {}

    /// Interned construction.
    static StreamType *make(Type *elemType);

    Type *elemType_ = nullptr;
};

} // namespace camel::core::type