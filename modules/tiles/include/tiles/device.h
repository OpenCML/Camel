/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Typed device values own native storage, never pointers into Camel's moving heap.
#pragma once
#include "camel/core/type/other.h"
#include "tiles/program.h"
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace camel::tiles {
// Backend-owned storage has a checked dynamic type. It must never retain Camel
// heap pointers; concrete backends own their context and native allocation.
struct DeviceStorage {
    virtual ~DeviceStorage()                                                  = default;
    virtual std::vector<std::byte> read(uint64_t address, size_t bytes) const = 0;
};
// Native submissions may expose pending leaves only inside the backend. Camel
// operators must wait before publishing a Device value. Completion is separate
// from allocation ownership, including for aliases and zero-byte results.
struct DeviceCompletion {
    virtual ~DeviceCompletion() = default;
    virtual void wait() const   = 0;
    virtual bool ready() const  = 0;
};
struct DeviceValue {
    Shape shape;
    DType dtype      = DType::F32;
    uint64_t address = 0;
    std::shared_ptr<DeviceStorage> owner;
    std::shared_ptr<DeviceCompletion> completion;
};
// One host leaf of a transfer batch. `bytes` must outlive the call; the batch is
// synchronous, so no pointer is retained afterwards.
struct HostLeaf {
    Shape shape;
    DType dtype = DType::F32;
    std::span<const std::byte> bytes;
};
// A tree's leaves move together: one stream, one pinned staging region and one
// synchronisation per direction, while every leaf keeps its own device block
// layout. Result order matches input order.
std::vector<std::shared_ptr<DeviceValue>> uploadValues(std::span<const HostLeaf> values);
std::vector<std::vector<std::byte>> downloadValues(std::span<const DeviceValue *const> values);
std::shared_ptr<DeviceValue>
uploadValue(const Shape &shape, DType dtype, std::span<const std::byte> data);
std::vector<std::byte> downloadValue(const DeviceValue &value);
// Reuse storage on the selected device, otherwise transfer through its owning backend.
std::shared_ptr<const DeviceValue> placeOnDevice(std::shared_ptr<const DeviceValue> value);
// Own a native host snapshot for portable execution; no moving-heap references.
std::shared_ptr<DeviceValue>
retainHostValue(const Shape &shape, DType dtype, std::span<const std::byte> data);
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
    core::type::Type *cloneWithParams(std::span<core::type::Type *const> params) const override;
    core::type::Type *cloneWithArguments(
        std::span<const core::type::TypeArgument> arguments, std::string &error) const override;

  private:
    explicit DeviceType(core::type::Type *host);
};
const DeviceType *asDeviceType(const core::type::Type *type);
} // namespace camel::tiles
