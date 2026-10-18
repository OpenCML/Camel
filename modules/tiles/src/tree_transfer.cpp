/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Snapshot all leaves before CUDA calls; no moving-heap pointers cross that boundary.
#include "tiles/internal/tree_transfer.h"
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "modules/tensor/tensor.h"
#include "modules/tensor/type.h"
#include "tiles/device.h"
#include <cstring>

namespace camel::tiles {
using namespace core::type;
namespace td = core::rtdata;
Type *transferTreeType(Type *type, bool upload) {
    if (type->code() == TypeCode::Tuple) {
        auto *tuple = static_cast<TupleType *>(type);
        if (tuple->isTagged() || tuple->isCell())
            return nullptr;
        type_vec_t fields;
        for (size_t i = 0; i < tuple->size(); ++i) {
            auto *field = transferTreeType(tuple->typeAt(i), upload);
            if (!field)
                return nullptr;
            fields.push_back(field);
        }
        return TupleType::create(fields);
    }
    if (type->code() == TypeCode::Struct) {
        auto *record = static_cast<StructType *>(type);
        StructTypeFactory fields;
        for (size_t i = 0; i < record->size(); ++i) {
            auto *field = transferTreeType(record->typeAt(i), upload);
            if (!field)
                return nullptr;
            fields.add(std::string(record->fieldName(i)), field);
        }
        return fields.build();
    }
    if (!upload) {
        const auto *device = asDeviceType(type);
        return device ? device->hostType() : nullptr;
    }
    if (type->code() == TypeCode::Float64)
        return DeviceType::get(type);
    const auto *tensor = tensor::asTensorType(type);
    return tensor && (!tensor->dtype() || *tensor->dtype() == TypeCode::Float32)
               ? DeviceType::get(type)
               : nullptr;
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
                auto *tensor = td::fromSlot<tensor::TensorObject *>(value);
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
    for (auto &leaf : leaves) {
        if (upload) {
            leaf.device = uploadValue(leaf.shape, leaf.dtype, leaf.bytes);
            leaf.bytes.clear();
        } else {
            leaf.bytes = downloadValue(*leaf.device);
        }
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
            tensor::TensorObject::create(TypeCode::Float32, leaf.shape, core::mm::autoSpace());
        if (!leaf.bytes.empty())
            std::memcpy(out->rawData(), leaf.bytes.data(), leaf.bytes.size());
        return td::toSlot(out);
    };
    return rebuild(outputType, materialize);
}
} // namespace camel::tiles
