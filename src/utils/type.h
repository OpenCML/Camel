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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Jul. 08, 2025
 * Updated: Jul. 08, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once
#include <memory>
#include <type_traits>
#include <typeinfo>

namespace TypeTraits {

template <typename Base, typename Derived> constexpr bool is_base_of() { return std::is_base_of<Base, Derived>::value; }

template <typename Derived, typename Base> constexpr bool is_derived_of() {
    return std::is_base_of<Base, Derived>::value && !std::is_same<Base, Derived>::value;
}

template <typename Target, typename Source> bool is_instance_of(Source *ptr) {
    static_assert(std::has_virtual_destructor_v<Source>, "Source must be a polymorphic type");
    return dynamic_cast<Target *>(ptr) != nullptr;
}

template <typename Target, typename Source> bool is_exact_type(const Source &obj) {
    return typeid(obj) == typeid(Target);
}

template <typename Target, typename Source> Target *as(Source *ptr) {
    static_assert(std::has_virtual_destructor_v<Source>, "Source must be a polymorphic type");
    return dynamic_cast<Target *>(ptr);
}

template <typename Target, typename Source> bool is_shared_instance_of(const std::shared_ptr<Source> &ptr) {
    return std::dynamic_pointer_cast<Target>(ptr) != nullptr;
}

template <typename Target, typename Source> std::shared_ptr<Target> as_shared(const std::shared_ptr<Source> &ptr) {
    return std::dynamic_pointer_cast<Target>(ptr);
}

template <template <typename...> class Template, typename T> struct is_instance_of_template : std::false_type {};

template <template <typename...> class Template, typename... Args>
struct is_instance_of_template<Template, Template<Args...>> : std::true_type {};

template <typename T, typename U> constexpr bool is_same_template() { return false; }

template <template <typename...> class Template, typename... Args1, typename... Args2>
constexpr bool is_same_template(Template<Args1...> *, Template<Args2...> *) {
    return true;
}

template <typename T, typename... Types> constexpr bool is_any_of() { return (std::is_same<T, Types>::value || ...); }

} // namespace TypeTraits

namespace tt = TypeTraits;