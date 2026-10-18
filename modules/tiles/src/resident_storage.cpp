/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Backend-independent resident reads and immutable native host snapshots.
#include "tiles/device.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace camel::tiles {
namespace {
class HostStorage final : public DeviceStorage {
    std::vector<std::byte> data_;

  public:
    explicit HostStorage(std::span<const std::byte> data) : data_(data.begin(), data.end()) {}
    uint64_t address() const { return reinterpret_cast<uint64_t>(this); }
    std::vector<std::byte> read(uint64_t address, size_t bytes) const override {
        if (address != this->address() || bytes != data_.size())
            throw std::runtime_error("resident host storage metadata mismatch");
        return data_;
    }
};
} // namespace
std::shared_ptr<DeviceValue>
retainHostValue(const Shape &shape, DType dtype, std::span<const std::byte> data) {
    if (bytes(Node{.shape = shape, .dtype = dtype}) != data.size() ||
        (dtype == DType::F64 && !shape.empty()))
        throw std::runtime_error("resident host storage shape mismatch");
    auto storage   = std::make_shared<HostStorage>(data);
    auto value     = std::make_shared<DeviceValue>();
    value->shape   = shape;
    value->dtype   = dtype;
    value->address = storage->address();
    value->owner   = std::move(storage);
    return value;
}
std::vector<std::byte> downloadValue(const DeviceValue &value) {
    if (value.completion)
        value.completion->wait();
    if (!value.owner || !value.address)
        throw std::runtime_error("invalid resident tensor");
    return value.owner->read(
        value.address,
        bytes(Node{.shape = value.shape, .dtype = value.dtype}));
}
} // namespace camel::tiles
