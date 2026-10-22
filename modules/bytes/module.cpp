/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Explicit binary/text boundaries. Invalid UTF-8 is a value-level decoding error.
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm/block.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/module/builtin.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/array.h"
#include "camel/execute/executor.h"
#include <cstring>
#include <limits>

namespace {
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using namespace camel::core::error;
namespace mm = camel::core::mm;
slot_t encodeKernel(ArgsView &, ArgsView &norm, Context &) {
    // A string is always valid UTF-8 (design principles 11.1), so encoding cannot fail.
    auto *text          = norm.get<::String *>(0);
    const std::string_view textView = text->view();
    if (textView.size() < mm::kInlineBlockBytes) {
        return toSlot(BytesObject::from(textView, mm::autoSpace()));
    }
    // Large text: copy it once, straight from the language string into a data block. The
    // string may move when the block allocation collects, so root it and re-read after.
    mm::RootHandle root(mm::autoSpace(), text, Type::String(), "bytes.encode");
    mm::Block *block = mm::Block::create(textView.size());
    const std::string_view current = root.getAs<::String>()->view();
    std::memcpy(block->data(), current.data(), current.size());
    block->setBytes(current.size());
    return toSlot(BytesObject::adopt(block, current.size(), mm::autoSpace()));
}
slot_t decodeKernel(ArgsView &, ArgsView &norm, Context &) {
    const std::string bytes(norm.get<BytesObject *>(0)->view());
    if (const auto bad = invalidUtf8Offset(bytes))
        return makeError(
            "EncodingError",
            "invalid UTF-8 at byte " + std::to_string(*bad),
            "<bytes::decode>");
    if (bytes.size() > std::numeric_limits<uint32_t>::max())
        return makeError(
            "EncodingError",
            "decoded text exceeds the string size limit",
            "<bytes::decode>");
    return makeOk(
        toSlot(::String::from(bytes, mm::autoSpace())),
        TupleType::fallible(Type::String()));
}
slot_t fromArrayKernel(ArgsView &, ArgsView &norm, Context &) {
    auto *array = norm.get<::Array *>(0);
    std::string bytes(array->size(), '\0');
    for (size_t i = 0; i < array->size(); ++i) {
        const auto value = array->get<int64_t>(i);
        if (value < 0 || value > 255)
            throwRuntimeFault(
                RuntimeDiag::InvalidArgument,
                "bytes.from_array",
                "element " + std::to_string(i) + " is outside 0..255");
        bytes[i] = static_cast<char>(value);
    }
    return toSlot(BytesObject::from(bytes, mm::autoSpace()));
}
slot_t toArrayKernel(ArgsView &, ArgsView &norm, Context &) {
    const std::string bytes(norm.get<BytesObject *>(0)->view());
    auto *out = ::Array::create(
        mm::autoSpace(), ArrayType::create(Type::Int64()), bytes.size());
    for (size_t i = 0; i < bytes.size(); ++i)
        out->set<int64_t>(i, static_cast<unsigned char>(bytes[i]));
    return toSlot(out);
}
slot_t sizeKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto size = norm.get<BytesObject *>(0)->size();
    if (size > INT64_MAX)
        throwRuntimeFault(RuntimeDiag::InvalidArgument, "bytes.size", "length exceeds int range");
    return toSlot(static_cast<int64_t>(size));
}
slot_t concatKernel(ArgsView &, ArgsView &norm, Context &) {
    std::string bytes(norm.get<BytesObject *>(0)->view());
    bytes.append(norm.get<BytesObject *>(1)->view());
    return toSlot(BytesObject::from(bytes, mm::autoSpace()));
}
class BytesExecutor final : public Executor {
  public:
    explicit BytesExecutor(context_ptr_t ctx)
        : Executor(
              std::move(ctx), {{"encode", encodeKernel},
                               {"decode", decodeKernel},
                               {"from_array", fromArrayKernel},
                               {"to_array", toArrayKernel},
                               {"size", sizeKernel},
                               {"concat", concatKernel}}) {}
};
class BytesModule final : public BuiltinModule {
  public:
    explicit BytesModule(context_ptr_t ctx) : BuiltinModule("bytes", std::move(ctx)) {
        // The signatures of the operators are written once, in the declaration module
        // `stdlib/bytes.cml` (design principles 6.4; handover H19, step 1). What is left here is
        // what only C++ knows: the kernels below, the executor that runs them, and the operator
        // traits declared when the module loads.
    }
    bool load() override {
        if (loaded_)
            return true;
        context_->registerExecutorFactory("bytes", [ctx = context_]() {
            return std::make_shared<BytesExecutor>(ctx);
        });
        camel::core::OperatorTable::instance().declare(
            "bytes",
            {{"encode", true},
             {"decode", true},
             {"from_array", true},
             {"to_array", true},
             {"size", true},
             {"concat", true}});
        loaded_ = true;
        return true;
    }
};
} // namespace
extern "C" Module *camel_module_create(Context *ctx) {
    return new BytesModule(ctx->shared_from_this());
}
