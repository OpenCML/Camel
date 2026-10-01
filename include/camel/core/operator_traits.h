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
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Semantic properties of operators, keyed by operator URI.
 *
 * Generic graph passes (constant folding, CSE, DCE) must not know about any
 * particular module, yet they need to know whether an operator may be
 * evaluated early, merged, or dropped. Modules and the builtin operator table
 * register those properties here when they are loaded; passes only query.
 * An operator without an entry is treated conservatively (effectful).
 */

#pragma once

#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/resolver.h"

#include <functional>
#include <optional>
#include <span>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::core {

struct OperatorTraits {
    /// No side effects and no hidden state: the result depends only on the arguments, so a call
    /// may be evaluated at compile time, merged with an identical call, or removed if unused.
    bool pure = false;
    /// Output element i depends only on input elements i (after broadcasting).
    bool elementwise = false;
};

class OperatorTraitsRegistry {
  public:
    static OperatorTraitsRegistry &instance();

    /// Registers (or replaces) the traits of `uri`, e.g. "tensor:matmul" or ":op/add_i".
    void set(std::string_view uri, OperatorTraits traits);

    /// Traits of `uri`, or nullopt when nothing was registered.
    std::optional<OperatorTraits> find(std::string_view uri) const;

    /// True when `uri` is registered as pure.
    bool isPure(std::string_view uri) const;

  private:
    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, OperatorTraits, StringHash, std::equal_to<>> traits_;
};

/**
 * The type resolver of every operator overload, keyed by its URI. An OPER node records only the
 * URI its call resolved to; passes that change what is known about the arguments (binding input
 * shapes, folding a shape to a constant) re-run the overload's resolver through this registry.
 * Operator groups register their overloads when they are created.
 */
class OperatorResolverRegistry {
  public:
    static OperatorResolverRegistry &instance();

    /// Registers (or replaces) the resolver of `uri`.
    void set(std::string_view uri, type::resolver_ptr_t resolver);

    /// The resolver of `uri`, or nullptr.
    type::resolver_ptr_t find(std::string_view uri) const;

  private:
    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, type::resolver_ptr_t, StringHash, std::equal_to<>> resolvers_;
};

/**
 * The value an operator's result has whenever its arguments have the given types, for operators
 * whose result a type can fix: the shape of a tensor whose type carries its shape, its element
 * count, one of its dimensions. `statics` holds the arguments that are constants (aligned with
 * `types`); `allocator` holds any object the value needs. Returns nullopt when the types do not
 * fix the result. std::opt::fold uses these to turn such results into constants once shape
 * specialization has put the facts into the types.
 */
using TypeFolder = std::function<std::optional<slot_t>(
    std::span<type::Type *const> types, type::static_args_t statics, mm::IAllocator &allocator)>;

class OperatorTypeFolderRegistry {
  public:
    static OperatorTypeFolderRegistry &instance();

    /// Registers (or replaces) the folder of `uri`.
    void set(std::string_view uri, TypeFolder folder);

    /// The folder of `uri`, or nullptr.
    const TypeFolder *find(std::string_view uri) const;

  private:
    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, TypeFolder, StringHash, std::equal_to<>> folders_;
};

/**
 * The exact type of a constant, for types that do not fix everything about their values: a
 * tensor constant typed `Tensor` has a dtype and a shape. Returns nullptr when `type` is not one
 * the refiner knows. Graph rewrites give constants these types, so that shape facts flow from
 * captured weights into the operators that use them.
 */
using ValueTypeRefiner = std::function<type::Type *(slot_t value, type::Type *type)>;

class ValueTypeRefinerRegistry {
  public:
    static ValueTypeRefinerRegistry &instance();

    void add(ValueTypeRefiner refiner);

    /// The exact type of `value`, or `type` when no refiner knows more.
    type::Type *refine(slot_t value, type::Type *type) const;

  private:
    mutable std::shared_mutex mutex_;
    std::vector<ValueTypeRefiner> refiners_;
};

} // namespace camel::core
