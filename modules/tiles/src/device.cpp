/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Interned device storage types preserve the host tensor's static refinements.
#include "tiles/device.h"
#include "camel/core/mm.h"
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
    if (!tensor::asTensorType(host) && host->code() != TypeCode::Float64)
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
OtherType *DeviceType::cloneWithParams(std::span<Type *const> params) const {
    if (params.size() != 1 ||
        (!tensor::asTensorType(params[0]) && params[0]->code() != TypeCode::Float64))
        return nullptr;
    return get(params[0]);
}
const DeviceType *asDeviceType(const Type *type) {
    return type && type->code() == DeviceType::typeCode() ? static_cast<const DeviceType *>(type)
                                                          : nullptr;
}
} // namespace camel::tiles
