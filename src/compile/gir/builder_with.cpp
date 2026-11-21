// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `with` and the access expression: the graph a `with` node becomes (visitWithNode), the
 * selection an access node names (visitAccsNode), and the two readings they are built on -- the
 * name an access expression leaves immutable (immutableNameOf) and the type a written argument
 * has (writtenArgumentType).
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"
#include "camel/core/trace.h"
#include <format>

#include "camel/compile/gir/static_function.h"
#include "camel/core/data/composite/struct.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/rtdata/string.h"
#include "camel/execute/entry_args.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/other.h"
#include "camel/core/type/ref.h"

#include <atomic>
#include <cctype>
#include <stdexcept>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {
std::optional<std::string> Builder::immutableNameOf(const GCT::node_ptr_t &expr) {
    GCT::node_ptr_t root = expr;
    while (root && root->type() == GCT::NodeKind::ACCS) {
        root = root->at(0);
    }
    if (!root || root->type() != GCT::NodeKind::DREF) {
        return std::nullopt;
    }
    const std::string name = moduleLevelName(root->name());
    // A name without a node is a function or an operator.
    const auto node = nodeAt(name);
    if (!node.has_value() || isVariable(*node)) {
        return std::nullopt;
    }
    return name;
}

/// The type of a `with` argument as far as it is known before the callee is chosen: a literal has
/// the type it is written with, a name has the type it was bound at; anything else takes its type
/// from the parameter it enters (nullptr).
Type *Builder::writtenArgumentType(const GCT::node_ptr_t &argument) {
    if (argument->type() == GCT::NodeKind::DATA) {
        // A composite literal takes its element types from the position it stands in.
        Type *literal = argument->data() != nullptr ? argument->data()->type() : nullptr;
        return literal != nullptr && !isComposite(literal->code()) ? literal : nullptr;
    }
    if (argument->type() == GCT::NodeKind::DREF) {
        if (const auto node = nodeAt(moduleLevelName(argument->name())); node.has_value()) {
            return valueTypeOf(nodeTypeOf(*node));
        }
    }
    return nullptr;
}

node_handle_t Builder::visitWithNode(const GCT::node_ptr_t &gct) {
    ENTER("WITH");
    // `f<w>(x)`: the callee of the call is the target of the binding.
    genericCalleeAllowed_ =
        std::exchange(genericCalleeAllowed_, false) &&
        (gct->at(0)->type() == GCT::NodeKind::DREF || gct->at(0)->type() == GCT::NodeKind::TAPP);
    // The callee of a binding is a name: `name<value>` binds the parameters of `name`
    // (visitDRefNode), it does not make the rule value that early.
    const bool outerRuleBindingCallee = std::exchange(ruleBindingCallee_, true);
    // The explicit type arguments of a generic callee are applied knowing which `with` arguments
    // follow them.
    const size_t outerWithWritten = typeApplicationWithWritten_;
    const auto outerWithNames     = typeApplicationWithNames_;
    const auto outerWithTypes     = typeApplicationWithTypes_;
    if (gct->at(0)->type() == GCT::NodeKind::TAPP) {
        typeApplicationWithWritten_ = gct->size() - 1;
        typeApplicationWithNames_.clear();
        typeApplicationWithTypes_.clear();
        for (size_t i = 1; i < gct->size(); ++i) {
            typeApplicationWithTypes_.push_back(writtenArgumentType(gct->at(i)));
        }
        if (const auto *written = GCT::Tree::of(gct)->argumentNamesOf(gct)) {
            typeApplicationWithNames_ = *written;
        }
    }
    any targetNodeRes           = visit(gct->at(0));
    typeApplicationWithWritten_ = outerWithWritten;
    typeApplicationWithNames_   = outerWithNames;
    typeApplicationWithTypes_   = outerWithTypes;
    ruleBindingCallee_          = outerRuleBindingCallee;
    genericCalleeAllowed_       = false;
    // As in visitLinkNode: a lambda written in place gives its graph.
    node_handle_t targetNode = valueNodeOf(targetNodeRes);
    // Whether the value bound here is the callee of the call being built (`withArgsOfCall_`).
    const bool inCall = std::exchange(withArgsOfCall_, false);
    // A callee with a signature of its own: the only kind whose with-parameter types the call
    // itself completes. `reduce<(a, b) => a + b, 0>`: the element type comes from the array,
    // and the type of the result of the reduction from the position the call stands at, so the
    // lambda and the literal take their types there. A name is not deferred: a generic function
    // or a set of overloads bound as a with-argument stays a name until the callee resolves it.
    vector<Argument> inputs;
    FunctionType *argumentSignature     = signatureForArgumentContext(targetNode, gct->size() - 1);
    const bool parameterTypesFollowCall = inCall && argumentSignature != nullptr;
    // A call of a rest function is expanded at the call (inlineRestCall), so a with-argument
    // that names a generic function or a set of overloads stays a compile-time callable: every
    // call of it in the body picks the instance for that call's arguments (`tree_map<twice>`,
    // `tree_map<bump>`). Making it a function value here would need the one type it does not
    // have yet, and a set of overloads has no single value at all.
    const auto restInfo = genericDrefs_.find(targetNode);
    const bool restCall = restInfo != genericDrefs_.end() && restInfo->second.one() != nullptr &&
                          restInfo->second.one()->funcType->hasRest();
    const auto isCallableName = [&](node_handle_t node) {
        if (genericDrefs_.contains(node)) {
            return true;
        }
        if (node == nullptr || !nodeIsKind(node, runtime::GCNodeKind::Dref)) {
            return false;
        }
        const auto &target = nodeGraphOf(node)->drefTarget(node);
        if (std::holds_alternative<oper_group_ptr_t>(target)) {
            return true;
        }
        if (std::holds_alternative<graph_vec_ptr_t>(target)) {
            auto graphs = asCompileGraphVec(target);
            return graphs && graphs->size() > 1;
        }
        return false;
    };
    const auto *withNames = GCT::Tree::of(gct)->argumentNamesOf(gct);
    // A `with` argument may be written by name (`f<rounds: 8>`, design principles 4.4): the
    // name chooses the parameter the literal takes its type from, and is kept on the argument
    // for the call to bind (bindArguments). A binding used as a value has no names to
    // resolve them with, and is refused there.
    const auto slotOf = [&](size_t written) -> size_t {
        if (withNames == nullptr || written >= withNames->size() || !(*withNames)[written]) {
            return written;
        }
        if (argumentSignature != nullptr) {
            const std::string &name = *(*withNames)[written];
            for (size_t i = 0; i < argumentSignature->withTypesCount(); ++i) {
                if (name == argumentSignature->argNameAt(i)) {
                    return i;
                }
            }
        }
        return written;
    };
    const auto refuseNonConstant = [&](size_t slot, const Argument &argument) {
        if (argumentSignature == nullptr || slot >= argumentSignature->withTypesCount() ||
            !argumentSignature->withNeedsValueAt(slot)) {
            return;
        }
        if (argument.node != nullptr && constantOfNode(argument.node) != nullptr) {
            return;
        }
        const GCT::node_ptr_t &expr = argument.expr;
        diags_->of(SemanticDiag::FactArgumentNotConstant)
            .atOrigin(expr != nullptr && expr->origin() != 0 ? expr->origin() : gct->origin())
            .commit(
                std::string(argumentSignature->argNameAt(slot)),
                argumentSignature->withTypeAt(slot)->toString());
        throw BuildAbortException();
    };
    // The angled values are not the callee: a rule bound inside them (`f->rewrite<[r<7>]>`) is a
    // use of the rule as a value, whatever the binding itself stands beside.
    struct CalleeContext {
        bool &flag;
        bool outer;
        ~CalleeContext() { flag = outer; }
    } calleeContext{ruleBindingCallee_, ruleBindingCallee_};
    ruleBindingCallee_ = false;
    for (size_t i = 1; i < gct->size(); i++) {
        const size_t slot = slotOf(i - 1);
        Type *wanted      = argumentSignature && slot < argumentSignature->withTypesCount()
                                ? argumentSignature->withTypeAt(slot)
                                : nullptr;
        ExpectedValueScope argumentScope(
            expectedValueType_,
            wanted && !containsTypeVar(wanted) ? wanted : nullptr);
        const std::optional<std::string> name =
            withNames != nullptr && i - 1 < withNames->size() ? (*withNames)[i - 1] : std::nullopt;
        if (gct->at(i)->type() == GCT::NodeKind::SPRD) {
            // `..ws` among the `with` arguments: the pack's elements are `with` arguments of their
            // own, exactly as `..e` gives the ordinary arguments of a call. `f<..ws>(..xs)` is how
            // a pack is forwarded to another function's pack, so the elements stand where they
            // stand.
            if (i + 1 != gct->size()) {
                const GCT::node_ptr_t operand = gct->at(i)->at(0);
                diags_->of(SemanticDiag::RestArgumentNotLast)
                    .atOrigin(operand->origin() != 0 ? operand->origin() : gct->origin())
                    .commit(
                        operand->type() == GCT::NodeKind::DREF ? operand->name()
                                                               : std::string("expression"));
                throw BuildAbortException();
            }
            const GCT::node_ptr_t operand = gct->at(i)->at(0);
            for (const node_handle_t &element : expandRestArgument(
                     operand,
                     operand->type() == GCT::NodeKind::DREF ? operand->name()
                                                            : std::string("expression"),
                     gct)) {
                inputs.push_back({element, operand, std::nullopt});
            }
            continue;
        }
        if (!restCall) {
            const GCT::NodeKind kind = gct->at(i)->type();
            // A literal whose type the signature still leaves open takes its type from the
            // position (`reduce<(a, b) => a + b, 0>`), and a lambda that writes no parameter
            // type needs the completed signature for it; both are built at the call, after the
            // ordinary arguments are typed. A lambda that writes every type is built here, as
            // before: nothing about it waits for the call.
            bool deferred = kind == GCT::NodeKind::DATA;
            if (!deferred && kind == GCT::NodeKind::FUNC) {
                const auto &lambda    = gct->at(i);
                const auto *unwritten = GCT::Tree::of(lambda)->unwrittenParamsOf(lambda);
                deferred              = unwritten == nullptr || !unwritten->empty();
            }
            if (parameterTypesFollowCall && deferred && wanted != nullptr &&
                containsTypeVar(wanted)) {
                // Built at the call, where the ordinary arguments are typed and the signature's
                // variables have their types (visitLinkNode). A lambda's parameters come from
                // the parameter it is passed to (design principles 5.9). The name goes with it:
                // the binding puts it at its parameter's place before the literal is built.
                inputs.push_back({nullptr, gct->at(i), name});
                continue;
            }
            // A `with` argument stands where an ordinary argument stands (design principles 5.1):
            // a literal takes its type from the parameter, `[]` its element type, and a `T`
            // enters a `T?`. A value that does not fit is left as it is, for the call to report.
            const bool concrete = wanted != nullptr && !containsTypeVar(wanted) &&
                                  wanted->code() != TypeCode::Function;
            node_handle_t built = concrete && kind == GCT::NodeKind::DATA
                                      ? visitInPosition(gct->at(i), wanted)
                                      : visitOperand(gct->at(i), gct);
            if (concrete && kind != GCT::NodeKind::DATA && built != nullptr &&
                asTagged(wanted) != nullptr &&
                asTagged(valueTypeOf(nodeTypeOf(built))) == nullptr &&
                !valueTypeOf(nodeTypeOf(built))->equals(wanted)) {
                if (node_handle_t entered = coerceTo(built, wanted, gct->at(i))) {
                    built = entered;
                }
            }
            inputs.push_back({built, gct->at(i), name});
            refuseNonConstant(slot, inputs.back());
            continue;
        }
        any res               = visit(gct->at(i));
        node_handle_t argNode = res.type() == typeid(graph_ptr_t)
                                    ? createFuncDataNode(any_cast<graph_ptr_t>(res), true, false)
                                    : valueNodeOf(res);
        if (!isCallableName(argNode)) {
            argNode = functionValueOf(argNode, gct->at(i));
        }
        inputs.push_back({argNode, gct->at(i), name});
    }
    auto &bound = boundWithArgs_[targetNode];
    bound.insert(bound.end(), inputs.begin(), inputs.end());
    boundWithCallee_.try_emplace(targetNode, gct->at(0));
    // The `f<w>` expression the binding was made by: a partial application is located there,
    // at the callee and the angled arguments together (design principles 5.1), not at the
    // statement it is written in (1.6). Its span is derived where the binding is used as a
    // value (partialApplicationOf), so an ordinary `f<w>(x)` allocates nothing extra.
    boundWithOrigin_.try_emplace(targetNode, gct);
    if (!calleeContext.outer && isRuleBindingTarget(targetNode)) {
        // `name<value>` where the name is the function a rule with `with` parameters is written
        // as: the values bind the rule's parameters and the value is the `Rule` (design
        // principles 5.5). What `f<w>` makes of a function -- a function value that has the
        // parameters left -- is not what a rule's name stands for, so this is a call and not a
        // partial application.
        node_handle_t value = ruleBindingValueOf(targetNode, gct);
        LEAVE("WITH");
        return value;
    }
    LEAVE("WITH");
    return targetNode;
}

node_handle_t Builder::visitAccsNode(const GCT::node_ptr_t &gct) {
    ENTER("ACCS");
    node_handle_t tgtNode = valueNodeOf(visit(gct->at(0)));
    GUARD(tgtNode != nullptr, "Access node target is null.");
    if (EnumType *enumType = asEnumType(nodeTypeOf(tgtNode))) {
        // A value of an enum has the fields of the variant it is, and which one that is the
        // compiler does not know (design principles 3.8, 5.3): they are taken out by matching.
        std::string example;
        for (const EnumVariant &variant : enumType->variants()) {
            if (variant.fieldCount() > 0) {
                example = enumType->decl()->shortName() + "::" + variant.name;
                example += variant.shape == VariantShape::Struct
                               ? " { " + variant.fieldNames.front() + ", ... }"
                               : "(...)";
                break;
            }
        }
        diags_->of(SemanticDiag::EnumFieldAccess)
            .atOrigin(gct->origin())
            .commit(
                gct->accessByIndex() ? std::to_string(gct->accessIndex()) : gct->accessKey(),
                enumType->toString(),
                example);
        throw BuildAbortException();
    }
    if (!nodeTypeOf(tgtNode)->isComposite()) {
        diags_->of(SemanticDiag::TypeNotIndexable)
            .atOrigin(gct->origin())
            .commit(nodeTypeOf(tgtNode)->toString());
        throw BuildAbortException();
    }

    const auto tgtType = nodeTypeOf(tgtNode);
    Type *elemType     = nullptr;
    if (asTagged(tgtType) != nullptr) {
        // A `T?` / `T!` has the layout of a tuple and is not one: its parts are reached by
        // `??`, `!!`, `->` and `match` only (design principles 3.9).
        diags_->of(SemanticDiag::TaggedValueNotOpened)
            .atOrigin(gct->origin())
            .commit(tgtType->toString(), "a member cannot be read from it");
        throw BuildAbortException();
    }

    switch (tgtType->code()) {
    case TypeCode::Tuple: {
        const auto &tupleType = tt::as_ptr<TupleType>(tgtType);
        const auto *gctTree   = GCT::Tree::of(gct);
        const size_t names    = gctTree != nullptr ? gctTree->unpackCount(gct) : 0;
        // A destructuring declaration binds one name per element: `let (x, y) = t` needs a tuple of
        // two, whichever way the count is wrong (design principles 3.9). Reported here, at the
        // declaration, and not as an element read past the end -- a name too few would drop the
        // remaining elements in silence, and a name too many would report an index the source
        // never wrote.
        // One answer per declaration: every name of it carries the count, and the first reports.
        if (names != 0 && gct->accessIndex() == 0 && names != tupleType->size()) {
            diags_->of(SemanticDiag::TupleUnpackingCountMismatch).atOrigin(gct->origin()).commit();
            throw BuildAbortException();
        }
        // A tuple has a fixed length: an element past its end is a compile-time error (it was
        // read unchecked, which crashed the compiler in release builds).
        if (gct->accessIndex() >= tupleType->size()) {
            diags_->of(SemanticDiag::InvalidAccessIndex)
                .atOrigin(gct->origin())
                .commit(std::to_string(gct->accessIndex()));
            throw BuildAbortException();
        }
        elemType = tupleType->typeAt(gct->accessIndex());
        break;
    }
    case TypeCode::Array: {
        // `.N` reads a tuple element and `.name` a struct field. On an array no back end
        // implements either: the array was read as if it were a tuple, without a bounds
        // check, and the program printed whatever was there. An element is read with `[N]`.
        diags_->of(SemanticDiag::TypeNotIndexable)
            .atOrigin(gct->origin())
            .commit(tgtType->toString());
        throw BuildAbortException();
    }
    case TypeCode::Struct: {
        const auto *strTy = tt::as_ptr<camel::core::type::StructType>(tgtType);
        if (gct->accessByIndex()) {
            // `.N` is a tuple's element; a struct's members are read by name. Asking for the name
            // of a positional access dereferences the name it does not have -- a null pointer the
            // assert above guards in a debug build and release builds dropped, which is how `p.1`
            // on a struct segfaulted the compiler instead of being refused.
            diags_->of(SemanticDiag::InvalidAccessIndex)
                .atOrigin(gct->origin())
                .commit(std::to_string(gct->accessIndex()));
            throw BuildAbortException();
        }
        const auto &optIndex = strTy->findField(gct->accessKey());
        if (!optIndex.has_value()) {
            diags_->of(SemanticDiag::InvalidAccessIndex)
                .atOrigin(gct->origin())
                .commit(gct->accessKey());
            throw BuildAbortException();
        }
        elemType = strTy->typeAt(optIndex.value());
        break;
    }
    default:
        // A composite that is none of the three above has no element to read: a function value is
        // the case that reaches here, and it is not a corner -- `split` answers the forward half,
        // so `let (y, lin) = split(f)` destructures a *function*, and the read lands on a type
        // whose members are parameters, not elements (`(float, float) => float`). A `runtime_error`
        // would be reported by the driver as a defect of the compiler; a program that
        // asks a function for its element is a program with a mistake in it, and it is told which
        // type it asked (the split ABI in test/cases/transforms reaches
        // it).
        diags_->of(SemanticDiag::TypeNotIndexable)
            .atOrigin(gct->origin())
            .commit(tgtType->toString());
        throw BuildAbortException();
    }

    // A member read of a compile-time struct value is that field's value, known here as well: the
    // third name of `fields T` binds such a value (Builder::staticFieldTagsNode builds a field's
    // tags as a compile-time struct), and `tags.width.w` has to be a compile-time constant -- it
    // may be the key of a `t.[…]` or a type argument in turn. The read stays in the graph, because
    // the printing pass spells it and recompiling has to give the same graph back; the value it
    // reads is recorded where the compiler keeps one, as `std::opt` folds the same access. A field
    // whose value has another type than the type declares keeps its read alone.
    data_ptr_t readConstant = nullptr;
    if (!gct->accessByIndex() && tgtType->code() == TypeCode::Struct) {
        if (const data_ptr_t subject = constantOfNode(tgtNode);
            subject != nullptr && subject->type() != nullptr &&
            subject->type()->code() == TypeCode::Struct) {
            if (const auto object = std::dynamic_pointer_cast<StructData>(subject);
                object != nullptr && object->resolved()) {
                if (const data_ptr_t field = object->get(gct->accessKey());
                    field != nullptr && field->type() != nullptr && field->type() == elemType) {
                    readConstant = field;
                }
            }
        }
    }

    node_handle_t accsNode{};
    if (gct->accessByIndex()) {
        accsNode = currGraph_->addAccsNode(elemType, static_cast<uint32_t>(gct->accessIndex()));
    } else {
        accsNode = currGraph_->addAccsNode(elemType, gct->accessKey());
    }
    // `typeof` infers a type and evaluates nothing, and the graph it builds is dropped: a value
    // that lives in the graph the type is used in is borrowed by name, not by node. Linking the
    // outer node here would leave an edge in this graph pointing at a node it does not own, which
    // outlives the graph, so a port carrying that node's type stands in for it.
    node_handle_t accsSource = tgtNode;
    if (nodeGraphOf(tgtNode) != currGraph_.get()) {
        static unsigned long long typeofStandIns = 0;
        accsSource                               = currGraph_->addPortNode(
            nodeTypeOf(tgtNode),
            "$typeof" + std::to_string(++typeofStandIns),
            false);
    }
    linkNodes(LinkType::Norm, accsSource, accsNode);
    registerNodeOrigin(context_, accsNode, gct, "gir.accs");
    if (readConstant != nullptr) {
        rememberConstant(accsNode, readConstant);
    }
    LEAVE("ACCS");
    return accsNode;
}

} // namespace camel::compile::gir
