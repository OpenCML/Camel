// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/node_attrs.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata.h"
#include "camel/core/type/base.h"
#include "camel/core/type/resolver.h"

#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace camel::core::context {
class Context;
}
class Operator;
class OperatorIndex;
class OperatorGroup;

using data_idx_t = int32_t;
using data_arr_t = std::span<const data_idx_t>;

namespace type    = camel::core::type;
namespace rtdata  = camel::core::rtdata;
namespace context = camel::core::context;

class ArgsView {
  public:
    virtual ~ArgsView()                              = default;
    virtual size_t size() const                      = 0;
    virtual slot_t slot(size_t index) const          = 0;
    virtual void setSlot(size_t index, slot_t value) = 0;
    virtual type::TypeCode code(size_t index) const  = 0;
    virtual type::Type *type(size_t index) const     = 0;

    /// Static output layout of this monomorphic call, not a runtime type-valued argument.
    type::Type *resultType() const { return resultType_; }
    void setResultType(type::Type *result) { resultType_ = result; }

    /// The attributes of the node this call runs (camel/core/node_attrs.h): what a pass recorded
    /// about the node, e.g. which implementation runs it. 0 when the caller has none. The core
    /// knows no key: the scheduler hands the set over and the operator's own module reads it.
    camel::core::attr::AttrSetId attrs() const { return attrs_; }
    void setAttrs(camel::core::attr::AttrSetId attrs) { attrs_ = attrs; }

    template <typename T> T get(size_t index) const { return rtdata::fromSlot<T>(slot(index)); }
    template <typename T> void set(size_t index, T value) { setSlot(index, rtdata::toSlot(value)); }

  private:
    type::Type *resultType_ = nullptr;
    camel::core::attr::AttrSetId attrs_ = camel::core::attr::kNoAttrs;
};

using operator_t = slot_t (*)(ArgsView &with, ArgsView &norm, context::Context &ctx);

using oper_idx_ptr_t     = std::shared_ptr<OperatorIndex>;
using oper_idx_vec_t     = std::vector<oper_idx_ptr_t>;
using oper_idx_vec_ptr_t = std::shared_ptr<oper_idx_vec_t>;

class OperatorIndex {
  private:
    std::string name_;
    type::FunctionType *type_;
    std::string uri_;

  public:
    OperatorIndex(const std::string &name, type::FunctionType *type, const std::string &uri)
        : name_(name), type_(std::move(type)), uri_(uri) {}

    const std::string &name() const { return name_; }
    const std::string &uri() const { return uri_; }
    type::FunctionType *funcType() const { return type_; }
};

using oper_group_ptr_t = std::shared_ptr<OperatorGroup>;

class OperatorGroup {
  private:
    std::string name_;
    std::vector<std::pair<std::string, type::resolver_ptr_t>> resolvers_;

  public:
    OperatorGroup(
        const std::string &name,
        const std::vector<std::pair<std::string, type::resolver_ptr_t>> &resolvers)
        : name_(name), resolvers_(resolvers) {
        rejectOverlappingRules();
        publishResolvers();
    }
    OperatorGroup(
        const std::string &name,
        const std::vector<std::pair<std::string, type::resolver_ptr_t>> &&resolvers,
        bool registration = true)
        : name_(name), resolvers_(std::move(resolvers)) {
        if (registration) {
            rejectOverlappingRules();
        }
        publishResolvers();
    }

    static oper_group_ptr_t create(
        const std::string &name,
        const std::vector<std::pair<std::string, type::resolver_ptr_t>> &resolvers) {
        return std::make_shared<OperatorGroup>(name, resolvers);
    }
    static oper_group_ptr_t create(
        const std::string &name,
        const std::vector<std::pair<std::string, type::resolver_ptr_t>> &&resolvers) {
        return std::make_shared<OperatorGroup>(name, std::move(resolvers));
    }
    /// The group a program sees for a name that several modules export: the rules of all of
    /// them. Whether two of them accept the same arguments is the program's business (an
    /// ambiguity at the call), not a registration error, so nothing is rejected here.
    static oper_group_ptr_t merged(
        const std::string &name,
        std::vector<std::pair<std::string, type::resolver_ptr_t>> &&resolvers) {
        return std::make_shared<OperatorGroup>(name, std::move(resolvers), false);
    }

    const std::string &name() const { return name_; }

    const auto &resolvers() const { return resolvers_; }

    /// Adds a rule to a group that is already shared: the prelude's declaration of a built-in
    /// operator is the rule, and the modules that hold the group see it at once.
    void addRule(const std::string &uri, type::resolver_ptr_t resolver) {
        resolvers_.emplace_back(uri, std::move(resolver));
        rejectOverlappingRules();
        publishResolvers();
    }

    std::optional<oper_idx_ptr_t> resolve(
        const type::type_vec_t &with, const type::type_vec_t &norm) const {
        for (const auto &[uri, resolver] : resolvers_) {
            auto optType = resolver->resolve(with, norm);
            if (optType) {
                return std::make_shared<OperatorIndex>(name_, std::move(*optType), uri);
            }
        }
        return std::nullopt;
    }

    /// Like resolve(), also passing the compile-time values of constant norm arguments.
    std::optional<oper_idx_ptr_t> resolve(
        const type::type_vec_t &with, const type::type_vec_t &norm, type::static_args_t normStatics) const {
        for (const auto &[uri, resolver] : resolvers_) {
            auto optType = resolver->resolveWith(with, norm, normStatics);
            if (optType) {
                return std::make_shared<OperatorIndex>(name_, std::move(*optType), uri);
            }
        }
        return std::nullopt;
    }

    /// A typed call: the type context is consumed by the resolver before a node is made.
    std::optional<oper_idx_ptr_t> resolveForCall(
        const type::type_vec_t &types, type::Type *expectedResult, const type::type_vec_t &with,
        const type::type_vec_t &norm, type::static_args_t normStatics) const {
        for (const auto &[uri, resolver] : resolvers_) {
            auto result =
                resolver->resolveForCall(types, expectedResult, with, norm, normStatics);
            if (result) {
                return std::make_shared<OperatorIndex>(name_, *result, uri);
            }
        }
        return std::nullopt;
    }

  private:
    /// Two rules of one group that accept exactly the same arguments cannot both be meant:
    /// the first would silently win. A registration error, found when the module loads
    /// (design principles log 2.17). Only rules that declare a fixed signature can be compared.
    void rejectOverlappingRules() const {
        for (size_t i = 0; i < resolvers_.size(); ++i) {
            const type::FunctionType *a =
                resolvers_[i].second ? resolvers_[i].second->fixedSignature() : nullptr;
            for (size_t j = i + 1; a != nullptr && j < resolvers_.size(); ++j) {
                const type::FunctionType *b =
                    resolvers_[j].second ? resolvers_[j].second->fixedSignature() : nullptr;
                if (b == nullptr || a->withTypesCount() != b->withTypesCount() ||
                    a->normTypesCount() != b->normTypesCount()) {
                    continue;
                }
                bool same = true;
                for (size_t k = 0; same && k < a->withTypesCount(); ++k) {
                    same = a->withTypeAt(k)->equals(b->withTypeAt(k));
                }
                for (size_t k = 0; same && k < a->normTypesCount(); ++k) {
                    same = a->normTypeAt(k)->equals(b->normTypeAt(k));
                }
                if (same) {
                    throw std::logic_error(
                        "operator group '" + name_ + "': the rules " + resolvers_[i].first +
                        " and " + resolvers_[j].first + " both accept " + a->toString());
                }
            }
        }
    }

    /// Makes each overload's resolver findable by its URI (see OperatorTable::setResolver), and
    /// tells the table which overloads this group has, so that the types it was registered for can
    /// be listed (OperatorTable::registeredTypes).
    void publishResolvers() const {
        std::vector<std::string> uris;
        uris.reserve(resolvers_.size());
        for (const auto &[uri, resolver] : resolvers_) {
            if (!uri.empty() && resolver) {
                camel::core::OperatorTable::instance().setResolver(uri, resolver);
                uris.push_back(uri);
            }
        }
        if (!uris.empty()) {
            camel::core::OperatorTable::instance().declareGroup(name_, uris);
        }
    }
};
