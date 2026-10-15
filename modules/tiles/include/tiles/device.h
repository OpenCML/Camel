/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Typed device values own native storage, never pointers into Camel's moving heap.
#pragma once
#include "camel/core/type/other.h"
#include "tiles/program.h"
#include <cstddef>
#include <memory>
#include <span>

namespace camel::tiles {
struct DeviceValue {
    Shape shape;
    DType dtype      = DType::F32;
    uint64_t address = 0;
    std::shared_ptr<void> owner;
};
std::shared_ptr<DeviceValue>
uploadValue(const Shape &shape, DType dtype, std::span<const std::byte> data);
std::vector<std::byte> downloadValue(const DeviceValue &value);
std::vector<std::shared_ptr<DeviceValue>> executeResident(std::shared_ptr<const Program> program);

class DeviceType final : public core::type::OtherType {
  public:
    static DeviceType *get(core::type::Type *host);
    static core::type::TypeCode typeCode();
    core::type::Type *hostType() const;
    std::string toString() const override;
    std::string mangle() const override;
    core::type::Type *clone(bool deep = false) const override;
    bool equals(core::type::Type *other) const override;
    bool assignableFrom(core::type::Type *other) const override;
    core::type::Type *unify(core::type::Type *other) const override;
    core::type::Type *widened() const override;
    core::type::CastSafety castSafetyFrom(core::type::Type *other) const override;
    slot_t castSlotFrom(slot_t value, core::type::Type *other) const override;
    bool typeArguments() const override;
    core::type::OtherType *
    cloneWithParams(std::span<core::type::Type *const> params) const override;

  private:
    explicit DeviceType(core::type::Type *host);
};
const DeviceType *asDeviceType(const core::type::Type *type);
} // namespace camel::tiles
