/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Interned device storage types preserve the host tensor's static refinements.
#include "tiles/device.h"
#include "camel/core/mm.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "modules/tensor/type.h"
#include <mutex>
#include <unordered_map>

namespace camel::tiles {
using namespace core::type;
TypeCode DeviceType::typeCode() {
    static auto code = registerOtherType("tiles.Device", TypeFlag::GC_Traced);
    return code;
}
DeviceType::DeviceType(Type *host)
    : OtherType(typeCode(), 1, copyParams(std::span<Type *const>(&host, 1))) {}
DeviceType *DeviceType::get(Type *host) {
    if (!tensor::asTensorType(host) && host->code() != TypeCode::Float64 &&
        host->code() != TypeCode::TypeVar)
        throw std::runtime_error("Device requires a tensor or float type");
    static std::mutex mutex;
    static std::unordered_map<Type *, DeviceType *> types;
    std::lock_guard lock(mutex);
    if (auto it = types.find(host); it != types.end())
        return it->second;
    void *memory = core::mm::permSpace().alloc(sizeof(DeviceType), alignof(DeviceType));
    auto *type   = new (memory) DeviceType(host);
    types.emplace(host, type);
    return type;
}
Type *DeviceType::hostType() const { return params_[0]; }
std::string DeviceType::toString() const { return "Device<" + hostType()->toString() + ">"; }
std::string DeviceType::mangle() const { return "tiles_Device_" + hostType()->mangle(); }
Type *DeviceType::clone(bool) const { return const_cast<DeviceType *>(this); }
bool DeviceType::equals(Type *other) const {
    const auto *d = asDeviceType(other);
    return d && hostType()->equals(d->hostType());
}
bool DeviceType::assignableFrom(Type *other) const {
    const auto *d = asDeviceType(other);
    return d && hostType()->assignableFrom(d->hostType());
}
Type *DeviceType::unify(Type *other) const {
    const auto *device = asDeviceType(other);
    if (!device)
        return nullptr;
    auto *host = hostType()->unify(device->hostType());
    return host ? get(host) : nullptr;
}
Type *DeviceType::widened() const { return get(hostType()->widened()); }
CastSafety DeviceType::castSafetyFrom(Type *other) const {
    return assignableFrom(other) ? CastSafety::Safe : CastSafety::Forbidden;
}
slot_t DeviceType::castSlotFrom(slot_t value, Type *other) const {
    if (!assignableFrom(other))
        throw std::runtime_error("unsupported device type conversion");
    return value;
}
bool DeviceType::typeArguments() const { return true; }
namespace {
Type *distributeDevice(Type *host, const std::string &path, std::string &error) {
    if (host->code() == TypeCode::Tuple) {
        auto *tuple = static_cast<TupleType *>(host);
        // Nullable/fallible boxes and mutable cells are not ordinary tuple trees.
        if (tuple->isTagged() || tuple->isCell()) {
            error = "Device leaf '" + path + "' has unsupported type " + host->toString();
            return nullptr;
        }
        type_vec_t fields;
        for (size_t i = 0; i < tuple->size(); ++i) {
            auto *field = distributeDevice(tuple->typeAt(i), path + "." + std::to_string(i), error);
            if (!field)
                return nullptr;
            fields.push_back(field);
        }
        return TupleType::create(fields);
    }
    if (host->code() == TypeCode::Struct) {
        auto *record = static_cast<StructType *>(host);
        StructTypeFactory fields;
        for (size_t i = 0; i < record->size(); ++i) {
            const std::string name(record->fieldName(i));
            auto *field = distributeDevice(record->typeAt(i), path + "." + name, error);
            if (!field)
                return nullptr;
            fields.add(name, field);
        }
        return fields.build();
    }
    if (tensor::asTensorType(host) || host->code() == TypeCode::Float64 ||
        host->code() == TypeCode::TypeVar)
        return DeviceType::get(host);
    error = "Device leaf '" + path + "' has unsupported type " + host->toString() +
            "; expected a tensor or float";
    return nullptr;
}
} // namespace
Type *DeviceType::cloneWithParams(std::span<Type *const> params) const {
    if (params.size() != 1)
        return nullptr;
    std::string error;
    return distributeDevice(params[0], "$", error);
}
Type *
DeviceType::cloneWithArguments(std::span<const TypeArgument> arguments, std::string &error) const {
    if (arguments.size() != 1 || !arguments[0].type) {
        error = "Device takes exactly one type argument";
        return nullptr;
    }
    return distributeDevice(arguments[0].type, "$", error);
}
const DeviceType *asDeviceType(const Type *type) {
    return type && type->code() == DeviceType::typeCode() ? static_cast<const DeviceType *>(type)
                                                          : nullptr;
}
} // namespace camel::tiles
