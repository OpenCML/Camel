/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Mar. 26, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "antlr/OpenCMLLexer.h"

#include "ast.h"
#include "core/struct/token.h"
#include "core/struct/function.h"
#include "utils/log.h"

#define DEBUG_LEVEL -1

ast_ptr_t copyFuncDeRefNode = nullptr;
ast_ptr_t castFuncDeRefNode = nullptr;
ast_ptr_t typeFuncDeRefNode = nullptr;
ast_ptr_t indexFuncDeRefNode = nullptr;
ast_ptr_t chainFuncDeRefNode = nullptr;

ast_ptr_t addFuncDeRefNode = nullptr;
ast_ptr_t subFuncDeRefNode = nullptr;
ast_ptr_t mulFuncDeRefNode = nullptr;
ast_ptr_t divFuncDeRefNode = nullptr;
ast_ptr_t modFuncDeRefNode = nullptr;
ast_ptr_t powFuncDeRefNode = nullptr;
ast_ptr_t interFuncDeRefNode = nullptr;
ast_ptr_t unionFuncDeRefNode = nullptr;

ast_ptr_t iAddFuncDeRefNode = nullptr;
ast_ptr_t iSubFuncDeRefNode = nullptr;
ast_ptr_t iMulFuncDeRefNode = nullptr;
ast_ptr_t iDivFuncDeRefNode = nullptr;
ast_ptr_t iModFuncDeRefNode = nullptr;
ast_ptr_t iPowFuncDeRefNode = nullptr;
ast_ptr_t iInterFuncDeRefNode = nullptr;
ast_ptr_t iUnionFuncDeRefNode = nullptr;

ast_ptr_t ltFuncDeRefNode = nullptr;
ast_ptr_t gtFuncDeRefNode = nullptr;
ast_ptr_t leFuncDeRefNode = nullptr;
ast_ptr_t geFuncDeRefNode = nullptr;
ast_ptr_t eqFuncDeRefNode = nullptr;
ast_ptr_t neFuncDeRefNode = nullptr;
ast_ptr_t andFuncDeRefNode = nullptr;
ast_ptr_t orFuncDeRefNode = nullptr;

void initFuncDeRefNodes() {
    if (copyFuncDeRefNode) {
        return;
    }

    copyFuncDeRefNode = createAstNode<DRefASTLoad>("__copy__");
    castFuncDeRefNode = createAstNode<DRefASTLoad>("__cast__");
    typeFuncDeRefNode = createAstNode<DRefASTLoad>("__type__");
    indexFuncDeRefNode = createAstNode<DRefASTLoad>("__index__");
    chainFuncDeRefNode = createAstNode<DRefASTLoad>("__chain__");

    addFuncDeRefNode = createAstNode<DRefASTLoad>("__add__");
    subFuncDeRefNode = createAstNode<DRefASTLoad>("__sub__");
    mulFuncDeRefNode = createAstNode<DRefASTLoad>("__mul__");
    divFuncDeRefNode = createAstNode<DRefASTLoad>("__div__");
    modFuncDeRefNode = createAstNode<DRefASTLoad>("__mod__");
    powFuncDeRefNode = createAstNode<DRefASTLoad>("__pow__");
    interFuncDeRefNode = createAstNode<DRefASTLoad>("__inter__");
    unionFuncDeRefNode = createAstNode<DRefASTLoad>("__union__");

    iAddFuncDeRefNode = createAstNode<DRefASTLoad>("__i_add__");
    iSubFuncDeRefNode = createAstNode<DRefASTLoad>("__i_sub__");
    iMulFuncDeRefNode = createAstNode<DRefASTLoad>("__i_mul__");
    iDivFuncDeRefNode = createAstNode<DRefASTLoad>("__i_div__");
    iModFuncDeRefNode = createAstNode<DRefASTLoad>("__i_mod__");
    iPowFuncDeRefNode = createAstNode<DRefASTLoad>("__i_pow__");
    iInterFuncDeRefNode = createAstNode<DRefASTLoad>("__i_inter__");
    iUnionFuncDeRefNode = createAstNode<DRefASTLoad>("__i_union__");

    ltFuncDeRefNode = createAstNode<DRefASTLoad>("__lt__");
    gtFuncDeRefNode = createAstNode<DRefASTLoad>("__gt__");
    leFuncDeRefNode = createAstNode<DRefASTLoad>("__le__");
    geFuncDeRefNode = createAstNode<DRefASTLoad>("__ge__");
    eqFuncDeRefNode = createAstNode<DRefASTLoad>("__eq__");
    neFuncDeRefNode = createAstNode<DRefASTLoad>("__ne__");
    andFuncDeRefNode = createAstNode<DRefASTLoad>("__and__");
    orFuncDeRefNode = createAstNode<DRefASTLoad>("__or__");
}

data_ptr_t ASTConstructor::extractStaticValue(const ast_ptr_t &node) {
    if (node->type() == ASTNodeType::DATA) {
        const auto dataNode = std::dynamic_pointer_cast<DataASTLoad>(node->load());
        return dataNode->data();
    } else if (node->type() == ASTNodeType::DREF) {
        const auto refNode = std::dynamic_pointer_cast<DRefASTLoad>(node->load());
        return std::make_shared<DanglingValue>(refNode->ident());
    } else {
        return nullptr;
    }
}

std::pair<ast_ptr_t, data_ptr_t> ASTConstructor::makeDanglingValue(const ast_ptr_t &expr) {
    const std::string indent = std::to_string(indentIndex_++);
    ast_ptr_t refNode = createAstNode<NRefASTLoad>(indent);
    *refNode << expr;
    data_ptr_t value = std::make_shared<DanglingValue>(indent);
    return std::make_pair(refNode, value);
}

std::pair<data_ptr_t, bool> ASTConstructor::extractValue(const ast_ptr_t &node, ast_ptr_t &execNode, bool &dangling) {
    const data_ptr_t value = extractStaticValue(node);
    if (value) {
        return std::make_pair(value, false);
    } else {
        dangling = true;
        auto [refNode, refValue] = makeDanglingValue(node);
        *execNode << refNode;
        return std::make_pair(refValue, true);
    }
}

/*
program : stmtList? EOF;
*/
std::any ASTConstructor::visitProgram(OpenCMLParser::ProgramContext *context) {
    debug(0) << "visitProgram" << std::endl;
    if (context->stmtList()) {
        root_ = std::any_cast<ast_ptr_t>(visitStmtList(context->stmtList()));
    } else {
        root_ = createAstNode<ExecASTLoad>();
    }
    return root_;
};

/*
stmtList : stmt+ ;
*/
std::any ASTConstructor::visitStmtList(OpenCMLParser::StmtListContext *context) {
    debug(0) << "visitStmtList" << std::endl;
    pushScope();
    ast_ptr_t execNode = createAstNode<ExecASTLoad>();
    for (const auto &stmt : context->stmt()) {
        *execNode << std::any_cast<ast_ptr_t>(visitStmt(stmt));
    }
    popScope();
    return execNode;
};

/*
stmt
    : letStmt SEP
    | useStmt SEP
    | typeStmt SEP
    | exprStmt SEP
    | assignStmt SEP
    | funcDef SEP
    | retStmt SEP
    ;
*/
std::any ASTConstructor::visitStmt(OpenCMLParser::StmtContext *context) {
    debug(0) << "visitStmt" << std::endl;
    switch (context->getAltNumber()) {
    case 1:
        return visitLetStmt(context->letStmt());
        break;
    case 2:
        return visitUseStmt(context->useStmt());
        break;
    case 3:
        return visitTypeStmt(context->typeStmt());
        break;
    case 4:
        return visitExprStmt(context->exprStmt());
        break;
    case 5:
        return visitAssignStmt(context->assignStmt());
        break;
    case 6:
        return visitFuncDef(context->funcDef());
        break;
    case 7:
        return visitRetStmt(context->retStmt());
        break;

    default:
        throw std::runtime_error("Unknown statement type");
    }
};

/*
letStmt : LET carrier (':' typeExpr)? ('='? entityExpr)?
        | carrier (':' typeExpr)? ':=' entityExpr ;
*/
std::any ASTConstructor::visitLetStmt(OpenCMLParser::LetStmtContext *context) {
    debug(0) << "visitLetStmt" << std::endl;
    const auto &[carrierType, carrier] = std::any_cast<std::pair<size_t, std::any>>(visitCarrier(context->carrier()));
    const auto &typeExpr = context->typeExpr();
    type_ptr_t type = nullptr;
    if (typeExpr) {
        type = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr));
    }
    const auto &entityExpr = context->entityExpr();
    ast_ptr_t exprNode = nullptr;
    if (entityExpr) {
        exprNode = std::any_cast<ast_ptr_t>(visitEntityExpr(entityExpr));
    } else {
        exprNode = createAstNode<DataASTLoad>(std::make_shared<NullValue>(type));
    }
    switch (carrierType) {
    case 1: // identRef
    {
        const std::string &ident = std::any_cast<std::string>(carrier);
        ast_ptr_t newRefNode = createAstNode<NRefASTLoad>(ident);
        ast_ptr_t baseNode = nullptr;
        if (type) {
            bool dangling = false;
            ast_ptr_t execNode = createAstNode<ExecASTLoad>();
            ast_ptr_t linkNode = createAstNode<LinkASTLoad>();
            auto [exprValue, _] = extractValue(exprNode, execNode, dangling);
            ast_ptr_t dataNode = createAstNode<DataASTLoad>(
                std::make_shared<ListValue>(data_list_t{exprValue, std::make_shared<NullValue>(type)}),
                dangling ? data_list_t{exprValue} : data_list_t{});
            ast_ptr_t funcNode = castFuncDeRefNode;

            if (dangling) {
                *execNode << dataNode;
                *linkNode << execNode << funcNode;
            } else {
                *linkNode << dataNode << funcNode;
            }

            baseNode = linkNode;
        } else {
            baseNode = exprNode;
        }
        if (exprNode->type() == ASTNodeType::DREF) {
            ast_ptr_t copyNode = createAstNode<LinkASTLoad>();
            ast_ptr_t copyFunc = copyFuncDeRefNode;
            *copyNode << baseNode << copyFunc;
            baseNode = copyNode;
        }
        *newRefNode << baseNode;
        return newRefNode;
    } break;

    case 2: // bracedIdents
    {
        if (type) {
            const auto &token = context->getStart();
            throw BuildException("Type cannot be specified for multiple identifiers", token);
        }
        const std::vector<std::string> &idents = std::any_cast<std::vector<std::string>>(carrier);
        ast_ptr_t execNode = createAstNode<ExecASTLoad>();
        for (size_t i = 0; i < idents.size(); i++) {
            const std::string &ident = idents[i];
            ast_ptr_t newRefNode = createAstNode<NRefASTLoad>(ident);
            ast_ptr_t linkNode = createAstNode<LinkASTLoad>();
            const auto indexValue = std::make_shared<PrimValue<int32_t>>(i);
            ast_ptr_t funcNode = indexFuncDeRefNode;
            *linkNode << exprNode << funcNode;
            ast_ptr_t copyNode = createAstNode<LinkASTLoad>();
            ast_ptr_t copyFunc = copyFuncDeRefNode;
            *copyNode << linkNode << copyFunc;
            *newRefNode << copyNode;
            *execNode << newRefNode;
        }
        return execNode;
    } break;

    case 3: // bracketIdents
    {
        if (type) {
            const auto &token = context->getStart();
            throw BuildException("Type cannot be specified for multiple identifiers", token);
        }
        const std::vector<std::string> &idents = std::any_cast<std::vector<std::string>>(carrier);
        ast_ptr_t execNode = createAstNode<ExecASTLoad>();
        for (size_t i = 0; i < idents.size(); i++) {
            const std::string &ident = idents[i];
            ast_ptr_t newRefNode = createAstNode<NRefASTLoad>(ident);
            ast_ptr_t linkNode = createAstNode<LinkASTLoad>();
            const auto indexValue = std::make_shared<StringValue>(ident);
            ast_ptr_t funcNode = indexFuncDeRefNode;
            *linkNode << exprNode << funcNode;
            ast_ptr_t copyNode = createAstNode<LinkASTLoad>();
            ast_ptr_t copyFunc = copyFuncDeRefNode;
            *copyNode << linkNode << copyFunc;
            *newRefNode << copyNode;
            *execNode << newRefNode;
        }
        return execNode;
    } break;

    default:
        throw std::runtime_error("Unknown carrier type");
    }
};

/*
useStmt : USE carrier '='? entityExpr
        | carrier '::' entityExpr ;
*/
std::any ASTConstructor::visitUseStmt(OpenCMLParser::UseStmtContext *context) {
    debug(0) << "visitUseStmt" << std::endl;
    const auto &[carrierType, carrier] = std::any_cast<std::pair<size_t, std::any>>(visitCarrier(context->carrier()));
    const auto &entityExpr = context->entityExpr();
    ast_ptr_t dataNode = std::any_cast<ast_ptr_t>(visitEntityExpr(entityExpr));
    switch (carrierType) {
    case 1: // identRef
    {
        const std::string &ident = std::any_cast<std::string>(carrier);
        ast_ptr_t newRefNode = createAstNode<NRefASTLoad>(ident);
        *newRefNode << dataNode;
        return newRefNode;
    } break;

    case 2: // bracedIdents
    {
        const std::vector<std::string> &idents = std::any_cast<std::vector<std::string>>(carrier);
        ast_ptr_t useNode = createAstNode<ExecASTLoad>();
        bool dangling = false;
        auto [exprValue, dang] = extractValue(dataNode, useNode, dangling);
        for (size_t i = 0; i < idents.size(); i++) {
            const std::string &ident = idents[i];
            ast_ptr_t newRefNode = createAstNode<NRefASTLoad>(ident);
            ast_ptr_t linkNode = createAstNode<LinkASTLoad>();

            const auto indexValue = std::make_shared<StringValue>(ident);
            const auto listValue = std::make_shared<ListValue>(data_list_t{exprValue, indexValue});

            ast_ptr_t argsNode = createAstNode<DataASTLoad>(listValue, dang ? data_list_t{exprValue} : data_list_t{});
            ast_ptr_t funcNode = indexFuncDeRefNode;

            *linkNode << argsNode << funcNode;
            *newRefNode << linkNode;

            *useNode << newRefNode;
        }
        return useNode;
    } break;

    case 3: // bracketIdents
    {
        const std::vector<std::string> &idents = std::any_cast<std::vector<std::string>>(carrier);
        ast_ptr_t useNode = createAstNode<ExecASTLoad>();
        bool dangling = false;
        auto [exprValue, dang] = extractValue(dataNode, useNode, dangling);
        for (size_t i = 0; i < idents.size(); i++) {
            const std::string &ident = idents[i];
            ast_ptr_t newRefNode = createAstNode<NRefASTLoad>(ident);
            ast_ptr_t linkNode = createAstNode<LinkASTLoad>();

            const auto indexValue = std::make_shared<PrimValue<int32_t>>(i);
            const auto listValue = std::make_shared<ListValue>(data_list_t{exprValue, indexValue});

            ast_ptr_t argsNode = createAstNode<DataASTLoad>(listValue, dang ? data_list_t{exprValue} : data_list_t{});
            ast_ptr_t funcNode = indexFuncDeRefNode;

            *linkNode << argsNode << funcNode;
            *newRefNode << linkNode;

            *useNode << newRefNode;
        }
        return useNode;
    } break;

    default:
        throw std::runtime_error("Unknown carrier type");
    }
};

/*
typeStmt : TYPE identRef '='? typeExpr ;
*/
std::any ASTConstructor::visitTypeStmt(OpenCMLParser::TypeStmtContext *context) {
    debug(0) << "visitTypeStmt" << std::endl;
    const std::string &ident = context->identRef()->getText();
    type_ptr_t type = std::any_cast<type_ptr_t>(visitTypeExpr(context->typeExpr()));
    typeScope_->insert(ident, type);
    return createAstNode<TypeASTLoad>(type);
};

/*
exprStmt : annotations? entityExpr ;
*/
std::any ASTConstructor::visitExprStmt(OpenCMLParser::ExprStmtContext *context) {
    debug(0) << "visitExprStmt" << std::endl;
    return visitEntityExpr(context->entityExpr());
};

/*
assignStmt : identRef memberAccess* '=' entityExpr ;
*/
std::any ASTConstructor::visitAssignStmt(OpenCMLParser::AssignStmtContext *context) {
    debug(0) << "visitAssignStmt" << std::endl;
    const std::string &ident = context->identRef()->getText();
    ast_ptr_t targetNode = nullptr;
    bool dangling = false;
    ast_ptr_t execNode = createAstNode<ExecASTLoad>();
    for (const auto &member : context->memberAccess()) {
        ast_ptr_t memberNode = std::any_cast<ast_ptr_t>(visitMemberAccess(member));
        ast_ptr_t linkNode = createAstNode<LinkASTLoad>();
        data_ptr_t targetValue = nullptr;
        bool targetDang = true;
        if (targetNode) {
            std::tie(targetValue, targetDang) = extractValue(targetNode, execNode, dangling);
        } else {
            targetValue = std::make_shared<DanglingValue>(ident);
        }
        auto [indexValue, indexDang] = extractValue(memberNode, execNode, dangling);
        const auto listValue = std::make_shared<ListValue>(data_list_t{targetValue, indexValue});
        const auto dataNode = // TODO: Find a better way to handle this
            createAstNode<DataASTLoad>(listValue, targetDang && indexDang ? data_list_t{targetValue, indexValue}
                                               : targetDang            ? data_list_t{targetValue}
                                               : indexDang             ? data_list_t{indexValue}
                                                                       : data_list_t{});
        ast_ptr_t funcNode = indexFuncDeRefNode;
        *linkNode << dataNode << funcNode;
        targetNode = linkNode;
    }
    if (!targetNode) {
        targetNode = createAstNode<DRefASTLoad>(ident);
    }
    if (dangling) {
        *execNode << targetNode;
        targetNode = execNode;
    }
    ast_ptr_t dataNode = std::any_cast<ast_ptr_t>(visitEntityExpr(context->entityExpr()));
    ast_ptr_t assignNode = createAstNode<AssnASTLoad>();
    *assignNode << targetNode << dataNode;
    return assignNode;
};

/*
withDef : WITH angledParams ;
*/
std::any ASTConstructor::visitWithDef(OpenCMLParser::WithDefContext *context) {
    debug(0) << "visitWithDef" << std::endl;
    return visitAngledParams(context->angledParams());
};

/*
funcDef : annotations? withDef? modifiers? FUNC identRef parentParams (':' typeExpr)? bracedStmts ;
*/
std::any ASTConstructor::visitFuncDef(OpenCMLParser::FuncDefContext *context) {
    debug(0) << "visitFuncDef" << std::endl;
    // TODO: Implement annotations
    const std::string ident = std::any_cast<std::string>(visitIdentRef(context->identRef()));
    std::shared_ptr<FunctorType> funcType = nullptr;
    const auto withType = std::make_shared<NamedTupleType>();
    const auto paramsType = std::make_shared<NamedTupleType>();
    const auto &withDef = context->withDef();
    if (withDef) {
        const auto &pairedParams =
            std::any_cast<std::vector<std::tuple<std::string, type_ptr_t, data_ptr_t>>>(visitWithDef(withDef));
        for (const auto &[name, type, value] : pairedParams) {
            withType->add(name, type, value);
        }
    }
    const auto &params = std::any_cast<std::vector<std::tuple<std::string, type_ptr_t, data_ptr_t>>>(
        visitParentParams(context->parentParams()));
    for (const auto &[name, type, value] : params) {
        paramsType->add(name, type, value);
    }
    const auto &typeExpr = context->typeExpr();
    if (typeExpr) {
        const auto returnType = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr));
        funcType = std::make_shared<FunctorType>(withType, paramsType, returnType);
    } else {
        funcType = std::make_shared<FunctorType>(withType, paramsType, anyTypePtr);
    }
    const auto &modifiers = context->modifiers();
    if (modifiers) {
        const auto &modSet = std::any_cast<std::set<FunctionModifier>>(visitModifiers(modifiers));
        funcType->setModifiers(modSet);
    }
    const auto funcTypeNode = createAstNode<TypeASTLoad>(std::dynamic_pointer_cast<Type>(funcType));
    const auto funcNode = createAstNode<FuncASTLoad>();
    *funcNode << funcTypeNode << std::any_cast<ast_ptr_t>(visitBracedStmts(context->bracedStmts()));

    ast_ptr_t newRefNode = createAstNode<NRefASTLoad>(ident);
    *newRefNode << funcNode;

    return newRefNode;
};

/*
retStmt : RETURN entityExpr? ;
*/
std::any ASTConstructor::visitRetStmt(OpenCMLParser::RetStmtContext *context) {
    debug(0) << "visitRetStmt" << std::endl;
    ast_ptr_t retNode = createAstNode<RetnASTLoad>();
    if (context->entityExpr()) {
        *retNode << std::any_cast<ast_ptr_t>(visitEntityExpr(context->entityExpr()));
    }
    return retNode;
};

/*
lambdaExpr : modifiers? ((parentParams (':' typeExpr)? '=>' (bracedStmts | entityExpr)) | '{' stmtList '}' ) ;
*/
std::any ASTConstructor::visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) {
    debug(0) << "visitLambdaExpr" << std::endl;
    std::shared_ptr<FunctorType> funcType = nullptr;
    ast_ptr_t bodyNode = nullptr;
    const auto &stmtList = context->stmtList();
    if (stmtList) {
        bodyNode = std::any_cast<ast_ptr_t>(visitStmtList(stmtList));
        funcType = std::make_shared<FunctorType>(nullptr, nullptr, anyTypePtr);
    } else {
        const auto &params = std::any_cast<std::vector<std::tuple<std::string, type_ptr_t, data_ptr_t>>>(
            visitParentParams(context->parentParams()));
        const auto paramsType = std::make_shared<NamedTupleType>();
        for (const auto &[name, type, value] : params) {
            paramsType->add(name, type, value);
        }
        const auto &typeExpr = context->typeExpr();
        if (typeExpr) {
            const auto returnType = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr));
            funcType = std::make_shared<FunctorType>(nullptr, paramsType, returnType);
        } else {
            funcType = std::make_shared<FunctorType>(nullptr, paramsType, anyTypePtr);
        }
        const auto &stmts = context->bracedStmts();
        if (stmts) {
            bodyNode = std::any_cast<ast_ptr_t>(visitBracedStmts(stmts));
        } else {
            ast_ptr_t exprNode = std::any_cast<ast_ptr_t>(visitEntityExpr(context->entityExpr()));
            ast_ptr_t returnNode = createAstNode<RetnASTLoad>();
            ast_ptr_t execNode = createAstNode<ExecASTLoad>();
            *returnNode << exprNode;
            *execNode << returnNode;
            bodyNode = execNode;
        }
    }
    const auto &modifiers = context->modifiers();
    if (modifiers) {
        const auto &modSet = std::any_cast<std::set<FunctionModifier>>(visitModifiers(modifiers));
        funcType->setModifiers(modSet);
    }
    const auto funcTypeNode = createAstNode<TypeASTLoad>(funcType);
    const auto funcNode = createAstNode<FuncASTLoad>();
    *funcNode << funcTypeNode << bodyNode;
    return funcNode;
};

/*
carrier : identRef | bracedIdents | bracketIdents ;
*/
std::any ASTConstructor::visitCarrier(OpenCMLParser::CarrierContext *context) {
    debug(0) << "visitCarrier" << std::endl;
    const size_t alt = context->getAltNumber();
    switch (alt) {
    case 1:
        return std::make_pair(alt, visitIdentRef(context->identRef()));
        break;
    case 2:
        return std::make_pair(alt, visitBracedIdents(context->bracedIdents()));
        break;
    case 3:
        return std::make_pair(alt, visitBracketIdents(context->bracketIdents()));
        break;

    default:
        throw std::runtime_error("Unknown carrier type");
    }
};

/*
annotation  : '@' primEntity ;
*/
std::any ASTConstructor::visitAnnotation(OpenCMLParser::AnnotationContext *context) {
    debug(0) << "visitAnnotation" << std::endl;
    // TODO: Implement visitAnnotation
    return visitPrimEntity(context->primEntity());
};

/*
annotations : (annotation SEP?)+ ;
*/
std::any ASTConstructor::visitAnnotations(OpenCMLParser::AnnotationsContext *context) {
    debug(0) << "visitAnnotations" << std::endl;
    // TODO: Implement visitAnnotations
    ast_ptr_t execNode = createAstNode<ExecASTLoad>();
    for (const auto &annotation : context->annotation()) {
        *execNode << std::any_cast<ast_ptr_t>(visitAnnotation(annotation));
    }
    return execNode;
};

/*
modifiers   : (INNER | OUTER | ATOMIC | STATIC | SYNC)+ ;
*/
std::any ASTConstructor::visitModifiers(OpenCMLParser::ModifiersContext *context) {
    debug(0) << "visitModifiers" << std::endl;
    std::set<FunctionModifier> modifiers;
    for (const auto &mod : context->children) {
        modifiers.insert(str2modifier(mod->getText()));
    }
    // TODO: use std::move to transfer the ownership of modifiers to the caller
    return modifiers;
};

/*
keyTypePair  : identRef ':' typeExpr ;
*/
std::any ASTConstructor::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) {
    debug(0) << "visitKeyTypePair" << std::endl;
    std::string key = std::any_cast<std::string>(visitIdentRef(context->identRef()));
    return std::make_pair(key, std::any_cast<type_ptr_t>(visitTypeExpr(context->typeExpr())));
};

/*
keyValuePair : identRef ':' entityExpr ;
*/
std::any ASTConstructor::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {
    debug(0) << "visitKeyValuePair" << std::endl;
    return std::make_pair(context->identRef()->getText(),
                          std::any_cast<ast_ptr_t>(visitEntityExpr(context->entityExpr())));
};

/*
keyParamPair : identRef annotation? ':' typeExpr ('=' entityExpr)? ;
*/
std::any ASTConstructor::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {
    debug(0) << "visitKeyParamPair" << std::endl;
    type_ptr_t type = std::any_cast<type_ptr_t>(visitTypeExpr(context->typeExpr()));
    data_ptr_t defaultValue = nullptr;
    if (context->entityExpr()) {
        const auto defaultNode = std::any_cast<ast_ptr_t>(visitEntityExpr(context->entityExpr()));
        defaultValue = extractStaticValue(defaultNode);
        if (!defaultValue) {
            const auto &exprToken = context->entityExpr()->getStart();
            // TODO shouldn't throw exception here
            throw BuildException("Default value must not be expressions", exprToken);
        }
    }
    // TODO: implement the support for annotation
    return std::make_tuple(context->identRef()->getText(), type, defaultValue);
};

/*
typeList     : typeExpr (',' typeExpr)* ;
*/
std::any ASTConstructor::visitTypeList(OpenCMLParser::TypeListContext *context) {
    debug(0) << "visitTypeList" << std::endl;
    std::vector<type_ptr_t> typeList;
    for (const auto &type : context->typeExpr()) {
        typeList.push_back(std::any_cast<type_ptr_t>(visitTypeExpr(type)));
    }
    return typeList;
};

/*
identList    : identRef (',' identRef)* ;
*/
std::any ASTConstructor::visitIdentList(OpenCMLParser::IdentListContext *context) {
    debug(0) << "visitIdentList" << std::endl;
    std::vector<std::string> identList;
    for (const auto &ident : context->identRef()) {
        identList.push_back(ident->getText());
    }
    return identList;
};

/*
valueList    : entityExpr (',' entityExpr)* ;
*/
std::any ASTConstructor::visitValueList(OpenCMLParser::ValueListContext *context) {
    debug(0) << "visitValueList" << std::endl;
    std::vector<ast_ptr_t> valueList;
    for (const auto &value : context->entityExpr()) {
        valueList.push_back(std::any_cast<ast_ptr_t>(visitEntityExpr(value)));
    }
    return valueList;
};

/*
pairedTypes  : keyTypePair (',' keyTypePair)* ;
*/
std::any ASTConstructor::visitPairedTypes(OpenCMLParser::PairedTypesContext *context) {
    debug(0) << "visitPairedTypes" << std::endl;
    std::vector<std::pair<std::string, type_ptr_t>> pairedTypes;
    for (const auto &pair : context->keyTypePair()) {
        pairedTypes.push_back(std::any_cast<std::pair<std::string, type_ptr_t>>(visitKeyTypePair(pair)));
    }
    return pairedTypes;
};

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
*/
std::any ASTConstructor::visitPairedValues(OpenCMLParser::PairedValuesContext *context) {
    debug(0) << "visitPairedValues" << std::endl;
    std::vector<std::pair<std::string, ast_ptr_t>> pairedValues;
    for (const auto &pair : context->keyValuePair()) {
        pairedValues.push_back(std::any_cast<std::pair<std::string, ast_ptr_t>>(visitKeyValuePair(pair)));
    }
    return pairedValues;
};

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
std::any ASTConstructor::visitPairedParams(OpenCMLParser::PairedParamsContext *context) {
    debug(0) << "visitPairedParams" << std::endl;
    std::vector<std::tuple<std::string, type_ptr_t, data_ptr_t>> pairedParams;
    for (const auto &pair : context->keyParamPair()) {
        pairedParams.push_back(
            std::any_cast<std::tuple<std::string, type_ptr_t, data_ptr_t>>(visitKeyParamPair(pair)));
    }
    return pairedParams;
};

/*
argumentList : valueList (',' pairedValues)? | pairedValues ;
*/
std::any ASTConstructor::visitArgumentList(OpenCMLParser::ArgumentListContext *context) {
    debug(0) << "visitArgumentList" << std::endl;
    const auto &valueList = context->valueList();
    const auto &pairedValues = context->pairedValues();
    std::vector<ast_ptr_t> indexArgs;
    if (valueList) {
        indexArgs = std::any_cast<std::vector<ast_ptr_t>>(visitValueList(valueList));
    }
    std::vector<std::pair<std::string, ast_ptr_t>> namedArgs;
    if (pairedValues) {
        namedArgs = std::any_cast<std::vector<std::pair<std::string, ast_ptr_t>>>(visitPairedValues(pairedValues));
    }
    return std::make_pair(indexArgs, namedArgs);
};

/*
bracedPairedValues : '{' pairedValues? ','? '}' ;
*/
std::any ASTConstructor::visitBracedPairedValues(OpenCMLParser::BracedPairedValuesContext *context) {
    debug(0) << "visitBracedPairedValues" << std::endl;
    const auto &pairedValues = context->pairedValues();
    if (pairedValues) {
        return visitPairedValues(pairedValues);
    } else {
        return std::vector<std::pair<std::string, ast_ptr_t>>();
    }
};

/*
bracedIdents       : '{' identList? ','? '}' ;
*/
std::any ASTConstructor::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {
    debug(0) << "visitBracedIdents" << std::endl;
    const auto &identList = context->identList();
    if (identList) {
        return visitIdentList(identList);
    } else {
        return std::vector<std::string>();
    }
};

/*
bracedStmts        : '{' stmtList? '}' ;
*/
std::any ASTConstructor::visitBracedStmts(OpenCMLParser::BracedStmtsContext *context) {
    debug(0) << "visitBracedStmts" << std::endl;
    if (context->stmtList()) {
        return visitStmtList(context->stmtList());
    } else {
        return createAstNode<ExecASTLoad>();
    }
};

/*
bracketIdents : '[' identList? ','? ']' ;
*/
std::any ASTConstructor::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {
    debug(0) << "visitBracketIdents" << std::endl;
    const auto &identList = context->identList();
    if (identList) {
        return visitIdentList(identList);
    } else {
        return std::vector<std::string>();
    }
};

/*
bracketValues : '[' valueList? ','? ']' ;
*/
std::any ASTConstructor::visitBracketValues(OpenCMLParser::BracketValuesContext *context) {
    debug(0) << "visitBracketValues" << std::endl;
    const auto &valueList = context->valueList();
    if (valueList) {
        return visitValueList(valueList);
    } else {
        return std::vector<ast_ptr_t>();
    }
};

/*
parentParams : '(' pairedParams? ','? ')' ;
*/
std::any ASTConstructor::visitParentParams(OpenCMLParser::ParentParamsContext *context) {
    debug(0) << "visitParentParams" << std::endl;
    const auto &pairedParams = context->pairedParams();
    if (pairedParams) {
        return visitPairedParams(pairedParams);
    } else {
        return std::vector<std::tuple<std::string, type_ptr_t, data_ptr_t>>();
    }
};

/*
parentValues : '(' argumentList? ','? ')' ;
*/
std::any ASTConstructor::visitParentValues(OpenCMLParser::ParentValuesContext *context) {
    debug(0) << "visitParentValues" << std::endl;
    const auto &argumentList = context->argumentList();
    if (argumentList) {
        return visitArgumentList(argumentList);
    } else {
        return std::make_pair(std::vector<ast_ptr_t>(), std::vector<std::pair<std::string, ast_ptr_t>>());
    }
};

/*
angledParams : '<' pairedParams? ','? '>' ;
*/
std::any ASTConstructor::visitAngledParams(OpenCMLParser::AngledParamsContext *context) {
    debug(0) << "visitAngledParams" << std::endl;
    const auto &pairedParams = context->pairedParams();
    if (pairedParams) {
        return visitPairedParams(pairedParams);
    } else {
        return std::vector<std::tuple<std::string, type_ptr_t, data_ptr_t>>();
    }
};

/*
angledValues : '<' argumentList? ','? '>' ;
*/
std::any ASTConstructor::visitAngledValues(OpenCMLParser::AngledValuesContext *context) {
    debug(0) << "visitAngledValues" << std::endl;
    const auto &argumentList = context->argumentList();
    if (argumentList) {
        return visitArgumentList(argumentList);
    } else {
        return std::make_pair(std::vector<ast_ptr_t>(), std::vector<std::pair<std::string, ast_ptr_t>>());
    }
};

/*
primEntity
    : identRef
    | literal
    | bracketValues
    | bracedPairedValues
    | lambdaExpr
    | '(' entityExpr ')' ;
*/
std::any ASTConstructor::visitPrimEntity(OpenCMLParser::PrimEntityContext *context) {
    debug(0) << "visitPrimEntity: " << context->getAltNumber() << std::endl;
    switch (context->getAltNumber()) {
    case 1: {
        const std::string &ident = std::any_cast<std::string>(visitIdentRef(context->identRef()));
        return createAstNode<DRefASTLoad>(ident);
    } break;
    case 2:
        return visitLiteral(context->literal());
        break;
    case 3: {
        const std::vector<ast_ptr_t> &values =
            std::any_cast<std::vector<ast_ptr_t>>(visitBracketValues(context->bracketValues()));
        const auto &listValue = std::make_shared<ListValue>();
        data_vec_t danglingValues;
        bool dangling = false;
        ast_ptr_t execNode = createAstNode<ExecASTLoad>();
        for (const auto &node : values) {
            auto [value, dang] = extractValue(node, execNode, dangling);
            listValue->add(value);
            if (dang) {
                danglingValues.push_back(value);
            }
        }
        const auto &dataNode = createAstNode<DataASTLoad>(listValue, std::move(danglingValues));
        if (dangling) {
            *execNode << dataNode;
            return execNode;
        } else {
            return dataNode;
        }
    } break;
    case 4: {
        const std::vector<std::pair<std::string, ast_ptr_t>> &values =
            std::any_cast<std::vector<std::pair<std::string, ast_ptr_t>>>(
                visitBracedPairedValues(context->bracedPairedValues()));
        const auto &dictValue = std::make_shared<DictValue>();
        data_vec_t danglingValues;
        bool dangling = false;
        ast_ptr_t execNode = createAstNode<ExecASTLoad>();
        for (const auto &[key, node] : values) {
            auto [value, dang] = extractValue(node, execNode, dangling);
            dictValue->add(key, value);
            if (dang) {
                danglingValues.push_back(value);
            }
        }
        const auto &dataNode = createAstNode<DataASTLoad>(dictValue, std::move(danglingValues));
        if (dangling) {
            *execNode << dataNode;
            return execNode;
        } else {
            return dataNode;
        }
    } break;
    case 5:
        return visitLambdaExpr(context->lambdaExpr());
        break;
    case 6:
        return visitEntityExpr(context->entityExpr());
        break;

    default:
        throw std::runtime_error("Unknown primary entity type");
    }
};

/*
memberAccess : '[' entityExpr ']' ;
*/
std::any ASTConstructor::visitMemberAccess(OpenCMLParser::MemberAccessContext *context) {
    debug(0) << "visitMemberAccess" << std::endl;
    return visitEntityExpr(context->entityExpr());
};

/*
entityChain  : entityLink+ ;
*/
std::any ASTConstructor::visitEntityChain(OpenCMLParser::EntityChainContext *context) {
    debug(0) << "visitEntityChain" << std::endl;
    const auto &entityLinks = context->entityLink();

    if (entityLinks.size() == 1) {
        return visitEntityLink(entityLinks[0]);
    }

    data_vec_t danglingValues;
    bool dangling = false;
    ast_ptr_t execNode = createAstNode<ExecASTLoad>();
    const auto listValue = std::make_shared<ListValue>();
    for (const auto &link : entityLinks) {
        ast_ptr_t linkNode = std::any_cast<ast_ptr_t>(visitEntityLink(link));
        auto [value, dang] = extractValue(linkNode, execNode, dangling);
        listValue->add(value);
        if (dang) {
            danglingValues.push_back(value);
        }
    }
    ast_ptr_t dataNode = createAstNode<DataASTLoad>(std::dynamic_pointer_cast<Data>(listValue));
    ast_ptr_t linkNode = createAstNode<LinkASTLoad>();
    ast_ptr_t funcNode = chainFuncDeRefNode;

    if (dangling) {
        *execNode << dataNode;
        *linkNode << execNode << funcNode;
        return linkNode;
    } else {
        *linkNode << dataNode << funcNode;
        return linkNode;
    }
};

/*
entityLink   : entityUnit | entityLink '->' entityUnit ;
*/
std::any ASTConstructor::visitEntityLink(OpenCMLParser::EntityLinkContext *context) {
    debug(0) << "visitEntityLink" << std::endl;
    if (context->children.size() == 1) {
        return visitEntityUnit(context->entityUnit());
    } else {
        ast_ptr_t linkNode = createAstNode<LinkASTLoad>();
        ast_ptr_t dataNode = std::any_cast<ast_ptr_t>(visitEntityLink(context->entityLink()));
        ast_ptr_t funcNode = std::any_cast<ast_ptr_t>(visitEntityUnit(context->entityUnit()));

        *linkNode << dataNode << funcNode;

        return linkNode;
    }
};

/*
entityUnit   : entityWith (({isAdjacent()}? (memberAccess | angledValues | parentValues)) | annotation)* ;
*/
std::any ASTConstructor::visitEntityUnit(OpenCMLParser::EntityUnitContext *context) {
    debug(0) << "visitEntityUnit" << std::endl;
    ast_ptr_t primEntity = std::any_cast<ast_ptr_t>(visitEntityWith(context->entityWith()));
    ast_ptr_t entityNode = primEntity;
    for (size_t i = 1; i < context->children.size(); i++) {
        bool dangling = false;
        ast_ptr_t execNode = createAstNode<ExecASTLoad>();
        const auto &child = context->children[i];
        // TODO: find a better way to determine the type of the child
        switch (child->getText()[0]) {
        case '[': // memberAccess
        {
            const auto &memberAccess = dynamic_cast<OpenCMLParser::MemberAccessContext *>(child);
            ast_ptr_t indexNode = std::any_cast<ast_ptr_t>(visitMemberAccess(memberAccess));
            ast_ptr_t linkNode = createAstNode<LinkASTLoad>();
            // TODO: inner function names can share the same deref node
            ast_ptr_t funcNode = indexFuncDeRefNode;
            auto listValue = std::make_shared<ListValue>();
            auto [targetValue, targetDang] = extractValue(entityNode, execNode, dangling);
            auto [indexValue, indexDang] = extractValue(indexNode, execNode, dangling);
            listValue->add(targetValue);
            listValue->add(indexValue);
            // TODO: Find a better way to handle this
            ast_ptr_t dataNode = createAstNode<DataASTLoad>(std::dynamic_pointer_cast<Data>(listValue),
                                                         targetDang && indexDang ? data_list_t{targetValue, indexValue}
                                                         : targetDang            ? data_list_t{targetValue}
                                                         : indexDang             ? data_list_t{indexValue}
                                                                                 : data_list_t{});
            *linkNode << dataNode << funcNode;
            if (dangling) {
                *execNode << linkNode;
                entityNode = execNode;
            } else {
                entityNode = linkNode;
            }
        } break;

        case '<': // angledValues
        {
            const auto &angledValues = dynamic_cast<OpenCMLParser::AngledValuesContext *>(child);
            const auto &[indexArgs, namedArgs] =
                std::any_cast<std::pair<std::vector<ast_ptr_t>, std::vector<std::pair<std::string, ast_ptr_t>>>>(
                    visitAngledValues(angledValues));
            ast_ptr_t withNode = createAstNode<WithASTLoad>();
            ast_ptr_t &funcNode = entityNode;
            auto namedTuple = std::make_shared<NamedTupleValue>();
            data_vec_t danglingValues;
            bool dangling = false;
            auto execNode = createAstNode<ExecASTLoad>();
            for (const auto &arg : indexArgs) {
                auto [value, dang] = extractValue(arg, execNode, dangling);
                namedTuple->add(value);
                if (dang) {
                    danglingValues.push_back(value);
                }
            }
            for (const auto &[key, arg] : namedArgs) {
                auto [value, dang] = extractValue(arg, execNode, dangling);
                namedTuple->add(value, key);
                if (dang) {
                    danglingValues.push_back(value);
                }
            }
            ast_ptr_t dataNode =
                createAstNode<DataASTLoad>(std::dynamic_pointer_cast<Data>(namedTuple), std::move(danglingValues));
            if (dangling) {
                *execNode << dataNode;
                *withNode << execNode << funcNode;
                entityNode = withNode;
            } else {
                *withNode << dataNode << funcNode;
                entityNode = withNode;
            }
        } break;

        case '(': // parentValues
        {
            const auto &parentValues = dynamic_cast<OpenCMLParser::ParentValuesContext *>(child);
            const auto &[indexArgs, namedArgs] =
                std::any_cast<std::pair<std::vector<ast_ptr_t>, std::vector<std::pair<std::string, ast_ptr_t>>>>(
                    visitParentValues(parentValues));
            ast_ptr_t linkNode = createAstNode<LinkASTLoad>();
            ast_ptr_t &funcNode = entityNode;
            auto namedTuple = std::make_shared<NamedTupleValue>();
            data_vec_t danglingValues;
            bool dangling = false;
            ast_ptr_t execNode = createAstNode<ExecASTLoad>();
            for (const auto &arg : indexArgs) {
                auto [value, dang] = extractValue(arg, execNode, dangling);
                namedTuple->add(value);
                if (dang) {
                    danglingValues.push_back(value);
                }
            }
            for (const auto &[key, arg] : namedArgs) {
                auto [value, dang] = extractValue(arg, execNode, dangling);
                namedTuple->add(value, key);
                if (dang) {
                    danglingValues.push_back(value);
                }
            }
            ast_ptr_t dataNode =
                createAstNode<DataASTLoad>(std::dynamic_pointer_cast<Data>(namedTuple), std::move(danglingValues));
            if (dangling) {
                *execNode << dataNode;
                *linkNode << execNode << funcNode;
                entityNode = linkNode;
            } else {
                *linkNode << dataNode << funcNode;
                entityNode = linkNode;
            }
        } break;

        case '@': // annotation
        {
            // TODO: Implement the support for annotation
            const auto &annotation = dynamic_cast<OpenCMLParser::AnnotationContext *>(child);
            ast_ptr_t annoNode = std::any_cast<ast_ptr_t>(visitAnnotation(annotation));
            ast_ptr_t linkNode = createAstNode<LinkASTLoad>();
            ast_ptr_t &funcNode = entityNode;
            *linkNode << annoNode << funcNode;
            entityNode = linkNode;
        } break;

        default:
            throw std::runtime_error("Unknown entity type");
        }
    }
    return entityNode;
};

/*
entityWith   : primEntity | entityWith '.' primEntity ;
*/
std::any ASTConstructor::visitEntityWith(OpenCMLParser::EntityWithContext *context) {
    debug(0) << "visitEntityWith" << std::endl;
    if (context->children.size() == 1) {
        return visitPrimEntity(context->primEntity());
    } else {
        ast_ptr_t withNode = createAstNode<WithASTLoad>();
        ast_ptr_t dataNode = std::any_cast<ast_ptr_t>(visitEntityWith(context->entityWith()));
        ast_ptr_t funcNode = std::any_cast<ast_ptr_t>(visitPrimEntity(context->primEntity()));

        *withNode << dataNode << funcNode;

        return withNode;
    }
};

/*
entityExpr
    : relaExpr
    | entityExpr '+=' relaExpr
    | entityExpr '-=' relaExpr
    | entityExpr '*=' relaExpr
    | entityExpr '/=' relaExpr
    | entityExpr '%=' relaExpr
    | entityExpr '^=' relaExpr
    | entityExpr '&=' relaExpr
    | entityExpr '|=' relaExpr
    ;
*/
std::any ASTConstructor::visitEntityExpr(OpenCMLParser::EntityExprContext *context) {
    debug(0) << "visitEntityExpr" << std::endl;
    const auto &relaExpr = visitRelaExpr(context->relaExpr());
    if (context->children.size() == 1) {
        return relaExpr;
    } else {
        ast_ptr_t linkNode = createAstNode<LinkASTLoad>();

        bool dangling = false;
        ast_ptr_t execNode = createAstNode<ExecASTLoad>();

        ast_ptr_t entityNode = std::any_cast<ast_ptr_t>(visitEntityExpr(context->entityExpr()));
        auto [entityValue, entityDang] = extractValue(entityNode, execNode, dangling);
        ast_ptr_t relaNode = std::any_cast<ast_ptr_t>(relaExpr);
        auto [relaValue, relaDang] = extractValue(relaNode, execNode, dangling);

        const auto listValue = std::make_shared<ListValue>(data_list_t{entityValue, relaValue});

        ast_ptr_t dataNode =
            createAstNode<DataASTLoad>(listValue, entityDang && relaDang ? data_list_t{entityValue, relaValue}
                                               : entityDang           ? data_list_t{entityValue}
                                                                      : data_list_t{relaValue});

        ast_ptr_t funcNode = nullptr;
        const auto &op = context->children[1]->getText();
        if (op == "+=") {
            funcNode = iAddFuncDeRefNode;
        } else if (op == "-=") {
            funcNode = iSubFuncDeRefNode;
        } else if (op == "*=") {
            funcNode = iMulFuncDeRefNode;
        } else if (op == "/=") {
            funcNode = iDivFuncDeRefNode;
        } else if (op == "%=") {
            funcNode = iModFuncDeRefNode;
        } else if (op == "^=") {
            funcNode = iPowFuncDeRefNode;
        } else if (op == "&=") {
            funcNode = iInterFuncDeRefNode;
        } else if (op == "|=") {
            funcNode = iUnionFuncDeRefNode;
        } else {
            throw std::runtime_error("Unknown operator: " + op);
        }

        if (dangling) {
            *execNode << dataNode;
            *linkNode << execNode << funcNode;
            return linkNode;
        } else {
            *linkNode << dataNode << funcNode;
            return linkNode;
        }
    }
};

/*
relaExpr
    : addExpr
    | relaExpr '<' addExpr
    | relaExpr '>' addExpr
    | relaExpr '<=' addExpr
    | relaExpr '>=' addExpr
    | relaExpr '==' addExpr
    | relaExpr '!=' addExpr
    | relaExpr '&&' addExpr
    | relaExpr '||' addExpr
    ;
*/
std::any ASTConstructor::visitRelaExpr(OpenCMLParser::RelaExprContext *context) {
    debug(0) << "visitRelaExpr" << std::endl;
    const auto &addExpr = visitAddExpr(context->addExpr());
    if (context->children.size() == 1) {
        return addExpr;
    } else {
        ast_ptr_t linkNode = createAstNode<LinkASTLoad>();

        bool dangling = false;
        ast_ptr_t execNode = createAstNode<ExecASTLoad>();

        ast_ptr_t relaNode = std::any_cast<ast_ptr_t>(visitRelaExpr(context->relaExpr()));
        auto [relaValue, relaDang] = extractValue(relaNode, execNode, dangling);
        ast_ptr_t addNode = std::any_cast<ast_ptr_t>(addExpr);
        auto [addValue, addDang] = extractValue(addNode, execNode, dangling);

        const auto listValue = std::make_shared<ListValue>(data_list_t{relaValue, addValue});

        ast_ptr_t dataNode = createAstNode<DataASTLoad>(listValue, relaDang && addDang ? data_list_t{relaValue, addValue}
                                                                : relaDang          ? data_list_t{relaValue}
                                                                                    : data_list_t{addValue});

        ast_ptr_t funcNode = nullptr;
        const auto &op = context->children[1]->getText();
        if (op == "<") {
            funcNode = ltFuncDeRefNode;
        } else if (op == ">") {
            funcNode = gtFuncDeRefNode;
        } else if (op == "<=") {
            funcNode = leFuncDeRefNode;
        } else if (op == ">=") {
            funcNode = geFuncDeRefNode;
        } else if (op == "==") {
            funcNode = eqFuncDeRefNode;
        } else if (op == "!=") {
            funcNode = neFuncDeRefNode;
        } else if (op == "&&") {
            funcNode = andFuncDeRefNode;
        } else if (op == "||") {
            funcNode = orFuncDeRefNode;
        } else {
            throw std::runtime_error("Unknown operator: " + op);
        }

        if (dangling) {
            *execNode << dataNode;
            *linkNode << execNode << funcNode;
            return linkNode;
        } else {
            *linkNode << dataNode << funcNode;
            return linkNode;
        }
    }
};

/*
addExpr
    : multiExpr
    | addExpr '+' multiExpr
    | addExpr '-' multiExpr
    | addExpr '&' multiExpr
    | addExpr '|' multiExpr
    ;
*/
std::any ASTConstructor::visitAddExpr(OpenCMLParser::AddExprContext *context) {
    debug(0) << "visitAddExpr" << std::endl;
    const auto &multiExpr = visitMultiExpr(context->multiExpr());
    if (context->children.size() == 1) {
        return multiExpr;
    } else {
        ast_ptr_t linkNode = createAstNode<LinkASTLoad>();

        bool dangling = false;
        ast_ptr_t execNode = createAstNode<ExecASTLoad>();

        ast_ptr_t addNode = std::any_cast<ast_ptr_t>(visitAddExpr(context->addExpr()));
        auto [addValue, addDang] = extractValue(addNode, execNode, dangling);
        ast_ptr_t multiNode = std::any_cast<ast_ptr_t>(multiExpr);
        auto [multiValue, multiDang] = extractValue(multiNode, execNode, dangling);

        const auto listValue = std::make_shared<ListValue>(data_list_t{addValue, multiValue});

        ast_ptr_t dataNode = createAstNode<DataASTLoad>(listValue);

        ast_ptr_t funcNode = nullptr;
        const auto &op = context->children[1]->getText();
        if (op == "+") {
            funcNode = addFuncDeRefNode;
        } else if (op == "-") {
            funcNode = subFuncDeRefNode;
        } else if (op == "&") {
            funcNode = interFuncDeRefNode;
        } else if (op == "|") {
            funcNode = unionFuncDeRefNode;
        } else {
            throw std::runtime_error("Unknown operator: " + op);
        }

        if (dangling) {
            *execNode << dataNode;
            *linkNode << execNode << funcNode;
            return linkNode;
        } else {
            *linkNode << dataNode << funcNode;
            return linkNode;
        }
    }
};

/*
multiExpr
    : unaryExpr
    | multiExpr '^' unaryExpr
    | multiExpr '*' unaryExpr
    | multiExpr '/' unaryExpr
    | multiExpr '%' unaryExpr
    | multiExpr AS typeExpr
    | multiExpr IS typeExpr
    ;
*/
std::any ASTConstructor::visitMultiExpr(OpenCMLParser::MultiExprContext *context) {
    debug(0) << "visitMultiExpr: " << context->getAltNumber() << std::endl;
    const auto &unaryExpr = context->unaryExpr();
    ast_ptr_t unaryExprNode = nullptr;
    if (unaryExpr) {
        unaryExprNode = std::any_cast<ast_ptr_t>(visitUnaryExpr(unaryExpr));
    }
    if (context->children.size() == 1) {
        return unaryExprNode;
    } else {
        ast_ptr_t linkNode = createAstNode<LinkASTLoad>();
        ast_ptr_t funcNode = nullptr;
        ast_ptr_t dataNode = nullptr;

        const auto listValue = std::make_shared<ListValue>();

        bool dangling = false;
        ast_ptr_t execNode = createAstNode<ExecASTLoad>();
        const auto &multiNode = std::any_cast<ast_ptr_t>(visitMultiExpr(context->multiExpr()));
        auto [multiValue, multiDang] = extractValue(multiNode, execNode, dangling);
        listValue->add(multiValue);

        const auto &op = context->children[1]->getText();

        if (op == "as" || op == "is") {
            const auto &typeExpr = context->typeExpr();
            type_ptr_t type = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr));

            listValue->add(std::make_shared<NullValue>(type));
            dataNode = createAstNode<DataASTLoad>(std::dynamic_pointer_cast<Data>(listValue),
                                               multiDang ? data_list_t{multiValue} : data_list_t{});
            if (op == "as") {
                funcNode = castFuncDeRefNode;
            } else if (op == "is") {
                funcNode = typeFuncDeRefNode;
            } else {
                throw std::runtime_error("Unknown operator: " + op);
            }
        } else {
            auto [unaryValue, unaryDang] = extractValue(unaryExprNode, execNode, dangling);
            listValue->add(unaryValue);

            dataNode = createAstNode<DataASTLoad>(std::dynamic_pointer_cast<Data>(listValue),
                                               multiDang && unaryDang ? data_list_t{multiValue, unaryValue}
                                               : multiDang            ? data_list_t{multiValue}
                                               : unaryDang            ? data_list_t{unaryValue}
                                                                      : data_list_t{});

            if (op == "^") {
                funcNode = powFuncDeRefNode;
            } else if (op == "*") {
                funcNode = mulFuncDeRefNode;
            } else if (op == "/") {
                funcNode = divFuncDeRefNode;
            } else if (op == "%") {
                funcNode = modFuncDeRefNode;
            } else if (op == "as") {
                funcNode = castFuncDeRefNode;
            } else if (op == "is") {
                funcNode = typeFuncDeRefNode;
            } else {
                throw std::runtime_error("Unknown operator: " + op);
            }
        }

        if (dangling) {
            *execNode << dataNode;
            *linkNode << execNode << funcNode;
            return linkNode;
        } else {
            *linkNode << dataNode << funcNode;
            return linkNode;
        }
    }
};

/*
unaryExpr
    : primExpr
    | '!' primExpr
    | '~' primExpr
    ;
*/
std::any ASTConstructor::visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) {
    debug(0) << "visitUnaryExpr: " << context->getAltNumber() << std::endl;
    const auto &primExpr = visitPrimExpr(context->primExpr());
    if (context->children.size() == 1) {
        return primExpr;
    } else {
        ast_ptr_t linkNode = createAstNode<LinkASTLoad>();
        ast_ptr_t dataNode = std::any_cast<ast_ptr_t>(primExpr);
        ast_ptr_t funcNode = nullptr;
        const auto &op = context->children[0]->getText();
        if (op == "!") {
            funcNode = createAstNode<DRefASTLoad>("__not__");
        } else {
            funcNode = createAstNode<DRefASTLoad>("__neg__");
        }
        *linkNode << dataNode << funcNode;
        return linkNode;
    }
};

/*
primExpr
    : entityChain
    | '(' entityExpr ')'
    ;
*/
std::any ASTConstructor::visitPrimExpr(OpenCMLParser::PrimExprContext *context) {
    debug(0) << "visitPrimExpr" << std::endl;
    if (context->entityChain()) {
        return visitEntityChain(context->entityChain());
    } else {
        return visitEntityExpr(context->entityExpr());
    }
};

/*
literal
    : INTEGER UNIT?
    | REAL UNIT?
    | STRING
    | MULTI_STR
    | FSTRING
    | TRUE
    | FALSE
    | NULL
    ;
*/
std::any ASTConstructor::visitLiteral(OpenCMLParser::LiteralContext *context) {
    debug(0) << "visitLiteral: " << context->getAltNumber() << std::endl;
    data_ptr_t value = nullptr;

    switch (context->getAltNumber()) {
    case 1: // INTEGER UNIT?
        value = std::dynamic_pointer_cast<Data>(
            std::make_shared<PrimValue<int64_t>>(parseNumber<int64_t>(context->INTEGER()->getText())));
        break;
    case 2: // REAL UNIT?
        value = std::dynamic_pointer_cast<Data>(
            std::make_shared<PrimValue<double>>(parseNumber<double>(context->REAL()->getText())));
        break;
    case 3: // STRING
    {
        const auto &text = context->STRING()->getText();
        value = std::dynamic_pointer_cast<Data>(std::make_shared<StringValue>(text.substr(1, text.size() - 2)));
    } break;
    case 4: // MULTI_STR
    {
        const auto &text = context->MULTI_STR()->getText();
        value = std::dynamic_pointer_cast<Data>(std::make_shared<StringValue>(text.substr(3, text.size() - 6)));
    } break;
    case 5: // FSTRING
    {
        // TODO: Implement FSTRING
        const auto &text = context->FSTRING()->getText();
        value = std::dynamic_pointer_cast<Data>(std::make_shared<StringValue>(text.substr(2, text.size() - 3)));
    } break;
    case 6: // TRUE
        value = std::dynamic_pointer_cast<Data>(std::make_shared<PrimValue<bool>>(true));
        break;
    case 7: // FALSE
        value = std::dynamic_pointer_cast<Data>(std::make_shared<PrimValue<bool>>(false));
        break;
    case 8: // NULL
        value = std::dynamic_pointer_cast<Data>(std::make_shared<NullValue>());
        break;

    default:
        break;
    }

    return createAstNode<DataASTLoad>(value);
};

/*
typeExpr
    : unaryType
    | typeExpr '&' unaryType
    | typeExpr '|' unaryType
    ;
*/
std::any ASTConstructor::visitTypeExpr(OpenCMLParser::TypeExprContext *context) {
    debug(0) << "visitTypeExpr" << std::endl;
    type_ptr_t unaryType = std::any_cast<type_ptr_t>(visitUnaryType(context->unaryType()));
    if (context->children.size() == 1) { // unaryType
        return unaryType;
    } else {
        if (context->children[1]->getText() == "&") { // typeExpr '&' unaryType
            type_ptr_t lhs = std::any_cast<type_ptr_t>(visitTypeExpr(context->typeExpr()));
            type_ptr_t rhs = unaryType;
            if (lhs->code() != TypeCode::DICT) {
                const auto &token = context->getStart();
                throw BuildException("The left-hand side of '&' must be a dict type", token);
            }
            if (rhs->code() != TypeCode::DICT) {
                const auto &token = context->getStart();
                throw BuildException("The right-hand side of '&' must be a dict type", token);
            }
            return std::dynamic_pointer_cast<Type>(dynamic_cast<DictType &>(*lhs.get()) &
                                                   dynamic_cast<DictType &>(*rhs.get()));
        } else { // typeExpr '|' unaryType
            type_ptr_t lhs = std::any_cast<type_ptr_t>(visitTypeExpr(context->typeExpr()));
            type_ptr_t rhs = unaryType;
            if (lhs->code() == TypeCode::DICT && rhs->code() == TypeCode::DICT) {
                return std::dynamic_pointer_cast<Type>(dynamic_cast<DictType &>(*lhs.get()) |
                                                       dynamic_cast<DictType &>(*rhs.get()));
            }
            return std::dynamic_pointer_cast<Type>(std::make_shared<UnionType>(lhs, rhs));
        }
    }
    throw std::runtime_error("Unknown type expression");
};

/*
unaryType
    : atomType ('[' INTEGER? ']')*
    ;
*/
std::any ASTConstructor::visitUnaryType(OpenCMLParser::UnaryTypeContext *context) {
    debug(0) << "visitUnaryType" << std::endl;
    type_ptr_t atomType = std::any_cast<type_ptr_t>(visitAtomType(context->atomType()));
    for (size_t i = 1; i < context->children.size(); i++) {
        if (context->children[i]->getText() == "[") {
            const auto &size = context->children[i + 1]->getText();
            if (size == "]") {
                atomType = std::dynamic_pointer_cast<Type>(std::make_shared<ArrayType>(atomType));
            } else {
                atomType = std::dynamic_pointer_cast<Type>(std::make_shared<VectorType>(atomType, std::stoi(size)));
            }
        }
    }
    return atomType;
};

/*
atomType
    : primType
    | structType
    | specialType
    | identRef
    | '(' typeExpr ')'
    | lambdaType
    ;
*/
std::any ASTConstructor::visitAtomType(OpenCMLParser::AtomTypeContext *context) {
    debug(0) << "visitAtomType: " << context->getAltNumber() << std::endl;
    switch (context->getAltNumber()) {
    case 1: // primType
        return visitPrimType(context->primType());
        break;

    case 2: // structType
        return visitStructType(context->structType());
        break;

    case 3: // specialType
        return visitSpecialType(context->specialType());
        break;

    case 4: // identRef
    {
        const auto &ident = std::any_cast<std::string>(visitIdentRef(context->identRef()));
        const auto &type = typeScope_->at(ident);
        if (!type.has_value()) {
            const auto &token = context->getStart();
            throw BuildException("Type '" + ident + "' is not defined", token);
        }
        return type.value();
    } break;

    case 5: // '(' typeExpr ')'
        return visitTypeExpr(context->typeExpr());
        break;

    case 6: // lambdaType
        return visitLambdaType(context->lambdaType());
        break;

    default:
        throw std::runtime_error("Unknown atom type");
    }
};

/*
lambdaType
    : ('<' pairedParams? '>')? '(' pairedParams? ')' '=>' typeExpr
    ;
*/
std::any ASTConstructor::visitLambdaType(OpenCMLParser::LambdaTypeContext *context) {
    debug(0) << "visitLambdaType" << std::endl;
    const auto &pairedParamsList = context->pairedParams();
    const auto &typeExpr = context->typeExpr();
    std::shared_ptr<NamedTupleType> withType = nullptr;
    auto paramsType = std::make_shared<NamedTupleType>();
    type_ptr_t returnType = nullptr;
    if (pairedParamsList.size() > 1) {
        withType = std::make_shared<NamedTupleType>();
        const auto &superParams = std::any_cast<std::vector<std::tuple<std::string, type_ptr_t, ast_ptr_t>>>(
            visitPairedParams(pairedParamsList[0]));
        for (const auto &[name, type, valueNode] : superParams) {
            data_ptr_t value = extractStaticValue(valueNode);
            if (!valueNode) {
                const auto &token = context->getStart();
                throw BuildException("Default values for a functor must not be expressions", token);
            }
            withType->add(name, type, value);
        }
    }
    const auto &params = std::any_cast<std::vector<std::tuple<std::string, type_ptr_t, data_ptr_t>>>(
        visitPairedParams(pairedParamsList.back()));
    for (const auto &[name, type, value] : params) {
        paramsType->add(name, type, value);
    }
    returnType = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr));
    return std::dynamic_pointer_cast<Type>(std::make_shared<FunctorType>(withType, paramsType, returnType));
};

/*
primType
    : INTEGER_TYPE
    | INTEGER32_TYPE
    | INTEGER64_TYPE
    | REAL_TYPE
    | FLOAT_TYPE
    | DOUBLE_TYPE
    | NUMBER_TYPE
    | STRING_TYPE
    | BOOL_TYPE
    | CHAR_TYPE
    ;
*/
std::any ASTConstructor::visitPrimType(OpenCMLParser::PrimTypeContext *context) {
    debug(0) << "visitPrimType" << context->getAltNumber() << std::endl;
    const auto tokenType = context->getStart()->getType();
    switch (tokenType) {
    case OpenCMLLexer::INTEGER_TYPE: // INTEGER_TYPE
        return intTypePtr;
        break;
    case OpenCMLLexer::INTEGER32_TYPE: // INTEGER32_TYPE
        return int32TypePtr;
        break;
    case OpenCMLLexer::INTEGER64_TYPE: // INTEGER64_TYPE
        return int64TypePtr;
        break;
    case OpenCMLLexer::REAL_TYPE: // REAL_TYPE
        return realTypePtr;
        break;
    case OpenCMLLexer::FLOAT_TYPE: // FLOAT_TYPE
        return floatTypePtr;
        break;
    case OpenCMLLexer::DOUBLE_TYPE: // DOUBLE_TYPE
        return doubleTypePtr;
        break;
    case OpenCMLLexer::NUMBER_TYPE: // NUMBER_TYPE
        return numberTypePtr;
        break;
    case OpenCMLLexer::STRING_TYPE: // STRING_TYPE
        return stringTypePtr;
        break;
    case OpenCMLLexer::BOOL_TYPE: // BOOL_TYPE
        return boolTypePtr;
        break;
    case OpenCMLLexer::CHAR_TYPE: // CHAR_TYPE
        return charTypePtr;
        break;

    default:
        throw std::runtime_error("Unknown primitive type");
    }
};

/*
structType
    : SET_TYPE ('<' typeExpr '>')?
    | MAP_TYPE ('<' typeExpr ',' typeExpr '>')?
    | LIST_TYPE
    | DICT_TYPE // universal dict type
    | '{' pairedTypes? ','? '}' // concrete dict type
    | ARRAY_TYPE ('<' typeExpr '>')?
    | TUPLE_TYPE ('<' typeList? ','? '>')?
    | UNION_TYPE ('<' typeList? ','? '>')?
    | VECTOR_TYPE ('<' typeExpr (',' INTEGER)? '>')?
    | TENSOR_TYPE ('<' typeExpr (',' '[' INTEGER (',' INTEGER)* ']')? '>')?
    ;
*/
std::any ASTConstructor::visitStructType(OpenCMLParser::StructTypeContext *context) {
    debug(0) << "visitStructType: " << context->getAltNumber() << std::endl;
    switch (context->getAltNumber()) {
    case 1: // SET_TYPE ('<' typeExpr '>')?
    {
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() == 1) {
            const auto &type = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr[0]));
            const auto &setType = std::make_shared<SetType>(type);
            return std::dynamic_pointer_cast<Type>(setType);
        } else {
            // if no type is specified, use any type
            return std::dynamic_pointer_cast<Type>(std::make_shared<SetType>(anyTypePtr));
        }
    } break;

    case 2: // MAP_TYPE ('<' typeExpr ',' typeExpr '>')?
    {
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() == 2) {
            const auto &keyType = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr[0]));
            const auto &valueType = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr[1]));
            const auto &mapType = std::make_shared<MapType>(keyType, valueType);
            return std::dynamic_pointer_cast<Type>(mapType);
        } else {
            // if no type is specified, use any type
            return std::dynamic_pointer_cast<Type>(std::make_shared<MapType>(anyTypePtr, anyTypePtr));
        }
    } break;

    case 3: // LIST_TYPE
        return listTypePtr;
        break;

    case 4: // DICT_TYPE
        // TODO: Implement the support for universal dict type
        return std::dynamic_pointer_cast<Type>(std::make_shared<DictType>());
        break;

    case 5: // '{' pairedTypes? ','? '}'
    {
        const auto &pairedTypes = context->pairedTypes();
        if (pairedTypes) {
            const auto &types =
                std::any_cast<std::vector<std::pair<std::string, type_ptr_t>>>(visitPairedTypes(pairedTypes));
            auto dictType = std::make_shared<DictType>();
            for (const auto &[key, type] : types) {
                dictType->add(key, type);
            }
            return std::dynamic_pointer_cast<Type>(dictType);
        } else {
            return std::dynamic_pointer_cast<Type>(std::make_shared<DictType>());
        }
    } break;

    case 6: // ARRAY_TYPE ('<' typeExpr '>')?
    {
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() == 1) {
            const auto &type = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr[0]));
            const auto &arrayType = std::make_shared<ArrayType>(type);
            return std::dynamic_pointer_cast<Type>(arrayType);
        } else {
            // if no type is specified, use any type
            return std::dynamic_pointer_cast<Type>(std::make_shared<ArrayType>(anyTypePtr));
        }
    } break;

    case 7: // TUPLE_TYPE ('<' typeList? ','? '>')?
    {
        const auto &typeList = context->typeList();
        if (typeList) {
            const auto &types = std::any_cast<std::vector<type_ptr_t>>(visitTypeList(typeList));
            const auto &tupleType = std::make_shared<TupleType>(types);
            return std::dynamic_pointer_cast<Type>(tupleType);
        } else {
            return std::dynamic_pointer_cast<Type>(std::make_shared<TupleType>());
        }
    } break;

    case 8: // UNION_TYPE ('<' typeList? ','? '>')?
    {
        const auto &typeList = context->typeList();
        if (typeList) {
            const auto &types = std::any_cast<std::vector<type_ptr_t>>(visitTypeList(typeList));
            const auto &unionType = std::make_shared<UnionType>(types);
            return std::dynamic_pointer_cast<Type>(unionType);
        } else {
            return std::dynamic_pointer_cast<Type>(std::make_shared<UnionType>());
        }
    } break;

    case 9: // VECTOR_TYPE ('<' typeExpr (',' INTEGER)? '>')?
    {
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() == 1) {
            const auto &type = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr[0]));
            const auto &sizes = context->INTEGER();
            const auto &vectorType =
                std::make_shared<VectorType>(type, sizes.size() ? std::stoi(sizes[0]->getText()) : 1);
            debug(0) << "vectorType: " << int(vectorType->code()) << std::endl;
            return std::dynamic_pointer_cast<Type>(vectorType);
        } else {
            // if no type is specified, use any type
            return std::dynamic_pointer_cast<Type>(std::make_shared<VectorType>(anyTypePtr, 1));
        }
    } break;

    case 10: // TENSOR_TYPE ('<' typeExpr (',' '[' INTEGER (',' INTEGER)* ']')? '>')?
    {
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() == 1) {
            const auto &type = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr[0]));
            const auto &sizes = context->INTEGER();
            if (sizes.size()) {
                std::vector<size_t> dimensions;
                for (const auto &size : sizes) {
                    dimensions.push_back(std::stoi(size->getText()));
                }
                const auto tensorType = std::make_shared<TensorType>(type, dimensions);
                return std::dynamic_pointer_cast<Type>(tensorType);
            } else {
                // if no size is specified, use a scalar tensor
                const auto tensorType = std::make_shared<TensorType>(type, std::vector<size_t>());
                return std::dynamic_pointer_cast<Type>(tensorType);
            }
        } else {
            // if no type is specified, use any type
            return std::dynamic_pointer_cast<Type>(std::make_shared<TensorType>(anyTypePtr, std::vector<size_t>()));
        }
    } break;

    default:
        throw std::runtime_error("Unknown struct type");
    }
};

/*
specialType
    : ANY_TYPE
    | VOID_TYPE
    | FUNCTOR_TYPE
    ;
*/
std::any ASTConstructor::visitSpecialType(OpenCMLParser::SpecialTypeContext *context) {
    debug(0) << "visitSpecialType" << std::endl;
    const auto &tokenType = context->getStart()->getType();
    switch (tokenType) {
    case OpenCMLLexer::ANY_TYPE: // ANY_TYPE
        return anyTypePtr;
        break;
    case OpenCMLLexer::VOID_TYPE: // VOID_TYPE
        return voidTypePtr;
        break;
    case OpenCMLLexer::FUNCTOR_TYPE: // FUNCTOR_TYPE
        return functorTypePtr;
        break;

    default:
        throw std::runtime_error("Unknown special type");
    }
};

/*
identRef : IDENTIFIER ;
*/
std::any ASTConstructor::visitIdentRef(OpenCMLParser::IdentRefContext *context) {
    debug(0) << "visitIdentRef" << std::endl;
    return context->IDENTIFIER()->getText();
};