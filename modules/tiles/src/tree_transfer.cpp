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
        if (leaf->code() == TypeCode::Float64)
            return DeviceType::get(leaf);
        const auto *tensor = tensor::asTensorType(leaf);
        return tensor && (!tensor->dtype() || *tensor->dtype() == TypeCode::Float32)
                   ? DeviceType::get(leaf)
                   : nullptr;
    });
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
            } else if (type->code() == TypeCode::Float64) {
                out.dtype = DType::F64;
                out.bytes.resize(sizeof(double));
                const double scalar = td::fromSlot<double>(value);
                std::memcpy(out.bytes.data(), &scalar, sizeof(scalar));
            } else {
                auto *tensor = td::fromSlot<tensor::Array *>(value);
                if (tensor->dtype() != TypeCode::Float32)
                    throw std::runtime_error("upload currently requires f32 tensor leaves");
                out.dtype = DType::F32;
                out.shape = tensor->shapeVector();
                out.bytes.resize(elements(out.shape) * sizeof(float));
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
        if (type->code() == TypeCode::Float64) {
            if (leaf.dtype != DType::F64 || leaf.bytes.size() != sizeof(double))
                throw std::runtime_error("download scalar type mismatch");
            double value;
            std::memcpy(&value, leaf.bytes.data(), sizeof(value));
            return td::toSlot(value);
        }
        if (leaf.dtype != DType::F32 || leaf.bytes.size() != elements(leaf.shape) * sizeof(float))
            throw std::runtime_error("download tensor type mismatch");
        auto *out =
            tensor::createTensor(TypeCode::Float32, leaf.shape, core::mm::autoSpace());
        if (!leaf.bytes.empty())
            std::memcpy(out->rawData(), leaf.bytes.data(), leaf.bytes.size());
        return td::toSlot(out);
    };
    return rebuild(outputType, materialize);
}
} // namespace camel::tiles
