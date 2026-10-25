// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The type of a rule and the object behind it (rewrite design 2.1, graph store 5.2).
 *
 * A rule declaration is two functions of the same parameters and a call that makes the rule from
 * them: the left function is the pattern, the right one what the matched value becomes. The
 * rule value is what the engines of this module take (a list of them is an ordinary array). It
 * holds the two function values, so it keeps the programs they belong to alive.
 *
 * A rule whose head declares type parameters or `with` parameters is the exception: it has no
 * function values to hold, and keeps the id of the templates it was registered under instead
 * (camel/compile/gir/rule_templates.h; the ruling of 2026-10-23 on generic-rule-instantiation, and
 * the ruling of 2026-10-25 on the one instantiation channel). Its sides and its condition are made
 * where it matches, one instance per type tuple and set of `with` values, and the `with` values it
 * was bound to are what it carries beside the id.
 */

#pragma once

#include "camel/core/data/base.h"
#include "camel/core/operator_semantics.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/symbol.h"
#include "camel/core/type/other.h"

#include <cstdint>
#include <string>
#include <vector>

namespace camel::rewritemod {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;
namespace data   = camel::core::data;

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

    /// `when` may be null (a rule without a condition). `ruleId` is 0 for an ordinary rule, whose
    /// two sides are `lhs` and `rhs`, and the id its templates were registered under for a rule
    /// that is instantiated, which holds neither side and no condition. `withValues` are the values
    /// its `with` parameters were bound to, in the order the declaration writes them; empty for a
    /// rule that declares none.
    static RuleObject *create(
        camel::core::SymId name, bool twoWay, camel::core::sem::PermissionMask needs, uint64_t constParams,
        ::Function *lhs, ::Function *rhs, ::Function *when, mm::IAllocator &allocator,
        uint64_t ruleId = 0, std::vector<data::data_ptr_t> withValues = {});

    std::string_view name() const { return camel::core::symbolText(name_); }
    /// The templates of a rule that is instantiated, or 0: see the class comment. A rule value that
    /// keeps an id has no `lhs`, `rhs` or `when` of its own.
    uint64_t ruleId() const { return ruleId_; }
    /// The values the rule's `with` parameters were bound to, in the order the declaration writes
    /// them. Empty for a rule that declares none.
    const std::vector<data::data_ptr_t> &withValues() const { return withValues_; }
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
        ::Function *lhs, ::Function *rhs, ::Function *when, uint64_t ruleId = 0,
        std::vector<data::data_ptr_t> withValues = {})
        : name_(name), twoWay_(twoWay), needs_(needs), constParams_(constParams), lhs_(lhs),
          rhs_(rhs), when_(when), ruleId_(ruleId), withValues_(std::move(withValues)) {}

    camel::core::SymId name_;
    bool twoWay_;
    camel::core::sem::PermissionMask needs_;
    uint64_t constParams_;
    ::Function *lhs_;
    ::Function *rhs_;
    ::Function *when_;
    /// See ruleId(): 0 for an ordinary rule.
    uint64_t ruleId_ = 0;
    /// See withValues(): the values the rule was bound to, which are the instance key's value half.
    std::vector<data::data_ptr_t> withValues_;
};

} // namespace camel::rewritemod

