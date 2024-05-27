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

#include "ast.h"
#include "core/struct/token.h"

/*
program : stmtList? EOF;
*/
std::any ASTConstructor::visitProgram(OpenCMLParser::ProgramContext *context) {
    if (context->stmtList()) {
        root_ = std::any_cast<ast_ptr_t>(visitStmtList(context->stmtList()));
    } else {
        root_ = createAstNode<ExecuteNode>();
    }
};

/*
stmtList : stmt+ ;
*/
std::any ASTConstructor::visitStmtList(OpenCMLParser::StmtListContext *context) {
    ast_ptr_t execNode = createAstNode<ExecuteNode>();
    for (const auto &stmt : context->stmt()) {
        *execNode << std::any_cast<ast_ptr_t>(visitStmt(stmt));
    }
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
    ast_ptr_t execNode = createAstNode<ExecuteNode>();
    ast_ptr_t typeNode = nullptr;
    ast_ptr_t dataNode = nullptr;
    // TODO: Implement the following code

    if (context->typeExpr()) {
        typeNode = std::any_cast<ast_ptr_t>(visitTypeExpr(context->typeExpr()));
    }

    if (context->entityExpr()) {
        dataNode = std::any_cast<ast_ptr_t>(visitEntityExpr(context->entityExpr()));
        if (typeNode) {
            ast_ptr_t castNode = createAstNode<DeRefNode>("__cast__");
            *castNode << dataNode << typeNode;
            *execNode << castNode;
        } else {
            *execNode << dataNode;
        }
    }
};

/*
useStmt : USE carrier '='? entityExpr
        | carrier '::' entityExpr ;
*/
std::any ASTConstructor::visitUseStmt(OpenCMLParser::UseStmtContext *context){};

/*
typeStmt : TYPE identRef '='? typeExpr ;
*/
std::any ASTConstructor::visitTypeStmt(OpenCMLParser::TypeStmtContext *context){};

/*
exprStmt : annotations? entityExpr ;
*/
std::any ASTConstructor::visitExprStmt(OpenCMLParser::ExprStmtContext *context){};

/*
assignStmt : identRef memberAccess? '=' entityExpr ;
*/
std::any ASTConstructor::visitAssignStmt(OpenCMLParser::AssignStmtContext *context){};

/*
withDef : WITH angledParams ;
*/
std::any ASTConstructor::visitWithDef(OpenCMLParser::WithDefContext *context){};

/*
funcDef : annotations? withDef? modifiers? FUNC identRef parentParams (':' typeExpr)? bracedStmts ;
*/
std::any ASTConstructor::visitFuncDef(OpenCMLParser::FuncDefContext *context){};

/*
retStmt : RETURN entityExpr? ;
*/
std::any ASTConstructor::visitRetStmt(OpenCMLParser::RetStmtContext *context){};

/*
lambdaExpr : modifiers? ((parentParams (':' typeExpr)? '=>' (bracedStmts | entityExpr)) | '{' stmtList '}' ) ;
*/
std::any ASTConstructor::visitLambdaExpr(OpenCMLParser::LambdaExprContext *context){};

/*
carrier : identRef | bracedIdents | bracketIdents ;
*/
std::any ASTConstructor::visitCarrier(OpenCMLParser::CarrierContext *context) {
    const size_t alt = context->getAltNumber();
    switch (alt) {
    case 1:
        return std::make_pair(alt, std::any_cast<std::string>(visitIdentRef(context->identRef())));
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
    // TODO: Implement visitAnnotation
    return visitPrimEntity(context->primEntity());
};

/*
annotations : (annotation SEP?)+ ;
*/
std::any ASTConstructor::visitAnnotations(OpenCMLParser::AnnotationsContext *context) {
    // TODO: Implement visitAnnotations
    ast_ptr_t execNode = createAstNode<ExecuteNode>();
    for (const auto &annotation : context->annotation()) {
        *execNode << std::any_cast<ast_ptr_t>(visitAnnotation(annotation));
    }
    return execNode;
};

/*
modifiers   : (INNER | OUTER | ATOMIC | STATIC | SYNC)+ ;
*/
std::any ASTConstructor::visitModifiers(OpenCMLParser::ModifiersContext *context) {
    std::set<std::string> modifiers;
    for (const auto &mod : context->children) {
        modifiers.insert(mod->getText());
    }
    // TODO: use std::move to transfer the ownership of modifiers to the caller
    return modifiers;
};

/*
keyTypePair  : identRef ':' typeExpr ;
*/
std::any ASTConstructor::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) {
    return std::make_pair(context->identRef()->getText(), visitTypeExpr(context->typeExpr()));
};

/*
keyValuePair : identRef ':' entityExpr ;
*/
std::any ASTConstructor::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {
    return std::make_pair(context->identRef()->getText(), visitEntityExpr(context->entityExpr()));
};

/*
keyParamPair : identRef annotation? ':' typeExpr ('=' entityExpr)? ;
*/
std::any ASTConstructor::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {
    type_ptr_t type = std::any_cast<type_ptr_t>(visitTypeExpr(context->typeExpr()));
    ast_ptr_t defaultNode = nullptr;
    if (context->entityExpr()) {
        defaultNode = std::any_cast<ast_ptr_t>(visitEntityExpr(context->entityExpr()));
    }
    // TODO: implement the support for annotation
    return std::make_tuple(context->identRef()->getText(), type, defaultNode);
};

/*
typeList     : typeExpr (',' typeExpr)* ;
*/
std::any ASTConstructor::visitTypeList(OpenCMLParser::TypeListContext *context) {
    std::vector<ast_ptr_t> typeList;
    for (const auto &type : context->typeExpr()) {
        typeList.push_back(std::any_cast<ast_ptr_t>(visitTypeExpr(type)));
    }
    return typeList;
};

/*
identList    : identRef (',' identRef)* ;
*/
std::any ASTConstructor::visitIdentList(OpenCMLParser::IdentListContext *context) {
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
    std::vector<std::tuple<std::string, type_ptr_t, ast_ptr_t>> pairedParams;
    for (const auto &pair : context->keyParamPair()) {
        pairedParams.push_back(std::any_cast<std::tuple<std::string, type_ptr_t, ast_ptr_t>>(visitKeyParamPair(pair)));
    }
    return pairedParams;
};

/*
argumentList : valueList (',' pairedValues)? | pairedValues ;
*/
std::any ASTConstructor::visitArgumentList(OpenCMLParser::ArgumentListContext *context) {
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
    if (context->stmtList()) {
        return visitStmtList(context->stmtList());
    } else {
        return createAstNode<ExecuteNode>();
    }
};

/*
bracketIdents : '[' identList? ','? ']' ;
*/
std::any ASTConstructor::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {
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
    const auto &pairedParams = context->pairedParams();
    if (pairedParams) {
        return visitPairedParams(pairedParams);
    } else {
        return std::vector<std::tuple<std::string, type_ptr_t, ast_ptr_t>>();
    }
};

/*
parentValues : '(' argumentList? ','? ')' ;
*/
std::any ASTConstructor::visitParentValues(OpenCMLParser::ParentValuesContext *context) {
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
    const auto &pairedParams = context->pairedParams();
    if (pairedParams) {
        return visitPairedParams(pairedParams);
    } else {
        return std::vector<std::tuple<std::string, type_ptr_t, ast_ptr_t>>();
    }
};

/*
angledValues : '<' argumentList? ','? '>' ;
*/
std::any ASTConstructor::visitAngledValues(OpenCMLParser::AngledValuesContext *context) {
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
    switch (context->getAltNumber()) {
    case 1:
        return createAstNode<DeRefNode>(context->identRef()->getText());
        break;
    case 2:
        return visitLiteral(context->literal());
        break;
    case 3: {
        const std::vector<ast_ptr_t> &values =
            std::any_cast<std::vector<ast_ptr_t>>(visitBracketValues(context->bracketValues()));
        const auto &listValue = std::make_shared<ListValue>();
        bool dangling = false;
        const auto &execNode = createAstNode<ExecuteNode>();
        for (const auto &node : values) {
            if (node->type() == SemNodeType::DATA) {
                const DataNode &dataNode = dynamic_cast<const DataNode &>(*node);
                listValue->add(dataNode.entity());
            } else {
                dangling = true;
                auto [refNode, entity] = makeDanglingPair(node);
                *execNode << refNode;
                listValue->add(entity);
            }
        }
        const auto &dataNode = createAstNode<DataNode>(std::make_shared<Entity>(listTypePtr, listValue));
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
        bool dangling = false;
        const auto &execNode = createAstNode<ExecuteNode>();
        for (const auto &[key, node] : values) {
            if (node->type() == SemNodeType::DATA) {
                const DataNode &dataNode = dynamic_cast<const DataNode &>(*node);
                dictValue->add(key, dataNode.entity());
            } else {
                dangling = true;
                auto [refNode, entity] = makeDanglingPair(node);
                *execNode << refNode;
                dictValue->add(key, entity);
            }
        }
        const auto &dataNode = createAstNode<DataNode>(std::make_shared<Entity>(dictValue->type(), dictValue));
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
    return visitEntityExpr(context->entityExpr());
};

/*
entity       : primEntity (memberAccess | angledValues | annotation | parentValues)* ;
*/
std::any ASTConstructor::visitEntity(OpenCMLParser::EntityContext *context) {
    ast_ptr_t &primEntity = std::any_cast<ast_ptr_t>(visitPrimEntity(context->primEntity()));
    ast_ptr_t entity = primEntity;
    for (size_t i = 1; i < context->children.size(); i++) {
        bool dangling = false;
        auto &execNode = createAstNode<ExecuteNode>();
        const auto &child = context->children[i];
        // TODO: find a better way to determine the type of the child
        switch (child->getText()[0]) {
        case '[': // memberAccess
        {
            const auto &memberAccess = dynamic_cast<OpenCMLParser::MemberAccessContext *>(child);
            ast_ptr_t &indexNode = std::any_cast<ast_ptr_t>(visitMemberAccess(memberAccess));
            ast_ptr_t &linkNode = createAstNode<LinkNode>();
            // TODO: inner function names can share the same deref node
            ast_ptr_t &funcNode = createAstNode<DeRefNode>("__index__");
            auto &listValue = std::make_shared<ListValue>();
            if (entity->type() == SemNodeType::DATA) {
                const DataNode &dataNode = dynamic_cast<const DataNode &>(*entity);
                listValue->add(dataNode.entity());
            } else {
                auto [refNode, refEntity] = makeDanglingPair(entity);
                dangling = true;
                *execNode << refNode;
                listValue->add(refEntity);
            }
            if (indexNode->type() == SemNodeType::DATA) {
                const DataNode &dataNode = dynamic_cast<const DataNode &>(*indexNode);
                listValue->add(dataNode.entity());
            } else {
                auto [refNode, refEntity] = makeDanglingPair(indexNode);
                dangling = true;
                *execNode << refNode;
                listValue->add(refEntity);
            }
            ast_ptr_t &dataNode = createAstNode<DataNode>(std::make_shared<Entity>(listTypePtr, listValue));
            *linkNode << dataNode << funcNode;
            if (dangling) {
                *execNode << linkNode;
                entity = execNode;
            } else {
                entity = linkNode;
            }
        } break;

        case '<': // angledValues
        {
            const auto &angledValues = dynamic_cast<OpenCMLParser::AngledValuesContext *>(child);
            const auto &[indexArgs, namedArgs] =
                std::any_cast<std::pair<std::vector<ast_ptr_t>, std::vector<std::pair<std::string, ast_ptr_t>>>>(
                    visitAngledValues(angledValues));
            ast_ptr_t &callNode = createAstNode<CallNode>();
            ast_ptr_t &funcNode = entity;
            auto &listValue = std::make_shared<ListValue>();
            auto &namedTuple = std::make_shared<NamedTupleValue>();
            bool dangling = false;
            auto &execNode = createAstNode<ExecuteNode>();
            for (const auto &arg : indexArgs) {
                if (arg->type() == SemNodeType::DATA) {
                    const DataNode &dataNode = dynamic_cast<const DataNode &>(*arg);
                    namedTuple->add(dataNode.entity());
                } else {
                    dangling = true;
                    auto [refNode, entity] = makeDanglingPair(arg);
                    *execNode << refNode;
                    namedTuple->add(entity);
                }
            }
            for (const auto &[key, arg] : namedArgs) {
                if (arg->type() == SemNodeType::DATA) {
                    const DataNode &dataNode = dynamic_cast<const DataNode &>(*arg);
                    namedTuple->add(dataNode.entity(), key);
                } else {
                    dangling = true;
                    auto [refNode, entity] = makeDanglingPair(arg);
                    *execNode << refNode;
                    namedTuple->add(entity, key);
                }
            }
            ast_ptr_t &dataNode = createAstNode<DataNode>(std::make_shared<Entity>(namedTuple->type(), namedTuple));
            if (dangling) {
                *execNode << dataNode;
                *callNode << execNode << funcNode;
                entity = callNode;
            } else {
                *callNode << dataNode << funcNode;
                entity = callNode;
            }
        } break;

        case '(': // parentValues
        {
            const auto &parentValues = dynamic_cast<OpenCMLParser::ParentValuesContext *>(child);
            const auto &[indexArgs, namedArgs] =
                std::any_cast<std::pair<std::vector<ast_ptr_t>, std::vector<std::pair<std::string, ast_ptr_t>>>>(
                    visitParentValues(parentValues));
            ast_ptr_t &linkNode = createAstNode<LinkNode>();
            ast_ptr_t &funcNode = entity;
            auto &listValue = std::make_shared<ListValue>();
            auto &namedTuple = std::make_shared<NamedTupleValue>();
            bool dangling = false;
            auto &execNode = createAstNode<ExecuteNode>();
            for (const auto &arg : indexArgs) {
                if (arg->type() == SemNodeType::DATA) {
                    const DataNode &dataNode = dynamic_cast<const DataNode &>(*arg);
                    listValue->add(dataNode.entity());
                } else {
                    dangling = true;
                    auto [refNode, entity] = makeDanglingPair(arg);
                    *execNode << refNode;
                    listValue->add(entity);
                }
            }
            for (const auto &[key, arg] : namedArgs) {
                if (arg->type() == SemNodeType::DATA) {
                    const DataNode &dataNode = dynamic_cast<const DataNode &>(*arg);
                    namedTuple->add(dataNode.entity(), key);
                } else {
                    dangling = true;
                    auto [refNode, entity] = makeDanglingPair(arg);
                    *execNode << refNode;
                    namedTuple->add(entity, key);
                }
            }
            ast_ptr_t &dataNode = createAstNode<DataNode>(std::make_shared<Entity>(namedTuple->type(), namedTuple));
            if (dangling) {
                *execNode << dataNode;
                *linkNode << execNode << funcNode;
                entity = linkNode;
            } else {
                *linkNode << dataNode << funcNode;
                entity = linkNode;
            }
        } break;

        case '@': // annotation
        {
            // TODO: Implement the support for annotation
            const auto &annotation = dynamic_cast<OpenCMLParser::AnnotationContext *>(child);
            ast_ptr_t &annoNode = std::any_cast<ast_ptr_t>(visitAnnotation(annotation));
            ast_ptr_t &linkNode = createAstNode<LinkNode>();
            ast_ptr_t &funcNode = entity;
            *linkNode << annoNode << funcNode;
            entity = linkNode;
        } break;

        default:
            throw std::runtime_error("Unknown entity type");
        }
    }
};

/*
entityChain  : entityLink+ ;
*/
std::any ASTConstructor::visitEntityChain(OpenCMLParser::EntityChainContext *context) {
    ast_ptr_t execNode = createAstNode<ExecuteNode>();
    const auto &entityListValue = std::make_shared<ListValue>();
    for (const auto &link : context->entityLink()) {
        auto [refNode, entity] = makeDanglingPair(std::any_cast<ast_ptr_t>(link));
        *execNode << refNode;
        entityListValue->add(entity);
    }
    ast_ptr_t dataNode = createAstNode<DataNode>(std::make_shared<Entity>(listTypePtr, entityListValue));
    *execNode << dataNode;
    ast_ptr_t linkNode = createAstNode<LinkNode>();
    ast_ptr_t funcNode = createAstNode<DeRefNode>("__chain__");
    *linkNode << execNode << funcNode;
    return linkNode;
};

/*
entityLink   : entityCall | entityLink '->' entityCall ;
*/
std::any ASTConstructor::visitEntityLink(OpenCMLParser::EntityLinkContext *context) {
    if (context->children.size() == 1) {
        return visitEntityCall(context->entityCall());
    } else {
        ast_ptr_t linkNode = createAstNode<LinkNode>();
        ast_ptr_t dataNode = std::any_cast<ast_ptr_t>(visitEntityLink(context->entityLink()));
        ast_ptr_t funcNode = std::any_cast<ast_ptr_t>(visitEntityCall(context->entityCall()));

        *linkNode << dataNode << funcNode;

        return linkNode;
    }
};

/*
entityCall   : entity | entityCall '.' entity ;
*/
std::any ASTConstructor::visitEntityCall(OpenCMLParser::EntityCallContext *context) {
    if (context->children.size() == 1) {
        return visitEntity(context->entity());
    } else {
        ast_ptr_t callNode = createAstNode<CallNode>();
        ast_ptr_t dataNode = std::any_cast<ast_ptr_t>(visitEntityCall(context->entityCall()));
        ast_ptr_t funcNode = std::any_cast<ast_ptr_t>(visitEntity(context->entity()));

        *callNode << dataNode << funcNode;

        return callNode;
    }
};

/*
entitySpread : '...' entity ;
*/
std::any ASTConstructor::visitEntitySpread(OpenCMLParser::EntitySpreadContext *context) {
    return visitEntity(context->entity());
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
    const auto &relaExpr = visitRelaExpr(context->relaExpr());
    if (context->children.size() == 1) {
        return relaExpr;
    } else {
        ast_ptr_t linkNode = createAstNode<LinkNode>();
        ast_ptr_t execNode = createAstNode<ExecuteNode>();

        auto [entityRefNode, entityEntity] =
            makeDanglingPair(std::any_cast<ast_ptr_t>(visitEntityExpr(context->entityExpr())));
        auto [relaRefNode, relaEntity] = makeDanglingPair(std::any_cast<ast_ptr_t>(relaExpr));

        const auto listValue =
            std::make_shared<ListValue>(std::initializer_list<entity_ptr_t>{entityEntity, relaEntity});
        const auto listEntity = std::make_shared<Entity>(listTypePtr, listValue);

        ast_ptr_t dataNode = createAstNode<DataNode>(listEntity);

        *execNode << entityRefNode << relaRefNode << dataNode;

        ast_ptr_t funcNode = nullptr;
        const auto &op = context->children[1]->getText();
        if (op == "+=") {
            funcNode = createAstNode<DeRefNode>("__i_add__");
        } else if (op == "-=") {
            funcNode = createAstNode<DeRefNode>("__i_sub__");
        } else if (op == "*=") {
            funcNode = createAstNode<DeRefNode>("__i_mul__");
        } else if (op == "/=") {
            funcNode = createAstNode<DeRefNode>("__i_div__");
        } else if (op == "%=") {
            funcNode = createAstNode<DeRefNode>("__i_mod__");
        } else if (op == "^=") {
            funcNode = createAstNode<DeRefNode>("__i_pow__");
        } else if (op == "&=") {
            funcNode = createAstNode<DeRefNode>("__i_inter__");
        } else if (op == "|=") {
            funcNode = createAstNode<DeRefNode>("__i_union__");
        } else {
            throw std::runtime_error("Unknown operator: " + op);
        }

        *linkNode << execNode << funcNode;

        return linkNode;
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
    const auto &addExpr = visitAddExpr(context->addExpr());
    if (context->children.size() == 1) {
        return addExpr;
    } else {
        ast_ptr_t linkNode = createAstNode<LinkNode>();
        ast_ptr_t execNode = createAstNode<ExecuteNode>();

        auto [relaRefNode, relaEntity] = makeDanglingPair(std::any_cast<ast_ptr_t>(visitRelaExpr(context->relaExpr())));
        auto [addRefNode, addEntity] = makeDanglingPair(std::any_cast<ast_ptr_t>(addExpr));

        const auto listValue = std::make_shared<ListValue>(std::initializer_list<entity_ptr_t>{relaEntity, addEntity});
        const auto listEntity = std::make_shared<Entity>(listTypePtr, listValue);

        ast_ptr_t dataNode = createAstNode<DataNode>(listEntity);

        *execNode << relaRefNode << addRefNode << dataNode;

        ast_ptr_t funcNode = nullptr;
        const auto &op = context->children[1]->getText();
        if (op == "<") {
            funcNode = createAstNode<DeRefNode>("__lt__");
        } else if (op == ">") {
            funcNode = createAstNode<DeRefNode>("__gt__");
        } else if (op == "<=") {
            funcNode = createAstNode<DeRefNode>("__le__");
        } else if (op == ">=") {
            funcNode = createAstNode<DeRefNode>("__ge__");
        } else if (op == "==") {
            funcNode = createAstNode<DeRefNode>("__eq__");
        } else if (op == "!=") {
            funcNode = createAstNode<DeRefNode>("__ne__");
        } else if (op == "&&") {
            funcNode = createAstNode<DeRefNode>("__and__");
        } else if (op == "||") {
            funcNode = createAstNode<DeRefNode>("__or__");
        } else {
            throw std::runtime_error("Unknown operator: " + op);
        }

        *linkNode << execNode << funcNode;

        return linkNode;
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
    const auto &multiExpr = visitMultiExpr(context->multiExpr());
    if (context->children.size() == 1) {
        return multiExpr;
    } else {
        ast_ptr_t linkNode = createAstNode<LinkNode>();
        ast_ptr_t execNode = createAstNode<ExecuteNode>();

        auto [addRefNode, addEntity] = makeDanglingPair(std::any_cast<ast_ptr_t>(visitAddExpr(context->addExpr())));
        auto [multiRefNode, multiEntity] = makeDanglingPair(std::any_cast<ast_ptr_t>(multiExpr));

        const auto listValue = std::make_shared<ListValue>(std::initializer_list<entity_ptr_t>{addEntity, multiEntity});
        const auto listEntity = std::make_shared<Entity>(listTypePtr, listValue);

        ast_ptr_t dataNode = createAstNode<DataNode>(listEntity);

        *execNode << addRefNode << multiRefNode << dataNode;

        ast_ptr_t funcNode = nullptr;
        const auto &op = context->children[1]->getText();
        if (op == "+") {
            funcNode = createAstNode<DeRefNode>("__add__");
        } else if (op == "-") {
            funcNode = createAstNode<DeRefNode>("__sub__");
        } else if (op == "&") {
            funcNode = createAstNode<DeRefNode>("__inter__");
        } else if (op == "|") {
            funcNode = createAstNode<DeRefNode>("__union__");
        } else {
            throw std::runtime_error("Unknown operator: " + op);
        }

        *linkNode << execNode << funcNode;

        return linkNode;
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
    const auto &unaryExpr = visitUnaryExpr(context->unaryExpr());
    if (context->children.size() == 1) {
        return unaryExpr;
    } else {
        ast_ptr_t linkNode = createAstNode<LinkNode>();
        ast_ptr_t execNode = createAstNode<ExecuteNode>();

        auto [multiRefNode, multiEntity] =
            makeDanglingPair(std::any_cast<ast_ptr_t>(visitMultiExpr(context->multiExpr())));
        auto [unaryRefNode, unaryEntity] = makeDanglingPair(std::any_cast<ast_ptr_t>(unaryExpr));

        const auto listValue =
            std::make_shared<ListValue>(std::initializer_list<entity_ptr_t>{multiEntity, unaryEntity});
        const auto listEntity = std::make_shared<Entity>(listTypePtr, listValue);

        ast_ptr_t dataNode = createAstNode<DataNode>(listEntity);

        *execNode << multiRefNode << unaryRefNode << dataNode;

        ast_ptr_t funcNode = nullptr;
        const auto &op = context->children[1]->getText();
        if (op == "^") {
            funcNode = createAstNode<DeRefNode>("__pow__");
        } else if (op == "*") {
            funcNode = createAstNode<DeRefNode>("__mul__");
        } else if (op == "/") {
            funcNode = createAstNode<DeRefNode>("__div__");
        } else if (op == "%") {
            funcNode = createAstNode<DeRefNode>("__mod__");
        } else if (op == "as") {
            funcNode = createAstNode<DeRefNode>("__cast__");
        } else if (op == "is") {
            funcNode = createAstNode<DeRefNode>("__type__");
        } else {
            throw std::runtime_error("Unknown operator: " + op);
        }

        *linkNode << execNode << funcNode;

        return linkNode;
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
    const auto &primExpr = visitPrimExpr(context->primExpr());
    if (context->children.size() == 1) {
        return primExpr;
    } else {
        ast_ptr_t linkNode = std::make_shared<ASTNode>(std::make_shared<LinkNode>());
        ast_ptr_t dataNode = std::any_cast<ast_ptr_t>(primExpr);
        ast_ptr_t funcNode = nullptr;
        const auto &op = context->children[0]->getText();
        if (op == "!") {
            funcNode = std::make_shared<ASTNode>(std::make_shared<DeRefNode>("__not__"));
        } else {
            funcNode = std::make_shared<ASTNode>(std::make_shared<DeRefNode>("__neg__"));
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
    type_ptr_t type = nullptr;
    value_ptr_t value = nullptr;

    switch (context->getAltNumber()) {
    case 1: // INTEGER UNIT?
        type = int64TypePtr;
        value = std::make_shared<PrimValue<int64_t>>(parseNumber<int64_t>(context->INTEGER()->getText()));
        break;
    case 2: // REAL UNIT?
        type = doubleTypePtr;
        value = std::make_shared<PrimValue<double>>(parseNumber<double>(context->REAL()->getText()));
        break;
    case 3: // STRING
    {
        type = stringTypePtr;
        const auto &text = context->STRING()->getText();
        value = std::make_shared<StringValue>(text.substr(1, text.size() - 2));
    } break;
    case 4: // MULTI_STR
    {
        type = stringTypePtr;
        const auto &text = context->MULTI_STR()->getText();
        value = std::make_shared<StringValue>(text.substr(3, text.size() - 6));
    } break;
    case 5: // FSTRING
    {
        type = stringTypePtr;
        // TODO: Implement FSTRING
        const auto &text = context->FSTRING()->getText();
        value = std::make_shared<StringValue>(text.substr(2, text.size() - 3));
    } break;
    case 6: // TRUE
        type = boolTypePtr;
        value = std::make_shared<PrimValue<bool>>(true);
        break;
    case 7: // FALSE
        type = boolTypePtr;
        value = std::make_shared<PrimValue<bool>>(false);
        break;

    default:
        break;
    }

    return createAstNode<DataNode>(std::make_shared<Entity>(type, value));
};

/*
typeExpr
    : type ('[' ']')?
    | typeExpr '|' typeExpr
    | typeExpr '&' typeExpr
    ;
*/
std::any ASTConstructor::visitTypeExpr(OpenCMLParser::TypeExprContext *context){};

/*
type
    : primType
    | structType
    | specialType
    | identRef
    | '(' typeExpr ')'
    ;
*/
std::any ASTConstructor::visitType(OpenCMLParser::TypeContext *context){};

/*
lambdaType
    : ('<' pairedParams? '>')? '(' pairedParams? ')' '=>' typeExpr
    ;
*/
std::any ASTConstructor::visitLambdaType(OpenCMLParser::LambdaTypeContext *context) {
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
            entity_ptr_t value = nullptr;
            if (valueNode) { // valueNode may be nullptr
                if (valueNode->type() == SemNodeType::DATA) {
                    const DataNode &dataNode = dynamic_cast<const DataNode &>(*valueNode);
                    value = dataNode.entity();
                } else {
                    throw std::runtime_error("Default values for a functor must be primitive");
                }
            }
            withType->add(name, type, value);
        }
    }
    const auto &params = std::any_cast<std::vector<std::tuple<std::string, type_ptr_t, ast_ptr_t>>>(
        visitPairedParams(pairedParamsList.back()));
    for (const auto &[name, type, valueNode] : params) {
        entity_ptr_t value = nullptr;
        if (valueNode) { // valueNode may be nullptr
            if (valueNode->type() == SemNodeType::DATA) {
                const DataNode &dataNode = dynamic_cast<const DataNode &>(*valueNode);
                value = dataNode.entity();
            } else {
                throw std::runtime_error("Default values for a functor must be primitive");
            }
        }
        paramsType->add(name, type, value);
    }
    returnType = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr));
    return std::make_shared<FunctorType>(withType, paramsType, returnType);
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
    switch (context->getAltNumber()) {
    case 1: // INTEGER_TYPE
        return intTypePtr;
        break;
    case 2: // INTEGER32_TYPE
        return int32TypePtr;
        break;
    case 3: // INTEGER64_TYPE
        return int64TypePtr;
        break;
    case 4: // REAL_TYPE
        return realTypePtr;
        break;
    case 5: // FLOAT_TYPE
        return floatTypePtr;
        break;
    case 6: // DOUBLE_TYPE
        return doubleTypePtr;
        break;
    case 7: // NUMBER_TYPE
        return numberTypePtr;
        break;
    case 8: // STRING_TYPE
        return stringTypePtr;
        break;
    case 9: // BOOL_TYPE
        return boolTypePtr;
        break;
    case 10: // CHAR_TYPE
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
    switch (context->getAltNumber()) {
    case 1: // SET_TYPE ('<' typeExpr '>')?
    {
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() == 1) {
            const auto &type = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr[0]));
            const auto &setType = std::make_shared<SetType>(type);
            return setType;
        } else {
            // if no type is specified, use any type
            return std::make_shared<SetType>(anyTypePtr);
        }
    } break;

    case 2: // MAP_TYPE ('<' typeExpr ',' typeExpr '>')?
    {
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() == 2) {
            const auto &keyType = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr[0]));
            const auto &valueType = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr[1]));
            const auto &mapType = std::make_shared<MapType>(keyType, valueType);
            return mapType;
        } else {
            // if no type is specified, use any type
            return std::make_shared<MapType>(anyTypePtr, anyTypePtr);
        }
    } break;

    case 3: // LIST_TYPE
        return listTypePtr;
        break;

    case 4: // DICT_TYPE
        // TODO: Implement the support for universal dict type
        return std::make_shared<DictType>();
        break;

    case 5: // '{' pairedTypes? ','? '}'
    {
        const auto &pairedTypes = context->pairedTypes();
        if (pairedTypes) {
            const auto &types =
                std::any_cast<std::vector<std::pair<std::string, type_ptr_t>>>(visitPairedTypes(pairedTypes));
            auto &dictType = std::make_shared<DictType>();
            for (const auto &[key, type] : types) {
                dictType->add(key, type);
            }
            return dictType;
        } else {
            return std::make_shared<DictType>();
        }
    } break;

    case 6: // ARRAY_TYPE ('<' typeExpr '>')?
    {
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() == 1) {
            const auto &type = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr[0]));
            const auto &arrayType = std::make_shared<ArrayType>(type);
            return arrayType;
        } else {
            // if no type is specified, use any type
            return std::make_shared<ArrayType>(anyTypePtr);
        }
    } break;

    case 7: // TUPLE_TYPE ('<' typeList? ','? '>')?
    {
        const auto &typeList = context->typeList();
        if (typeList) {
            const auto &types = std::any_cast<std::vector<type_ptr_t>>(visitTypeList(typeList));
            const auto &tupleType = std::make_shared<TupleType>(types);
            return tupleType;
        } else {
            return std::make_shared<TupleType>();
        }
    } break;

    case 8: // UNION_TYPE ('<' typeList? ','? '>')?
    {
        const auto &typeList = context->typeList();
        if (typeList) {
            const auto &types = std::any_cast<std::vector<type_ptr_t>>(visitTypeList(typeList));
            const auto &unionType = std::make_shared<UnionType>(types);
            return unionType;
        } else {
            return std::make_shared<UnionType>();
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
            return vectorType;
        } else {
            // if no type is specified, use any type
            return std::make_shared<VectorType>(anyTypePtr, 1);
        }
    } break;

    case 10: // TENSOR_TYPE ('<' typeExpr (',' '[' INTEGER (',' INTEGER)* ']')? '>')?
    {
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() == 1) {
            const auto &type = std::any_cast<type_ptr_t>(visitTypeExpr(typeExpr[0]));
            const auto &sizes = context->INTEGER();
            if (sizes.size()) {
                std::vector<int> dimensions;
                for (const auto &size : sizes) {
                    dimensions.push_back(std::stoi(size->getText()));
                }
                const auto &tensorType = std::make_shared<TensorType>(type, dimensions);
                return tensorType;
            } else {
                // if no size is specified, use a scalar tensor
                const auto &tensorType = std::make_shared<TensorType>(type, std::vector<int>());
                return tensorType;
            }
        } else {
            // if no type is specified, use any type
            return std::make_shared<TensorType>(anyTypePtr, std::vector<int>());
        }
    } break;

    default:
        break;
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
    switch (context->getAltNumber()) {
    case 1: // ANY_TYPE
        return anyTypePtr;
        break;
    case 2: // VOID_TYPE
        return voidTypePtr;
        break;
    case 3: // FUNCTOR_TYPE
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
    return context->IDENTIFIER()->getText();
};