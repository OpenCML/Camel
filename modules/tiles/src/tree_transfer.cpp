// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Snapshot all leaves before CUDA calls; no moving-heap pointers cross that boundary.

#include "tiles/internal/tree_transfer.h"
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/tree.h"
#include "modules/tensor/tensor.h"
#include "modules/tensor/type.h"
#include "tiles/device.h"
#include <cstring>

namespace camel::tiles {
using namespace core::type;
namespace td = core::rtdata;
Type *transferTreeType(Type *type, bool upload) {
    return core::type::tree::mapLeaves(type, [&](const std::string &, Type *leaf) -> Type * {
        if (!upload) {
            const auto *device = asDeviceType(leaf);
            return device ? device->hostType() : nullptr;
        }
        if (deviceScalarDType(leaf))
            return DeviceType::get(leaf);
        const tensor::TensorFacts facts = tensor::tensorFactsOf(leaf);
        // A tensor leaf a device can hold is one whose elements it has a width for: f32 and, since
        // a position is an index, i64.
        const bool carried =
            !facts.dtype || *facts.dtype == TypeCode::Float32 || *facts.dtype == TypeCode::Int64;
        return tensor::isTensorType(leaf) && carried ? DeviceType::get(leaf) : nullptr;
    });
}
slot_t hostLeafValue(Type *type, const Shape &shape, DType dtype, std::span<const std::byte> data) {
    // The readback of one leaf: what a download returned becomes the host value of the element type
    // the leaf says it holds. The leaf is read by the dtype it carries, not by the type the caller
    // expected, so the two are compared instead of one being assumed.
    if (const auto scalar = deviceScalarDType(type)) {
        if (dtype != *scalar)
            throw std::runtime_error("download scalar type mismatch");
        if (*scalar == DType::F32) {
            if (data.size() != sizeof(float))
                throw std::runtime_error("download scalar type mismatch");
            float value32;
            std::memcpy(&value32, data.data(), sizeof(value32));
            return td::toSlot(value32);
        }
        if (data.size() != sizeof(double))
            throw std::runtime_error("download scalar type mismatch");
        double value64;
        std::memcpy(&value64, data.data(), sizeof(value64));
        return td::toSlot(value64);
    }
    // A tensor leaf answers in its own element type (design principles 5.1): f32 and, since a
    // position is an index, i64. A wider float leaf is not something this device produces, so it is
    // refused here rather than read at the wrong width.
    if (dtype != DType::F32 && dtype != DType::I64)
        throw std::runtime_error("download tensor type mismatch");
    const size_t width = dtype == DType::I64 ? sizeof(int64_t) : sizeof(float);
    if (data.size() != elements(shape) * width)
        throw std::runtime_error("download tensor type mismatch");
    auto *out = tensor::createTensor(
        dtype == DType::I64 ? TypeCode::Int64 : TypeCode::Float32,
        shape,
        core::mm::autoSpace());
    if (!data.empty())
        std::memcpy(out->rawData(), data.data(), data.size());
    return td::toSlot(out);
}
namespace {
struct Leaf {
    Shape shape;
    DType dtype;
    std::vector<std::byte> bytes;
    std::shared_ptr<DeviceValue> device;
};
template <class F> slot_t walk(Type *type, slot_t value, F &leaf) {
    if (type->code() == TypeCode::Tuple) {
        auto *tuple = static_cast<TupleType *>(type);
        auto *input = td::fromSlot<::Tuple *>(value);
        for (size_t i = 0; i < tuple->size(); ++i)
            walk(tuple->typeAt(i), input->get<slot_t>(i), leaf);
    } else if (type->code() == TypeCode::Struct) {
        auto *record = static_cast<StructType *>(type);
        auto *input  = td::fromSlot<::Struct *>(value);
        for (size_t i = 0; i < record->size(); ++i)
            walk(record->typeAt(i), input->get<slot_t>(i), leaf);
    } else {
        leaf(type, value);
    }
    return value;
}
template <class F> slot_t rebuild(Type *type, F &leaf) {
    if (type->code() == TypeCode::Tuple) {
        auto *tuple = static_cast<TupleType *>(type);
        auto *out   = ::Tuple::create(tuple->size(), core::mm::autoSpace());
        for (size_t i = 0; i < tuple->size(); ++i)
            out->set(i, rebuild(tuple->typeAt(i), leaf), tuple);
        return td::toSlot(out);
    }
    if (type->code() == TypeCode::Struct) {
        auto *record = static_cast<StructType *>(type);
        auto *out    = ::Struct::create(record->size(), core::mm::autoSpace());
        for (size_t i = 0; i < record->size(); ++i)
            out->set(i, rebuild(record->typeAt(i), leaf), record);
        return td::toSlot(out);
    }
    return leaf(type);
}
} // namespace
slot_t transferTree(ArgsView &norm, bool upload) {
    auto *outputType = transferTreeType(norm.type(0), upload);
    if (!outputType)
        throw std::runtime_error("unsupported transfer tree type");
    std::vector<Leaf> leaves;
    {
        core::mm::NoCollectionScope guard;
        auto snapshot = [&](Type *type, slot_t value) {
            Leaf out{};
            if (!upload) {
                auto *wrapper = td::fromSlot<td::ForeignResourceObject *>(value);
                out.device    = *wrapper->resourceAs<std::shared_ptr<DeviceValue>>();
                out.shape     = out.device->shape;
                out.dtype     = out.device->dtype;
            } else if (const auto scalar = deviceScalarDType(type)) {
                // A scalar leaf uploads as the rank-0 value of its element type: 4 bytes for f32,
                // 8 for float (design principles 7.3, the same value the tensor path carries).
                out.dtype = *scalar;
                out.bytes.resize(*scalar == DType::F32 ? sizeof(float) : sizeof(double));
                if (*scalar == DType::F32) {
                    const float value32 = td::fromSlot<float>(value);
                    std::memcpy(out.bytes.data(), &value32, sizeof(value32));
                } else {
                    const double value64 = td::fromSlot<double>(value);
                    std::memcpy(out.bytes.data(), &value64, sizeof(value64));
                }
            } else {
                // A tensor leaf uploads as its own element type; a width the device has no dtype
                // for is refused here rather than carried as the wrong number of bytes.
                auto *tensor = td::fromSlot<tensor::Array *>(value);
                if (tensor->dtype() != TypeCode::Float32 && tensor->dtype() != TypeCode::Int64)
                    throw std::runtime_error("upload currently requires f32 or i64 tensor leaves");
                out.dtype = tensor->dtype() == TypeCode::Float32 ? DType::F32 : DType::I64;
                out.shape = tensor->shapeVector();
                out.bytes.resize(bytes(Node{.shape = out.shape, .dtype = out.dtype}));
                if (!out.bytes.empty())
                    std::memcpy(out.bytes.data(), tensor->rawData(), out.bytes.size());
            }
            leaves.push_back(std::move(out));
        };
        walk(norm.type(0), norm.slot(0), snapshot);
    }
    // The forest moves as one batch: one stream, one pinned staging region and
    // one synchronisation per direction. Each leaf keeps its own device block,
    // and result order is the walk order used by `materialize` below.
    if (upload) {
        std::vector<HostLeaf> batch;
        batch.reserve(leaves.size());
        for (const auto &leaf : leaves)
            batch.push_back(HostLeaf{leaf.shape, leaf.dtype, leaf.bytes});
        auto placed = uploadValues(batch);
        if (placed.size() != leaves.size())
            throw std::runtime_error("device upload returned the wrong leaf count");
        for (size_t i = 0; i < leaves.size(); ++i) {
            leaves[i].device = std::move(placed[i]);
            leaves[i].bytes.clear();
        }
    } else {
        std::vector<const DeviceValue *> batch;
        batch.reserve(leaves.size());
        for (const auto &leaf : leaves)
            batch.push_back(leaf.device.get());
        auto downloaded = downloadValues(batch);
        if (downloaded.size() != leaves.size())
            throw std::runtime_error("device download returned the wrong leaf count");
        for (size_t i = 0; i < leaves.size(); ++i)
            leaves[i].bytes = std::move(downloaded[i]);
    }
    core::mm::NoCollectionScope guard;
    size_t index     = 0;
    auto materialize = [&](Type *type) -> slot_t {
        auto &leaf = leaves.at(index++);
        if (upload) {
            auto owner = std::make_unique<std::shared_ptr<DeviceValue>>(std::move(leaf.device));
            td::ForeignResourceDescriptor descriptor;
            descriptor.kind    = "tiles.Device";
            descriptor.deleter = [](void *p) {
                delete static_cast<std::shared_ptr<DeviceValue> *>(p);
            };
            auto *wrapper = td::ForeignResourceObject::create(
                core::mm::autoSpace(),
                std::move(descriptor),
                owner.get());
            owner.release();
            return td::toSlot(wrapper);
        }
        // The readback of one leaf, in the same function a resident leaf reaches without a device.
        return hostLeafValue(type, leaf.shape, leaf.dtype, leaf.bytes);
    };
    return rebuild(outputType, materialize);
}
} // namespace camel::tiles
