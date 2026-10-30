// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The leaves of a parameter tree, as one flat tuple. `tree_map`, `tree_reduce` and `tree_unzip`
// are Camel functions in `stdlib/tree.cml` now; this one cannot be: the result's type is the
// concatenation of the leaves of every field, and Camel has no way to splice a tuple value's
// elements into a tuple being built (a rest argument is only a call argument). The boundary
// comes from the tree's own type (core::type::tree).

#include "camel/core/type/tree.h"
#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"
#include "camel/core/rtdata/tuple.h"
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
} // namespace
node_handle_t Builder::expandTreeCall(
    const oper_group_ptr_t &ops, const std::vector<Argument> &with,
    const std::vector<Argument> &norm, const GCT::node_ptr_t &origin) {
    if (ops->resolvers().size() != 1)
        return nullptr;
    const std::string uri = ops->resolvers().front().first;
    if (uri != "ckpt:tree_leaves")
        return nullptr;
    const auto fail = [&](const std::string &path, const std::string &why) -> void {
        diags_->of(SemanticDiag::ArgumentsRejected)
            .atOrigin(origin->origin())
            .commit(ops->name(), "tree leaf '" + core::type::tree::displayPath(path) + "'", why);
        throw BuildAbortException();
    };
    if (!with.empty() || norm.size() != 1)
        fail("", "invalid number of tree arguments");

    const auto access = [&](node_handle_t parent, size_t index) {
        Type *type = valueTypeOf(nodeTypeOf(parent));
        auto out =
            type->code() == TypeCode::Struct
                ? currGraph_->addAccsNode(fieldType(type, index), fieldName(type, index))
                : currGraph_->addAccsNode(fieldType(type, index), static_cast<uint32_t>(index));
        linkNodes(LinkType::Norm, valueOf(parent), out);
        registerNodeOrigin(context_, out, origin, "gir.tree.access");
        return out;
    };
    const auto pack = [&](const std::vector<node_handle_t> &values) {
        type_vec_t types;
        for (auto value : values)
            types.push_back(valueTypeOf(nodeTypeOf(value)));
        Type *type = TupleType::create(types);
        core::mm::NoCollectionScope guard;
        const slot_t empty =
            core::rtdata::toSlot(::Tuple::create(values.size(), core::mm::autoSpace()));
        auto &draft = currGraph_->transaction().edit();
        auto base   = draft.ref(draft.materializeStaticValue(empty, type));
        std::vector<size_t> slots(values.size());
        std::iota(slots.begin(), slots.end(), 0);
        auto out = currGraph_->addFillNode(
            type, runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
        linkNodes(LinkType::Norm, base, out);
        for (auto value : values)
            linkNodes(LinkType::With, valueOf(value), out);
        registerNodeOrigin(context_, out, origin, "gir.tree.fill");
        return out;
    };
    std::vector<node_handle_t> leaves;
    const auto walk = [&](auto &&self, Type *shape, node_handle_t value) -> void {
        if (!internal(shape)) {
            leaves.push_back(value);
            return;
        }
        for (size_t i = 0; i < sizeOf(shape); ++i) {
            self(self, fieldType(shape, i), access(value, i));
        }
    };
    walk(walk, valueTypeOf(nodeTypeOf(norm.front().node)), valueOf(norm.front().node));
    return pack(leaves);
}
} // namespace camel::compile::gir