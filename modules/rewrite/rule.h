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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */


/*
 * The type of a rule and the object behind it (rewrite design 2.1, graph store 5.2).
 *
 * A rule declaration is two functions of the same parameters and a call that makes the rule from
 * them: the left function is the pattern, the right one what the matched value becomes. The
 * rule value is what the engines of this module take (a list of them is an ordinary array). It
 * holds the two function values, so it keeps the programs they belong to alive.
 */

#pragma once

#include "camel/core/operator_semantics.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/symbol.h"
#include "camel/core/type/other.h"

#include <cstdint>
#include <string>

namespace camel::rewritemod {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

class RuleType final : public type::OtherType {
  public:
    static RuleType *get();
    static bool is(const type::Type *type);

    std::string toString() const override { return "Rule"; }
    std::string mangle() const override { return "rewrite.Rule"; }
    type::Type *clone(bool deep = false) const override;
    bool equals(type::Type *other) const override;
    type::CastSafety castSafetyFrom(type::Type *source) const override;
    bool assignableFrom(type::Type *source) const override;
    type::OtherType *cloneWithParams(std::span<type::Type *const> params) const override;

  private:
    explicit RuleType(type::TypeCode code) : OtherType(code) {}
};

class RuleObject final : public rtdata::Object {
  public:
    RuleObject(const RuleObject &)            = delete;
    RuleObject &operator=(const RuleObject &) = delete;

    /// `when` may be null (a rule without a condition).
    static RuleObject *create(
        camel::core::SymId name, bool twoWay, camel::core::sem::PermissionMask needs, uint64_t constParams,
        ::Function *lhs, ::Function *rhs, ::Function *when, mm::IAllocator &allocator);

    std::string_view name() const { return camel::core::symbolText(name_); }
    /// `<=>`: both sides are the same value.
    bool twoWay() const { return twoWay_; }
    /// The numeric permissions the rule needs (camel/core/operator_semantics.h).
    camel::core::sem::PermissionMask needs() const { return needs_; }
    /// Bit i: parameter i is `const`.
    uint64_t constParams() const { return constParams_; }
    ::Function *lhs() const { return lhs_; }
    ::Function *rhs() const { return rhs_; }
    ::Function *when() const { return when_; }

    bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override;
    rtdata::Object *
    clone(mm::IAllocator &allocator, const type::Type *type, bool deep = false) const override;
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override {}
    void updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) override;

  private:
    RuleObject(
        camel::core::SymId name, bool twoWay, camel::core::sem::PermissionMask needs, uint64_t constParams,
        ::Function *lhs, ::Function *rhs, ::Function *when)
        : name_(name), twoWay_(twoWay), needs_(needs), constParams_(constParams), lhs_(lhs),
          rhs_(rhs), when_(when) {}

    camel::core::SymId name_;
    bool twoWay_;
    camel::core::sem::PermissionMask needs_;
    uint64_t constParams_;
    ::Function *lhs_;
    ::Function *rhs_;
    ::Function *when_;
};

} // namespace camel::rewritemod

