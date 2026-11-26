// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The state module: an explicit mutable reference, as a handle the core provides.
 *
 * A reference is one box a handle points at (design principles 3.3, "handle types"). `ref` makes a
 * box and answers a handle to it; `get` reads the content; `set` writes it; `update` writes `f` of
 * it, for a `pure` `f`. The four are operators of this module, so what each touches is declared
 * where the operator contract declares it
 * (`OperatorTable::declareResources`, principle 10.5) and not by a body the scheduler cannot see:
 *
 *   ref                     allocates one handle (`state::Ref`), result fresh
 *   get(r)                  reads the resource the handle value names
 *   set(r, v)               read-modify-write of it (the handle itself is not replaced: the box is)
 *   update(r, f)            read-modify-write of it
 *
 * One resource per value follows from those two declarations together: a handle names its
 * resource, and a `ref` call's result is fresh, so two references made in one body are two
 * resources and no order edge joins the operations on one to the operations on the other. Two
 * operations on one reference keep the order they are written in, because they reach one value.
 *
 * The storage is the counted box of ref.h. The handle is a value, so a copy of it retains the box;
 * the box goes when the last handle does. Nothing in this module frees a box by hand.
 */

#include "camel/core/operator_table.h"
#include "ref.h"

#include "camel/core/context/context.h"
#include "camel/core/module/builtin.h"
#include "camel/core/operator_traits.h"
#include "camel/core/resource_semantics.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/native.h"
#include "camel/core/type/ref.h"
#include "camel/execute/executor.h"
#include "camel/execute/function_call.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace camel::core::context;
using namespace camel::core::module;
using camel::core::OperatorTable;
using camel::core::rtdata::ForeignResourceDescriptor;
using camel::core::rtdata::ForeignResourceObject;
using camel::statemod::RefBox;

namespace camel::statemod {

namespace {

namespace mm  = camel::core::mm;
namespace err = camel::core::error;

ForeignResourceDescriptor refDescriptor() {
    ForeignResourceDescriptor descriptor;
    descriptor.kind = "state.ref";
    // This module owns the type, so this module is the one that says it: a count reclaims the box
    // (rtdata/foreign.h, `ForeignResourceDescriptor::counted`). The kind above is the type's name
    // for diagnostics, and nothing in the count compares it.
    descriptor.counted = true;
    descriptor.deleter = [](void *resource) { delete static_cast<RefBox *>(resource); };
    // The content is not traced through the wrapper: a traced content is held by the box's own
    // root (ref.cpp), so the collector neither reads nor rewrites anything here.
    descriptor.containsCamelReferences = false;
    return descriptor;
}

/// The box a handle points at. The handle is the collector's object; the box is the resource the
/// control block counts (rtdata/foreign.h), so the last handle to go frees it.
RefBox &boxOf(ArgsView &norm, size_t index, const char *who) {
    auto *handle = norm.get<ForeignResourceObject *>(index);
    if (handle == nullptr || handle->disposed() || handle->kind() != "state.ref") {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            who,
            "the value is not a reference");
    }
    return *static_cast<RefBox *>(handle->resource());
}

/// `ref(v)`: a new box holding `v`, and a handle to it. The result is fresh, which is what makes
/// two references two resources (resources.h, `ResultAlias`).
slot_t refKernel(ArgsView &, ArgsView &norm, Context &) {
    auto *box = new RefBox(norm.type(0), norm.slot(0));
    return rtdata::toSlot(ForeignResourceObject::create(mm::autoSpace(), refDescriptor(), box));
}

/// `get(r)`: the content of the box `r` points at.
slot_t getKernel(ArgsView &, ArgsView &norm, Context &) {
    return boxOf(norm, 0, "state.get").get();
}

/// `set(r, v)`: write `v` into the box, and answer `v`.
slot_t setKernel(ArgsView &, ArgsView &norm, Context &) {
    const slot_t value = norm.slot(1);
    boxOf(norm, 0, "state.set").set(value);
    return value;
}

/// `update(r, f)`: write `f` of the content into the box, and answer the new content. `f` is
/// declared `pure`, so running it here touches no resource but the one being written.
slot_t updateKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    RefBox &box    = boxOf(norm, 0, "state.update");
    auto *function = norm.get<::Function *>(1);
    if (function == nullptr) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "state.update",
            "the update is not a function");
    }
    // The call runs under the box's own root: nothing here can be collected while `f` runs, so the
    // value read out and the value written back are the same objects before and after it.
    mm::NoCollectionScope noCollection;
    const slot_t current      = box.get();
    const slot_t arguments[1] = {current};
    const slot_t next         = camel::execute::invokeFunction(
        ctx,
        function,
        std::span<const slot_t>(),
        std::span<const slot_t>(arguments, 1));
    box.set(next);
    return next;
}

class StateExecutor final : public Executor {
  public:
    explicit StateExecutor(context_ptr_t ctx) : Executor(std::move(ctx), kernels()) {}

  private:
    static std::unordered_map<std::string, operator_t> kernels() {
        return {
            {"ref", refKernel},
            {"get", getKernel},
            {"set", setKernel},
            {"update", updateKernel}};
    }
};

class StateModule final : public BuiltinModule {
  public:
    explicit StateModule(context_ptr_t ctx) : BuiltinModule("state", std::move(ctx)) {
        // The type of the module is the module's (design principles 10.4): `Ref<T>` is written in
        // stdlib/state.cml without a body, and what it is -- a handle, one resource per value --
        // is the core's type (camel/core/type/ref.h). The module declares it as its own, so
        // `uses state::Ref` names it and the compiler's rules about a reference are rules about
        // this type wherever it is written.
        camel::core::type::registerNativeType("state", "Ref", camel::core::type::RefType::get());
    }

    bool load() override {
        if (loaded_) {
            return true;
        }
        context_->registerExecutorFactory("state", [ctx = context_]() {
            return std::make_shared<StateExecutor>(ctx);
        });
        auto &table = OperatorTable::instance();
        // None of the four is pure: a reference is a resource, and reading or writing one is an
        // effect on it (design principles 3.3). `update` runs the function value it is handed, so
        // its traits say it calls graphs.
        table.declare("state", {{"ref", false}, {"get", false}, {"set", false}, {"update", false}});
        table.set("state:update", {.pure = false, .callsGraphs = true});
        // What each touches, in the operator contract (principle 10.5): the handle the operand
        // names. `ref` allocates one, and its result is fresh -- a new box is a new resource.
        {
            using camel::core::sem::AccessMode;
            using camel::core::sem::operandAccess;
            using camel::core::sem::ResourceClass;
            using camel::core::sem::ResourceSpec;
            const std::string name = "state::Ref";

            ResourceSpec makes;
            makes.allocates   = camel::core::sem::Resource{ResourceClass::Handle, "state::Ref"};
            makes.result      = camel::core::sem::ResultAlias{};
            makes.result.kind = camel::core::sem::ResultAlias::Kind::Fresh;

            ResourceSpec reads;
            reads.accesses = {operandAccess(ResourceClass::Handle, 0, AccessMode::Read, name)};
            // What `get` answers is what the box holds, so it may be the object the reference
            // reaches: it is not a fresh value nothing else refers to.
            reads.result = camel::core::sem::ResultAlias{
                .kind    = camel::core::sem::ResultAlias::Kind::MayAlias,
                .operand = 0};

            // `set` answers the value it was handed, and `update` answers whatever `f` made of the
            // content: the first is its own operand, the second nothing this can name.
            ResourceSpec writes;
            writes.accesses = {
                operandAccess(ResourceClass::Handle, 0, AccessMode::ReadModifyWrite, name)};
            writes.result = camel::core::sem::ResultAlias{
                .kind    = camel::core::sem::ResultAlias::Kind::Same,
                .operand = 1};

            ResourceSpec updates = writes;
            updates.result =
                camel::core::sem::ResultAlias{.kind = camel::core::sem::ResultAlias::Kind::Unknown};
            // `update` writes the box *and* runs the function it is handed (ordinary operand 1),
            // so its declaration states both halves of the node's effect (design principles 3.3):
            // the box, and whatever the function value's type says it touches. The parameter type
            // is `(x: T) => T pure`, so a program can hand it only a function that touches
            // nothing -- the union is then the box alone -- but which function it runs is the
            // node's business, not a fact the declaration may leave out.
            updates.functionOperand = camel::core::sem::ResourceBinding{
                .kind  = camel::core::sem::ResourceBinding::Kind::Norm,
                .index = 1};

            table.declareResources(
                "state",
                {
                    {"ref", makes},
                    {"get", reads},
                    {"set", writes},
                    {"update", updates},
                });
        }
        loaded_ = true;
        return true;
    }
};

} // namespace
} // namespace camel::statemod

extern "C" Module *camel_module_create(Context *ctx) {
    return new camel::statemod::StateModule(ctx->shared_from_this());
}