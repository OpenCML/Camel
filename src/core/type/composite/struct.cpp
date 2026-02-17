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
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 06, 2024
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "struct.h"
#include "core/mm/alloc/allocator.h"
#include "core/mm/mm.h"
#include "utils/assert.h"
#include "utils/log.h"
#include "utils/type.h"

#include <algorithm>
#include <cstring>
#include <sstream>

using namespace std;

namespace {

StructTypeLayout computeLayout(size_t size, size_t refCount, size_t fieldNamesDataSize) {
    size_t typesSize       = size * sizeof(Type *);
    size_t typeCodesSize   = size * sizeof(TypeCode);
    size_t nameOffsetsSize = size * sizeof(size_t);
    size_t refsSize        = refCount * sizeof(size_t);
    size_t aTypes          = ::alignUp(typesSize, alignof(TypeCode));
    size_t aTypeCodes      = ::alignUp(typeCodesSize, alignof(size_t));
    size_t aNameOffsets    = ::alignUp(nameOffsetsSize, alignof(size_t));
    size_t aRefs           = ::alignUp(refsSize, alignof(char));
    size_t totalSize =
        sizeof(StructType) + aTypes + aTypeCodes + aNameOffsets + aRefs + fieldNamesDataSize;
    return StructTypeLayout{
        .totalSize              = totalSize,
        .size                   = size,
        .refCount               = refCount,
        .alignedTypesSize       = aTypes,
        .alignedTypeCodesSize   = aTypeCodes,
        .alignedNameOffsetsSize = aNameOffsets,
        .alignedRefsSize        = aRefs,
        .fieldNamesDataSize     = fieldNamesDataSize,
    };
}

} // namespace

StructType *StructTypeFactory::build() { return StructType::fromFactory(*this); }

StructType::StructType(
    const StructTypeLayout &layout, const Type *const *types, const TypeCode *typeCodes,
    const size_t *nameOffsets, const char *fieldNamesData, const size_t *refs)
    : CompositeType(TypeCode::Struct), size_(layout.size), refCount_(layout.refCount) {
    auto p             = layout.ptrs(data_);
    typesPtr_          = p.types;
    typeCodesPtr_      = p.typeCodes;
    nameOffsetsPtr_    = p.nameOffsets;
    refsPtr_           = p.refs;
    fieldNamesBasePtr_ = p.fieldNamesBase;
    for (size_t i = 0; i < size_; ++i) {
        p.types[i]       = const_cast<Type *>(types[i]);
        p.typeCodes[i]   = typeCodes[i];
        p.nameOffsets[i] = nameOffsets[i];
    }
    for (size_t i = 0; i < refCount_; ++i) {
        p.refs[i] = refs[i];
    }
    if (layout.fieldNamesDataSize > 0 && fieldNamesData) {
        std::memcpy(p.fieldNamesBase, fieldNamesData, layout.fieldNamesDataSize);
    }
}

StructType *StructType::create() {
    StructTypeLayout layout = computeLayout(0, 0, 0);
    EXEC_WHEN_DEBUG(
        l.in("StructType").debug("Allocating StructType: (), size: {} bytes", layout.totalSize));
    void *mem = mm::permSpace().alloc(layout.totalSize, alignof(StructType));
    ASSERT(mem != nullptr, "Failed to allocate StructType from permSpace");
    return new (mem) StructType(layout, nullptr, nullptr, nullptr, nullptr, nullptr);
}

StructType *StructType::fromFactory(StructTypeFactory &factory) {
    return fromFactoryData(factory.fields_, factory.refs_);
}

StructType *StructType::fromFactoryData(
    const std::vector<std::pair<std::string, Type *>> &fields,
    const std::vector<std::string> &refs) {
    size_t size     = fields.size();
    size_t refCount = refs.size();

    std::vector<Type *> types;
    std::vector<TypeCode> typeCodes;
    std::vector<size_t> nameOffsets;
    std::vector<size_t> refIndices;
    std::string fieldNamesData;

    types.reserve(size);
    typeCodes.reserve(size);
    nameOffsets.reserve(size);

    size_t currentOffset = 0;
    for (size_t i = 0; i < size; ++i) {
        const auto &[name, type] = fields[i];
        types.push_back(type);
        typeCodes.push_back(type->code());
        nameOffsets.push_back(currentOffset);
        fieldNamesData += name;
        fieldNamesData += '\0';
        currentOffset += name.length() + 1;
    }

    for (const auto &refName : refs) {
        auto it = std::find_if(fields.begin(), fields.end(), [&](const auto &p) {
            return p.first == refName;
        });
        ASSERT(it != fields.end(), "Ref name not found in fields");
        refIndices.push_back(std::distance(fields.begin(), it));
    }

    StructTypeLayout layout = computeLayout(size, refCount, fieldNamesData.size());
    EXEC_WHEN_DEBUG(l.in("StructType")
                        .debug(
                            "Allocating StructType: size={}, refCount={}, totalSize: {} bytes",
                            size,
                            refCount,
                            layout.totalSize));
    void *mem = mm::permSpace().alloc(layout.totalSize, alignof(StructType));
    ASSERT(mem != nullptr, "Failed to allocate StructType from permSpace");
    return new (mem) StructType(
        layout,
        types.data(),
        typeCodes.data(),
        nameOffsets.data(),
        fieldNamesData.data(),
        refIndices.data());
}

std::optional<size_t> StructType::findField(std::string_view name) const {
    for (size_t i = 0; i < size_; ++i) {
        if (fieldName(i) == name) {
            return i;
        }
    }
    return std::nullopt;
}

Type *StructType::operator|(const StructType &other) const {
    ASSERT(resolved() && other.resolved(), "StructType::operator| requires resolved operands");

    StructTypeFactory factory;
    // 复制 lhs 的字段
    for (size_t i = 0; i < size_; ++i) {
        factory.add(std::string(fieldName(i)), typeAt(i));
    }
    // 补齐 rhs 独有的字段
    for (size_t i = 0; i < other.size_; ++i) {
        std::string name(other.fieldName(i));
        if (!factory.has(name)) {
            factory.add(name, other.typeAt(i));
        }
    }
    return factory.build();
}

Type *StructType::operator&(const StructType &other) const {
    ASSERT(resolved() && other.resolved(), "StructType::operator& requires resolved operands");

    StructTypeFactory factory;
    for (size_t i = 0; i < size_; ++i) {
        std::string name(fieldName(i));
        auto optIdx = other.findField(name);
        if (optIdx.has_value()) {
            factory.add(name, typeAt(i));
        }
    }
    return factory.build();
}

Type *StructType::resolve(const type_vec_t &typeList) const {
    ASSERT(typeList.size() == refCount_, "StructType::resolve: typeList size mismatch");
    ASSERT(!resolved(), "StructType is already resolved");

    StructTypeFactory factory;
    // 复制所有字段
    for (size_t i = 0; i < size_; ++i) {
        factory.add(std::string(fieldName(i)), typeAt(i));
    }
    // 解析 refs
    const size_t *refIndices = refs();
    for (size_t i = 0; i < refCount_; ++i) {
        size_t refIdx = refIndices[i];
        // 更新对应字段的类型
        // 注意：factory 需要支持更新已存在的字段
        // 这里我们重新构建，因为 factory 不支持更新
        std::string refName(fieldName(refIdx));
        // 实际上我们需要重新构建整个 factory
    }
    // 简化：重新构建整个 struct
    StructTypeFactory newFactory;
    size_t typeListIdx = 0;
    for (size_t i = 0; i < size_; ++i) {
        Type *fieldType = typeAt(i);
        if (fieldType->code() == TypeCode::Ref) {
            if (typeListIdx < typeList.size()) {
                newFactory.add(std::string(fieldName(i)), typeList[typeListIdx++]);
            }
        } else {
            newFactory.add(std::string(fieldName(i)), fieldType);
        }
    }
    return newFactory.build();
}

bool StructType::resolved() const { return refCount_ == 0; }

string StructType::toString() const {
    ostringstream oss;
    oss << "{ ";
    bool first = true;
    for (size_t i = 0; i < size_; ++i) {
        if (!first) {
            oss << ", ";
        }
        first      = false;
        Type *type = typeAt(i);
        oss << fieldName(i) << ": " << (type ? type->toString() : "<null>");
    }
    oss << " }";
    return oss.str();
}

string StructType::mangle() const {
    ostringstream oss;
    oss << "S" << size_;
    for (size_t i = 0; i < size_; ++i) {
        std::string name(fieldName(i));
        Type *type = typeAt(i);
        oss << name.length() << name;
        oss << (type ? type->mangle() : "N");
    }
    return oss.str();
}

Type *StructType::clone(bool deep /* = false */) const {
    StructTypeFactory factory;
    for (size_t i = 0; i < size_; ++i) {
        Type *fieldType = typeAt(i);
        factory.add(
            std::string(fieldName(i)),
            deep && fieldType ? fieldType->clone(true) : fieldType);
    }
    return factory.build();
}

bool StructType::equals(Type *type) const {
    if (!type || type->code() != TypeCode::Struct) {
        return false;
    }
    auto other = static_cast<const StructType *>(type);
    if (size_ != other->size_) {
        return false;
    }
    for (size_t i = 0; i < size_; ++i) {
        std::string name(fieldName(i));
        auto optIdx = other->findField(name);
        if (!optIdx.has_value()) {
            return false;
        }
        Type *lhsType = typeAt(i);
        Type *rhsType = other->typeAt(optIdx.value());
        if (!lhsType || !rhsType || !lhsType->equals(rhsType)) {
            return false;
        }
    }
    return true;
}

CastSafety StructType::castSafetyTo(const Type &) const { return CastSafety::Unsafe; }

bool StructType::assignable(Type *type) const { return equals(type); }
