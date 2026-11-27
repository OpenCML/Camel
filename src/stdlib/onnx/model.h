// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The type `Model<F>` of the onnx module and its run-time object.
 *
 * A model is what `onnx::model` gives back: the ONNX file the function and its example were
 * translated to, as bytes, and nothing else. Those bytes are the whole content -- `save` writes
 * them, `run` builds the runtime from them -- so the object holds no reference into the program
 * that produced it.
 *
 * That is deliberate. `onnx::exportFunction` runs inside a `NoCollectionScope`, and what it reads
 * (the function, the examples, the constants it translates) is held as raw slots for the duration
 * of that call. A value of this type outlives the call, so it must not borrow any of them: the
 * serialized bytes are copied into a buffer the object owns, and the function and the example are
 * read only while `onnx::model` runs. Nothing below a Model is an untyped slot; the object holds
 * no reference at all.
 *
 * `F` is the function type the model was made from. It takes part in type checking only -- it is
 * what lets the module state `run` and `save` in terms of the arguments and the result of `F` --
 * and is not part of the run-time representation.
 */

#pragma once

#include "camel/core/mm.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/other.h"

#include <cstddef>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace camel::onnx {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

/// The type `Model<F>`, one object per function type `F`.
class ModelType : public type::OtherType {
  public:
    static type::TypeCode typeCode();
    /// The bare `Model`: a model whose function type the program did not state.
    static ModelType *get();
    /// `Model<F>`.
    static ModelType *of(type::Type *source);

    /// The function type the model was made from, null for a bare `Model`.
    type::Type *sourceType() const { return paramCount() == 1 ? params()[0] : nullptr; }

    /// `F` is the type's one argument, so `Model<F>` against `Model<(int) => int>` binds `F`.
    bool typeArguments() const override { return true; }

    std::string toString() const override;
    std::string mangle() const override;
    type::Type *clone(bool deep = false) const override;
    bool equals(type::Type *type) const override;
    type::CastSafety castSafetyFrom(type::Type *sourceType) const override;
    bool assignableFrom(type::Type *sourceType) const override;
    type::OtherType *cloneWithParams(std::span<type::Type *const> params) const override;

  private:
    ModelType(type::TypeCode code, type::Type *source);
};

/// Whether `type` is a `Model<F>`.
bool isModelType(const type::Type *type);

/// A model's bytes. The object owns its buffer: it is a copy, not a view of anything the export
/// held, because the value outlives the call that made it.
class ModelValue : public rtdata::Object {
  public:
    ModelValue(const ModelValue &)            = delete;
    ModelValue &operator=(const ModelValue &) = delete;

    /// A model of the given bytes, copied.
    static ModelValue *create(std::span<const std::byte> bytes, mm::IAllocator &allocator);

    std::span<const std::byte> bytes() const { return bytes_; }

    bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override;
    rtdata::Object *
    clone(mm::IAllocator &allocator, const type::Type *type, bool deep = false) const override;
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override {}
    void updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) override;

  private:
    explicit ModelValue(std::span<const std::byte> bytes) : bytes_(bytes.begin(), bytes.end()) {}

    std::vector<std::byte> bytes_;
};

} // namespace camel::onnx