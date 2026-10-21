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
 * Created: Oct. 20, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The enum: the construction of a variant and the match that takes one apart.
 */

#include "builder.h"
#include "builder_support.h"
#include "builder_helpers.h"

#include "camel/compile/gir/static_function.h"
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

#include <atomic>
#include <cctype>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

// ---- The enum (design principles 5.3) ----

namespace {

/// `Shape::Rect` gives ("Shape", "Rect"). A name that is not qualified gives an empty prefix.
std::pair<std::string, std::string> splitQualifiedName(const std::string &written) {
    const size_t last = written.rfind("::");
    if (last == std::string::npos) {
        return {{}, written};
    }
    return {written.substr(0, last), written.substr(last + 2)};
}

std::string lastSegmentOf(const std::string &path) {
    const size_t last = path.rfind("::");
    return last == std::string::npos ? path : path.substr(last + 2);
}

} // namespace

EnumType *Builder::enumTypeOfName(const Reference &name) {
    Type *resolved = nullptr;
    if (const auto declared = unit_->typeDecls.find(name); declared != unit_->typeDecls.end()) {
        resolved = declared->second;
    } else if (const auto imported = importedType(name); imported.has_value()) {
        resolved = imported.value();
    }
    if (resolved == nullptr) {
        return nullptr;
    }
    if (EnumType *instance = asEnumType(resolved)) {
        return instance;
    }
    if (resolved->code() == TypeCode::TypeFunc) {
        // A generic enum written without its arguments (`Option`): the declaration's own
        // instance, whose arguments are its parameters; a value of it is made by inference.
        return asEnumType(static_cast<TypeFunctionType *>(resolved)->body());
    }
    return nullptr;
}

bool Builder::enumVariantOfName(
    const std::string &written, const GCT::node_ptr_t &origin, EnumType *&type, size_t &tag) {
    const auto [prefix, name] = splitQualifiedName(written);
    if (prefix.empty()) {
        return false;
    }
    EnumType *found = enumTypeOfName(Reference(prefix));
    if (found == nullptr) {
        return false; // a qualified name of something else (`io::println`)
    }
    const auto variant = found->decl()->findVariant(name);
    if (!variant.has_value()) {
        diags_->of(SemanticDiag::EnumPatternMismatch)
            .atOrigin(origin->origin())
            .commit(
                written,
                found->toString(),
                "'" + name + "' is not one of its variants (" + found->decl()->name() +
                    " declares " +
                    [&] {
                        std::string list;
                        for (const EnumVariant &each : found->decl()->variants()) {
                            if (!list.empty()) {
                                list += ", ";
                            }
                            list += each.name;
                        }
                        return list;
                    }() +
                    ")");
        throw BuildAbortException();
    }
    type = found;
    tag  = *variant;
    return true;
}

node_handle_t Builder::applyInternalOperator(
    const char *op, Type *typeArgument, const std::vector<GCT::node_ptr_t> &operands,
    const GCT::node_ptr_t &origin) {
    return applyInternalOperatorCall(tree_->makeDRef(op), typeArgument, operands, origin);
}

node_handle_t Builder::applyInternalOperator(
    camel::core::OperId op, Type *typeArgument, const std::vector<GCT::node_ptr_t> &operands,
    const GCT::node_ptr_t &origin) {
    return applyInternalOperatorCall(internalOperatorDref(op), typeArgument, operands, origin);
}

node_handle_t Builder::applyInternalOperatorCall(
    const GCT::node_ptr_t &calleeNode, Type *typeArgument,
    const std::vector<GCT::node_ptr_t> &operands, const GCT::node_ptr_t &origin) {
    GCT::node_ptr_t callee = calleeNode;
    if (typeArgument != nullptr) {
        // The instance the call makes, as `f::<T>` gives one: the operator's result type is
        // that instance, and its slot is of it (a value of an enum type holds references).
        GCT::node_ptr_t application = tree_->makeTypeApplication();
        *application << callee << tree_->makeType(typeArgument);
        callee = application;
    }
    GCT::node_ptr_t call = tree_->makeLink(static_cast<uint32_t>(operands.size()));
    *call << callee;
    for (const auto &operand : operands) {
        *call << operand;
    }
    call->setOrigin(origin->origin());
    return visitLinkNode(call);
}

node_handle_t
Builder::visitEnumConstruction(const GCT::node_ptr_t &gct, EnumType *type, size_t tag) {
    const EnumDecl *decl       = type->decl();
    const EnumVariant &variant = decl->variants()[tag];
    const bool called          = gct->type() == GCT::NodeKind::LINK;
    const size_t argumentCount = called ? gct->size() - 1 : 0;

    std::vector<node_handle_t> fields;
    std::vector<Type *> fieldTypes; // what the arguments are, before the field types are known
    if (variant.shape == VariantShape::Struct) {
        if (argumentCount != 1) {
            diags_->of(SemanticDiag::EnumConstructionMismatch)
                .atOrigin(gct->origin())
                .commit(
                    decl->shortName() + "::" + variant.name,
                    decl->name(),
                    "its fields are given by name: '" + decl->shortName() + "::" + variant.name +
                        " { " +
                        [&] {
                            std::string list;
                            for (const std::string &field : variant.fieldNames) {
                                if (!list.empty()) {
                                    list += ", ";
                                }
                                list += field + ": ...";
                            }
                            return list;
                        }() +
                        " }'");
            throw BuildAbortException();
        }
        node_handle_t literal = visitOperand(gct->at(1), gct);
        Type *literalType     = valueTypeOf(nodeTypeOf(literal));
        if (literalType->code() != TypeCode::Struct) {
            diags_->of(SemanticDiag::EnumConstructionMismatch)
                .atOrigin(gct->at(1)->origin())
                .commit(
                    decl->shortName() + "::" + variant.name,
                    decl->name(),
                    "the value has type '" + literalType->toString() +
                        "', and the fields of a struct variant are given by name");
            throw BuildAbortException();
        }
        StructType *literalStruct = tt::as_ptr<StructType>(literalType);
        for (size_t i = 0; i < variant.fieldCount(); ++i) {
            const std::string &fieldName = variant.fieldNames[i];
            const auto position          = literalStruct->findField(fieldName);
            if (!position.has_value()) {
                diags_->of(SemanticDiag::EnumConstructionMismatch)
                    .atOrigin(gct->at(1)->origin())
                    .commit(
                        decl->shortName() + "::" + variant.name,
                        decl->name(),
                        "the value has no field '" + fieldName + "'");
                throw BuildAbortException();
            }
            Type *fieldType         = literalStruct->typeAt(*position);
            node_handle_t fieldNode = currGraph_->addAccsNode(fieldType, fieldName);
            linkNodes(LinkType::Norm, literal, fieldNode);
            registerNodeOrigin(context_, fieldNode, gct->at(1), "gir.accs");
            fields.push_back(fieldNode);
            fieldTypes.push_back(fieldType);
        }
        if (literalStruct->size() != variant.fieldCount()) {
            diags_->of(SemanticDiag::EnumConstructionMismatch)
                .atOrigin(gct->at(1)->origin())
                .commit(
                    decl->shortName() + "::" + variant.name,
                    decl->name(),
                    "the value has fields the variant does not: a struct variant takes exactly "
                    "the fields its declaration writes");
            throw BuildAbortException();
        }
    } else {
        if (argumentCount != variant.fieldCount()) {
            diags_->of(SemanticDiag::EnumVariantFieldCount)
                .atOrigin(gct->origin())
                .commit(
                    variant.name,
                    decl->name(),
                    std::to_string(variant.fieldCount()),
                    std::to_string(argumentCount));
            throw BuildAbortException();
        }
        for (size_t i = 0; i < argumentCount; ++i) {
            node_handle_t argument = visitOperand(gct->at(i + 1), gct);
            fields.push_back(argument);
            fieldTypes.push_back(valueTypeOf(nodeTypeOf(argument)));
        }
    }

    // The instance: the declaration's own when it is not generic, otherwise the arguments the
    // fields give (a field left open takes what the position expects, as `let x: Option<int>
    // = Option::None` does).
    EnumType *instance = type;
    if (decl->generic()) {
        type_bindings_t bindings;
        for (size_t i = 0; i < fields.size(); ++i) {
            unifyTypeVars(variant.fieldTypes[i], fieldTypes[i], bindings);
        }
        std::vector<Type *> arguments;
        arguments.reserve(decl->params().size());
        EnumType *expected = asEnumType(expectedValueType_);
        for (size_t i = 0; i < decl->params().size(); ++i) {
            Type *bound = findTypeBinding(bindings, decl->params()[i]);
            if (bound == nullptr && expected != nullptr && expected->decl() == decl &&
                expected->complete()) {
                bound = expected->params()[i];
            }
            if (bound == nullptr) {
                diags_->of(SemanticDiag::EnumTypeArgumentNotInferred)
                    .atOrigin(gct->origin())
                    .commit(decl->params()[i], decl->name(), variant.name);
                throw BuildAbortException();
            }
            arguments.push_back(bound);
        }
        instance = EnumType::get(const_cast<EnumDecl *>(decl), arguments);
    }

    // Each field as the variant of this instance has it.
    std::vector<GCT::node_ptr_t> operands;
    operands.push_back(tree_->makeData(
        tt::as_shared<Data>(std::make_shared<PrimaryData<int64_t>>(static_cast<int64_t>(tag)))));
    for (size_t i = 0; i < fields.size(); ++i) {
        Type *wanted          = instance->fieldType(tag, i);
        node_handle_t coerced = checkValue(fields[i], wanted, gct);
        if (coerced == nullptr) {
            diags_->of(SemanticDiag::EnumFieldTypeMismatch)
                .atOrigin(gct->origin())
                .commit(
                    variant.shape == VariantShape::Struct ? "'" + variant.fieldNames[i] + "'"
                                                          : std::to_string(i),
                    decl->shortName() + "::" + variant.name,
                    wanted->toString(),
                    fieldTypes[i]->toString());
            throw BuildAbortException();
        }
        operands.push_back(tree_->makeDRef(bindTemporary(coerced)));
    }
    return applyInternalOperator(camel::core::oper::EnumMake, instance, operands, gct);
}

node_handle_t
Builder::visitEnumMatch(const GCT::node_ptr_t &gct, node_handle_t scrutinee, EnumType *type) {
    using CaseType = GCT::CaseType;
    struct Arm {
        const GCT::node_ptr_t caseNode;
        size_t tag = 0;
        // The position of the field in the variant, and the name the arm binds it to.
        std::vector<std::pair<size_t, std::string>> bindings;
    };

    const std::string name      = bindTemporary(scrutinee);
    const node_handle_t tagNode = applyTagOperator(camel::core::oper::EnumTag, name, gct);
    const std::string tagName   = bindTemporary(tagNode);

    std::vector<Arm> arms;
    const GCT::node_ptr_t wildcard = [&] {
        for (size_t i = 1; i < gct->size(); ++i) {
            if (gct->at(i)->caseType() == CaseType::Else) {
                return gct->at(i);
            }
        }
        return GCT::node_ptr_t(nullptr);
    }();
    std::vector<bool> covered(type->variantCount(), false);
    for (size_t i = 1; i < gct->size(); ++i) {
        const GCT::node_ptr_t &caseNode = gct->at(i);
        if (caseNode->caseType() == CaseType::Else) {
            break; // the last case
        }
        if (caseNode->caseType() != CaseType::Enum) {
            // A bare name is a binding pattern everywhere else (`case v` over `T?` / `T!`), so
            // `case Empty` over an enum is read as a binding, not as the unit variant: say what
            // the name refers to and how the variant is written, instead of only that a case
            // must name a variant.
            std::string written = "case";
            std::string reason =
                "every case of a match over an enum names a variant, and the value a case "
                "does not name falls to 'case _'";
            if (caseNode->caseType() == CaseType::Bind) {
                written        = caseNode->binding();
                const auto tag = type->decl()->findVariant(written);
                if (tag.has_value()) {
                    const EnumVariant &variant = type->decl()->variants()[*tag];
                    const std::string how = variant.shape == VariantShape::Unit    ? "'"
                                            : variant.shape == VariantShape::Tuple ? "(...)'"
                                                                                   : " { ... }'";
                    reason = "'" + written +
                             "' is a variant of this enum, and a bare name in a pattern is a "
                             "binding; write the variant qualified: 'case " +
                             type->decl()->shortName() + "::" + written + how +
                             ", or 'case _' for every other value";
                } else {
                    reason = "'" + written +
                             "' is a bare name, and a bare name in a pattern is a binding; a "
                             "match over an enum names a variant: write 'case " +
                             type->decl()->shortName() + "::<variant>', or 'case _'";
                }
            }
            diags_->of(SemanticDiag::EnumPatternMismatch)
                .atOrigin(caseNode->origin())
                .commit(written, type->toString(), reason);
            throw BuildAbortException();
        }
        const GCT::Node::EnumCase &pattern = caseNode->enumCase();
        const auto [prefix, variantName]   = splitQualifiedName(*pattern.label);
        if (!prefix.empty() && lastSegmentOf(prefix) != lastSegmentOf(type->decl()->name())) {
            diags_->of(SemanticDiag::EnumPatternMismatch)
                .atOrigin(caseNode->origin())
                .commit(*pattern.label, type->toString(), "it is a variant of another enum");
            throw BuildAbortException();
        }
        const auto tag = type->decl()->findVariant(variantName);
        if (!tag.has_value()) {
            diags_->of(SemanticDiag::EnumPatternMismatch)
                .atOrigin(caseNode->origin())
                .commit(
                    *pattern.label,
                    type->toString(),
                    "'" + variantName + "' is not one of its variants");
            throw BuildAbortException();
        }
        const EnumVariant &variant = type->decl()->variants()[*tag];
        const bool structPattern   = caseNode->enumStructPattern();
        if (structPattern != (variant.shape == VariantShape::Struct)) {
            diags_->of(SemanticDiag::EnumPatternMismatch)
                .atOrigin(caseNode->origin())
                .commit(
                    *pattern.label,
                    type->toString(),
                    variant.shape == VariantShape::Struct
                        ? "this variant's fields are named: write 'case " + variantName + " { " +
                              [&] {
                                  std::string list;
                                  for (const std::string &field : variant.fieldNames) {
                                      if (!list.empty()) {
                                          list += ", ";
                                      }
                                      list += field;
                                  }
                                  return list;
                              }() +
                              " }'"
                        : "this variant's fields are in order: write 'case " + variantName + "(" +
                              [&] {
                                  std::string list;
                                  for (size_t f = 0; f < variant.fieldCount(); ++f) {
                                      if (!list.empty()) {
                                          list += ", ";
                                      }
                                      list += "_";
                                  }
                                  return list;
                              }() +
                              ")'");
            throw BuildAbortException();
        }
        Arm arm{caseNode, *tag, {}};
        if (structPattern) {
            for (size_t f = 0; f < pattern.count; ++f) {
                const std::string &fieldName = *pattern.fields[f];
                const auto position          = variant.findField(fieldName);
                if (!position.has_value()) {
                    diags_->of(SemanticDiag::EnumPatternMismatch)
                        .atOrigin(caseNode->origin())
                        .commit(
                            *pattern.label,
                            type->toString(),
                            "the variant has no field '" + fieldName + "'");
                    throw BuildAbortException();
                }
                arm.bindings.emplace_back(*position, *pattern.bindings[f]);
            }
            // A field written twice binds one position twice: the later binding was read
            // first, silently.
            for (size_t a = 0; a < arm.bindings.size(); ++a) {
                for (size_t b = a + 1; b < arm.bindings.size(); ++b) {
                    if (arm.bindings[a].first == arm.bindings[b].first) {
                        diags_->of(SemanticDiag::EnumPatternMismatch)
                            .atOrigin(caseNode->origin())
                            .commit(
                                *pattern.label,
                                type->toString(),
                                "the field '" + variant.fieldNames[arm.bindings[a].first] +
                                    "' is written twice");
                        throw BuildAbortException();
                    }
                }
            }
        } else {
            if (pattern.count != variant.fieldCount()) {
                diags_->of(SemanticDiag::EnumPatternMismatch)
                    .atOrigin(caseNode->origin())
                    .commit(
                        *pattern.label,
                        type->toString(),
                        "the variant has " + std::to_string(variant.fieldCount()) +
                            " field(s) and the pattern gives " + std::to_string(pattern.count) +
                            "; write each position, '_' for the one not bound");
                throw BuildAbortException();
            }
            for (size_t f = 0; f < pattern.count; ++f) {
                arm.bindings.emplace_back(f, *pattern.bindings[f]);
            }
        }
        if (covered[*tag]) {
            diags_->of(SemanticDiag::EnumVariantCaseRepeated)
                .atOrigin(caseNode->origin())
                .commit(variantName);
        }
        covered[*tag] = true;
        arms.push_back(std::move(arm));
    }
    if (wildcard == nullptr) {
        std::string missing;
        for (size_t tag = 0; tag < type->variantCount(); ++tag) {
            if (!covered[tag]) {
                if (!missing.empty()) {
                    missing += ", ";
                }
                missing += "'" + type->decl()->variants()[tag].name + "'";
            }
        }
        if (!missing.empty()) {
            diags_->of(SemanticDiag::EnumMatchNotExhaustive)
                .atOrigin(gct->origin())
                .commit(type->toString(), missing);
            throw BuildAbortException();
        }
    } else {
        // Naming every variant already covers every value: the wildcard is a case that can
        // never be taken. It stays in the graph (the request asked for it), but say so.
        bool everyVariantCovered = true;
        for (bool each : covered) {
            if (!each) {
                everyVariantCovered = false;
                break;
            }
        }
        if (everyVariantCovered) {
            diags_->of(SemanticDiag::MatchWildcardRedundant)
                .atOrigin(wildcard->origin())
                .commit(type->toString());
        }
    }

    const auto indexData = [&](size_t index) {
        return tree_->makeData(
            tt::as_shared<Data>(
                std::make_shared<PrimaryData<int64_t>>(static_cast<int64_t>(index))));
    };
    // The fields the case binds, each taken out of the value by the position of the variant the
    // case names: the variant is known here, so the type of each field is known too.
    const auto bindFields = [&](const Arm &arm) {
        for (const auto &[index, binding] : arm.bindings) {
            if (binding.empty()) {
                continue;
            }
            node_handle_t fieldNode = applyInternalOperator(
                camel::core::oper::EnumField,
                nullptr,
                {indexData(arm.tag), indexData(index), tree_->makeDRef(name)},
                arm.caseNode);
            insertNode(binding, fieldNode);
        }
    };
    // The arm that runs when no case names the value: the wildcard the program wrote, or,
    // when it wrote every variant, an arm that cannot be reached. The second is needed all
    // the same: the branch it ends offers the join one value of the arms' type.
    std::function<arm_ptr_t()> defaultArm = [&]() -> arm_ptr_t {
        if (wildcard != nullptr) {
            return buildArm(wildcard->at(0), [&] { return visitBodyNode(wildcard->at(0)); });
        }
        const Arm &last = arms.back();
        return buildArm(last.caseNode, [&] {
            bindFields(last);
            return visitBodyNode(last.caseNode);
        });
    };

    // Every case of the value is one test of the tag: `tag == k`, an ordinary comparison,
    // whose true arm is the body of that case and whose false arm is the test of the next.
    // A branch on a boolean has no With inputs, so the case values of the GCT (variant
    // names) never reach a node.
    std::function<node_handle_t(size_t)> branchAt = [&](size_t k) -> node_handle_t {
        const Arm &arm          = arms[k];
        node_handle_t condition = applyInternalOperator(
            "__eq__",
            nullptr,
            {tree_->makeDRef(tagName), indexData(arm.tag)},
            arm.caseNode);
        std::vector<arm_ptr_t> two(2);
        two[0] = buildArm(arm.caseNode, [&] {
            bindFields(arm);
            return visitBodyNode(arm.caseNode);
        });
        if (k + 1 < arms.size()) {
            const GCT::node_ptr_t next = arms[k + 1].caseNode;
            two[1]                     = buildArm(next, [&] { return branchAt(k + 1); });
        } else {
            two[1] = defaultArm();
        }
        return buildBranch(gct, condition, &two);
    };
    if (arms.empty()) {
        // `match e { case _ => ... }`: nothing is tested; the only arm is the value. The
        // branch has one arm and no case value, so the arm the runtime chooses is that one.
        node_handle_t always = currGraph_->addStaticDataNode(
            tt::as_shared<Data>(std::make_shared<PrimaryData<bool>>(true)));
        registerNodeOrigin(context_, always, gct, "gir.data");
        std::vector<arm_ptr_t> one(1);
        one[0] = buildArm(wildcard->at(0), [&] { return visitBodyNode(wildcard->at(0)); });
        return buildBranch(gct, always, &one);
    }
    return branchAt(0);
}

} // namespace camel::compile::gir
