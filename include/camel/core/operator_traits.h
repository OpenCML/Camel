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

#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

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

} // namespace camel::core
