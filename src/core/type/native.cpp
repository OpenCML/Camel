// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/native.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace camel::core::type {

namespace {

std::mutex &tableMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, Type *> &table() {
    static std::unordered_map<std::string, Type *> types;
    return types;
}

std::string keyOf(std::string_view module, std::string_view name) {
    std::string key(module);
    key.push_back('\x1f');
    key.append(name);
    return key;
}

} // namespace

void registerNativeType(std::string_view module, std::string_view name, Type *type) {
    std::lock_guard<std::mutex> lock(tableMutex());
    table()[keyOf(module, name)] = type;
}

Type *findNativeType(std::string_view module, std::string_view name) {
    std::lock_guard<std::mutex> lock(tableMutex());
    const auto found = table().find(keyOf(module, name));
    return found == table().end() ? nullptr : found->second;
}

} // namespace camel::core::type
