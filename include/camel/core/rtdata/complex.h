// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A complex number value (design principles 5.1, 10.5).
 *
 *   c64    two IEEE f32 parts
 *   c128   two IEEE f64 parts
 *
 * Both are boxed in a GC object: a c128 does not fit in a slot, and a c64 is boxed too so that
 * every complex number is one kind of value (the same object, the same code paths for reading,
 * printing, comparing and converting) rather than a slot for one width and an object for the
 * other. The value is pure data for the data block (type/pure.h): an array of complex numbers
 * stores the two parts compactly and reads one element by allocating a box. A stored complex is
 * never mutated; arithmetic allocates a new one.
 */

#pragma once

#include "base.h"

#include <new>

namespace camel::core::rtdata {

class Complex : public Object {
  public:
    Complex(const Complex &)            = delete;
    Complex &operator=(const Complex &) = delete;

    /// A complex value of `code` (Complex64 or Complex128). A c64 rounds both parts to f32, so
    /// the value in the box is exactly a c64 value.
    static Complex *create(
        double real, double imaginary, camel::core::type::TypeCode code,
        camel::core::mm::IAllocator &allocator) {
        ASSERT(
            camel::core::type::isComplex(code),
            "A complex value has a complex type code");
        if (code == camel::core::type::TypeCode::Complex64) {
            real      = static_cast<float>(real);
            imaginary = static_cast<float>(imaginary);
        }
        void *memory = allocator.alloc(sizeof(Complex), alignof(Complex));
        if (!memory)
            throw std::bad_alloc();
        return new (memory) Complex(real, imaginary, code);
    }

    camel::core::type::TypeCode typeCode() const { return code_; }
    bool isSingle() const {
        return code_ == camel::core::type::TypeCode::Complex64;
    }

    double real() const { return real_; }
    double imaginary() const { return imaginary_; }
    /// The parts as the width of the value: a c64 part is exactly an f32.
    float realF() const { return static_cast<float>(real_); }
    float imaginaryF() const { return static_cast<float>(imaginary_); }

    bool equals(
        const Object *other, const camel::core::type::Type *type,
        bool deep = false) const override {
        (void)deep;
        (void)type;
        if (this == other)
            return true;
        if (!isOfSameCls(this, other))
            return false;
        const auto *rhs = reinterpret_cast<const Complex *>(other);
        return code_ == rhs->code_ && real_ == rhs->real_ && imaginary_ == rhs->imaginary_;
    }

    Object *clone(
        camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type,
        bool deep = false) const override {
        (void)type;
        (void)deep;
        return create(real_, imaginary_, code_, allocator);
    }

    void print(std::ostream &os, const camel::core::type::Type *type) const override {
        (void)type;
        if (isSingle()) {
            os << camel::utils::complexText(realF(), imaginaryF());
        } else {
            os << camel::utils::complexText(real_, imaginary_);
        }
    }

    void onMoved() override {}

    void updateRefs(
        const RefRelocator &relocate, const camel::core::type::Type *type) override {
        (void)relocate;
        (void)type;
    }

  private:
    Complex(double real, double imaginary, camel::core::type::TypeCode code)
        : real_(real), imaginary_(imaginary), code_(code) {}

    double real_;
    double imaginary_;
    camel::core::type::TypeCode code_;
};

} // namespace camel::core::rtdata