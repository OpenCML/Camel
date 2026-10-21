/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Tree boundaries come only from the first/reference type, never from callbacks.
#include "camel/core/type/tree.h"
#include "builder.h"
#include "builder_support.h"
#include "camel/core/data/primary.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include <numeric>

namespace camel::compile::gir {
using namespace core::type;
using namespace core::data;
using namespace core::error;
namespace {
// What a tree and a leaf are, and how a path is written, is core::type::tree (one definition
// for the compiler, the checkpoints and the modules).
namespace tr = core::type::tree;
bool internal(Type *type) { return tr::isInternal(type); }
size_t sizeOf(Type *type) { return tr::arity(type); }
Type *fieldType(Type *type, size_t i) { return tr::childType(type, i); }
std::string fieldName(Type *type, size_t i) { return tr::childName(type, i); }
std::string childPath(const std::string &path, Type *type, size_t i) {
    return tr::childPath(path, type, i);
}
} // namespace
node_handle_t Builder::expandTreeCall(
    const oper_group_ptr_t &ops, const std::vector<Argument> &with,
    const std::vector<Argument> &norm, const GCT::node_ptr_t &origin) {
    if (ops->resolvers().size() != 1)
        return nullptr;
    const std::string uri = ops->resolvers().front().first;
    if (uri != "ckpt:tree_map" && uri != "ckpt:tree_reduce" && uri != "ckpt:tree_unzip" &&
        uri != "ckpt:tree_paths" && uri != "ckpt:tree_leaves")
        return nullptr;
    const auto fail = [&](const std::string &path, const std::string &why) -> void {
        diags_->of(SemanticDiag::ArgumentsRejected)
            .atOrigin(origin->origin())
            .commit(ops->name(), "tree leaf '" + core::type::tree::displayPath(path) + "'", why);
        throw BuildAbortException();
    };
    const bool map = uri == "ckpt:tree_map", reduce = uri == "ckpt:tree_reduce";
    const bool unzip = uri == "ckpt:tree_unzip", paths = uri == "ckpt:tree_paths";
    if ((map && (with.size() != 1 || norm.empty())) ||
        (reduce && (with.size() != 1 || norm.size() != 2)) ||
        (unzip && (!with.empty() || norm.size() != 2)) ||
        (!map && !reduce && !unzip && (!with.empty() || norm.size() != 1)))
        fail("", "invalid number of tree arguments");

    const auto access = [&](node_handle_t parent, size_t index) {
        Type *type = valueTypeOf(nodeTypeOf(parent));
        auto out =
            type->code() == TypeCode::Struct
                ? currGraph_->addAccsNode(fieldType(type, index), fieldName(type, index))
                : currGraph_->addAccsNode(fieldType(type, index), static_cast<uint32_t>(index));
        linkNodes(LinkType::Norm, valueOf(parent), out);
        registerTreeOrigin(out, origin, "gir.tree.access");
        return out;
    };
    const auto pack = [&](Type *shape, const std::vector<node_handle_t> &values) {
        type_vec_t types;
        for (auto value : values)
            types.push_back(valueTypeOf(nodeTypeOf(value)));
        Type *type;
        if (shape && shape->code() == TypeCode::Struct) {
            StructTypeFactory fields;
            for (size_t i = 0; i < values.size(); ++i)
                fields.add(fieldName(shape, i), types[i]);
            type = fields.build();
        } else {
            type = TupleType::create(types);
        }
        core::mm::NoCollectionScope guard;
        const bool tuple = type->code() == TypeCode::Tuple;
        const slot_t empty =
            tuple ? core::rtdata::toSlot(::Tuple::create(values.size(), core::mm::autoSpace()))
                  : core::rtdata::toSlot(::Struct::create(values.size(), core::mm::autoSpace()));
        auto &draft = currGraph_->draft();
        auto base  = draft.ref(draft.materializeStaticValue(empty, type));
        std::vector<size_t> slots(values.size());
        std::iota(slots.begin(), slots.end(), 0);
        auto out = currGraph_->addFillNode(
            type,
            runtime::makeFillPayload(
                tuple ? runtime::GCFillKind::Tuple : runtime::GCFillKind::Struct,
                slots));
        linkNodes(LinkType::Norm, base, out);
        for (auto value : values)
            linkNodes(LinkType::With, valueOf(value), out);
        registerTreeOrigin(out, origin, "gir.tree.fill");
        return out;
    };
    const auto compatible = [&](Type *shape, Type *other, const std::string &path) {
        if (!internal(other) || shape->code() != other->code() || sizeOf(shape) != sizeOf(other))
            fail(
                path,
                "tree structure differs: expected " + shape->toString() + ", got " +
                    other->toString());
        if (shape->code() == TypeCode::Struct)
            for (size_t i = 0; i < sizeOf(shape); ++i)
                if (fieldName(shape, i) != fieldName(other, i))
                    fail(childPath(path, shape, i), "tree field names or declaration order differ");
    };
    // A temporary names the original function value exactly once. Generic/overload
    // DREFs remain names and the normal call builder resolves them for each leaf.
    std::string callback;
    if (map || reduce)
        callback = bindTemporary(with.front().node);
    const auto invoke = [&](const std::vector<node_handle_t> &args, const std::string &path) {
        auto call = tree_->makeLink(args.size());
        call->setOrigin(origin->origin());
        auto callee = tree_->makeDRef(callback);
        callee->setOrigin(origin->origin());
        *call << callee;
        for (auto arg : args) {
            auto ref = tree_->makeDRef(bindTemporary(valueOf(arg)));
            ref->setOrigin(origin->origin());
            *call << ref;
        }
        try {
            return visitLinkNode(call);
        } catch (const BuildAbortException &) {
            std::string types;
            for (auto arg : args)
                types += (types.empty() ? "" : ", ") + valueTypeOf(nodeTypeOf(arg))->toString();
            fail(path, "callback is not applicable to (" + types + ")");
        }
        return node_handle_t{};
    };
    node_handle_t accumulator = reduce ? valueOf(norm[1].node) : nullptr;
    Type *accumulatorType     = reduce ? valueTypeOf(nodeTypeOf(accumulator)) : nullptr;
    std::vector<node_handle_t> leaves;
    std::vector<std::vector<node_handle_t>> unzipLeaves;
    size_t arity    = 0;
    const auto walk = [&](auto &&self,
                          Type *shape,
                          const std::vector<node_handle_t> &values,
                          const std::string &path) -> node_handle_t {
        if (internal(shape)) {
            for (auto value : values)
                compatible(shape, valueTypeOf(nodeTypeOf(value)), path);
            std::vector<node_handle_t> children;
            for (size_t i = 0; i < sizeOf(shape); ++i) {
                std::vector<node_handle_t> inputs;
                for (auto value : values)
                    inputs.push_back(access(value, i));
                children.push_back(
                    self(self, fieldType(shape, i), inputs, childPath(path, shape, i)));
            }
            return map || paths ? pack(shape, children) : nullptr;
        }
        if (map)
            return invoke(values, path);
        if (reduce) {
            accumulator = invoke({accumulator, values.front()}, path);
            if (!accumulatorType->assignableFrom(valueTypeOf(nodeTypeOf(accumulator))))
                fail(
                    path,
                    "reducer must return its accumulator type " + accumulatorType->toString());
        } else if (paths) {
            auto out = currGraph_->addStaticDataNode(std::make_shared<StringData>(path));
            registerTreeOrigin(out, origin, "gir.tree.path");
            return out;
        } else if (unzip) {
            Type *type = valueTypeOf(nodeTypeOf(values.front()));
            if (!internal(type) || type->code() != TypeCode::Tuple)
                fail(path, "unzip expects a tuple at every reference leaf");
            if (unzipLeaves.empty())
                arity = sizeOf(type);
            else if (arity != sizeOf(type))
                fail(path, "unzip leaf tuples have different arities");
            std::vector<node_handle_t> fields;
            for (size_t i = 0; i < arity; ++i)
                fields.push_back(access(values.front(), i));
            unzipLeaves.push_back(std::move(fields));
        } else {
            leaves.push_back(values.front());
        }
        return nullptr;
    };
    Type *shape = valueTypeOf(nodeTypeOf(norm[unzip ? 1 : 0].node));
    std::vector<node_handle_t> inputs;
    if (map)
        for (const auto &arg : norm)
            inputs.push_back(valueOf(arg.node));
    else
        inputs.push_back(valueOf(norm.front().node));
    auto result = walk(walk, shape, inputs, "");
    if (map || paths)
        return result;
    if (reduce)
        return accumulator;
    if (!unzip)
        return pack(nullptr, leaves);
    if (unzipLeaves.empty())
        fail("", "unzip tuple arity cannot be inferred from an empty reference tree");
    std::vector<node_handle_t> trees;
    for (size_t component = 0; component < arity; ++component) {
        size_t index       = 0;
        const auto rebuild = [&](auto &&self, Type *type) -> node_handle_t {
            if (!internal(type))
                return unzipLeaves.at(index++).at(component);
            std::vector<node_handle_t> fields;
            for (size_t i = 0; i < sizeOf(type); ++i)
                fields.push_back(self(self, fieldType(type, i)));
            return pack(type, fields);
        };
        trees.push_back(rebuild(rebuild, shape));
    }
    return pack(nullptr, trees);
}
} // namespace camel::compile::gir
