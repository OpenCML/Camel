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
 * Created: Mar. 26, 2024
 * Updated: May. 01, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "antlr/OpenCMLLexer.h"

#include "ast.h"
#include "common/token.h"
#include "utils/log.h"

#define DEBUG_LEVEL 0

using namespace std;
using namespace AST;

template <typename LoadType, typename... Args> node_ptr_t createNode(Args &&...args) {
    return std::make_shared<Node>(std::make_shared<LoadType>(std::forward<Args>(args)...));
}

/*
program : SEP? (decl SEP?)* EOF;
*/
any Constructor::visitProgram(OpenCMLParser::ProgramContext *context) {
    enter("Program");

    root_ = createNode<Program>();

    const auto &decls = context->decl();

    if (decls.size() == 1)
        root_ = any_cast<node_ptr_t>(visitDecl(decls[0]));
    else {
        for (const auto &decl : decls) {
            *root_ << any_cast<node_ptr_t>(visitDecl(decl));
        }
    }

    leave("Program");
    return root_;
}

/*
decl
    : moduleDecl
    | importDecl
    | exportDecl
    | letDecl
    | useDecl
    | funcDecl
    | typeDecl
    | enumDecl
    ;
*/
any Constructor::visitDecl(OpenCMLParser::DeclContext *context) {
    enter("Decl");
    any res;
    if (context->moduleDecl()) { // module decl
        res = visitModuleDecl(context->moduleDecl());
    }
    if (context->importDecl()) { // import decl
        res = visitImportDecl(context->importDecl());
    }
    if (context->exportDecl()) { // export decl
        res = visitExportDecl(context->exportDecl());
    }
    if (context->letDecl()) { // let decl
        res = visitLetDecl(context->letDecl());
    }
    if (context->useDecl()) { // use decl
        res = visitUseDecl(context->useDecl());
    }
    if (context->funcDecl()) { // func decl
        res = visitFuncDecl(context->funcDecl());
    }
    if (context->typeDecl()) { // type decl
        res = visitTypeDecl(context->typeDecl());
    }
    if (context->enumDecl()) { // enum decl
        res = visitEnumDecl(context->enumDecl());
    }
    if (res.has_value()) {
        leave("Decl");
        return res;
    }
    throw runtime_error("Unknown declaration type");
    leave("Decl");
}

/*
stmt
    : letDecl
    | useDecl
    | funcDecl
    | typeDecl
    | enumDecl
    | retStmt
    | waitStmt
    | dataExpr
    | stmtBlock
    ;
*/
any Constructor::visitStmt(OpenCMLParser::StmtContext *context) {
    enter("Stmt");
    node_ptr_t res;

    switch (context->getAltNumber()) {
    case 1: // letDecl
        res = any_cast<node_ptr_t>(visitLetDecl(context->letDecl()));
        break;
    case 2: // useDecl
        res = any_cast<node_ptr_t>(visitUseDecl(context->useDecl()));
        break;
    case 3: // funcDecl
        res = any_cast<node_ptr_t>(visitFuncDecl(context->funcDecl()));
        break;
    case 4: // typeDecl
        res = any_cast<node_ptr_t>(visitTypeDecl(context->typeDecl()));
        break;
    case 5: // enumDecl
        res = any_cast<node_ptr_t>(visitEnumDecl(context->enumDecl()));
        break;
    case 6: // retStmt
        res = any_cast<node_ptr_t>(visitRetStmt(context->retStmt()));
        break;
    case 7: // waitStmt
        res = any_cast<node_ptr_t>(visitWaitStmt(context->waitStmt()));
        break;
    case 8: // dataExpr
        res = any_cast<node_ptr_t>(visitDataExpr(context->dataExpr()));
        break;
    case 9: // stmtBlock
        res = any_cast<node_ptr_t>(visitStmtBlock(context->stmtBlock()));
        break;
    default: // Grammar error protection
        throw runtime_error("Unknown statement type");
    }

    leave("Stmt");
    return res;
}

/*
stmtList : stmt (SEP? stmt)* SEP? ;
*/
any Constructor::visitStmtList(OpenCMLParser::StmtListContext *context) {
    enter("StmtList");
    node_ptr_t stmtListNode = createNode<StmtList>();
    if (context->stmt().size() > 0)
        for (auto &stmt : context->stmt()) {
            *stmtListNode << any_cast<node_ptr_t>(visitStmt(stmt));
        }
    leave("StmtList");
    return stmtListNode;
}

/*
moduleDecl : MODULE identDef ;
*/
any Constructor::visitModuleDecl(OpenCMLParser::ModuleDeclContext *context) {
    enter("ModuleDecl");
    std::string name;
    if (context->identDef()) {
        name = context->identDef()->getText();
    }
    node_ptr_t moduleNode = createNode<ModuleDecl>(name);
    leave("ModuleDecl");
    return moduleNode;
}

/*
importDecl : IMPORT (STRING | (identDef | bracedIdents) FROM STRING) ;
*/
any Constructor::visitImportDecl(OpenCMLParser::ImportDeclContext *context) {
    enter("ImportDecl");
    std::string path;
    std::vector<std::string> idents;
    if (context->STRING()) {
        path = context->STRING()->getText();
    }
    node_ptr_t bracedIdents;
    if (context->identDef()) {
        idents.push_back(context->identDef()->getText());
    } else if (context->bracedIdents()) {
        for (auto &ident : context->bracedIdents()->identList()->identDef()) {
            idents.push_back(ident->getText());
        }
    }
    node_ptr_t importNode = createNode<ImportDecl>(path, idents);
    leave("ImportDecl");
    return importNode;
}

/*
exportDecl : EXPORT (letDecl | typeDecl | bracedIdents) ;
*/
any Constructor::visitExportDecl(OpenCMLParser::ExportDeclContext *context) {
    enter("ExportDecl");
    node_ptr_t res = nullptr;
    if (context->letDecl()) {
        res = any_cast<node_ptr_t>(visitLetDecl(context->letDecl()));
    } else if (context->typeDecl()) {
        res = any_cast<node_ptr_t>(visitTypeDecl(context->typeDecl()));
    } else if (context->bracedIdents()) {
        std::vector<std::string> idents;
        for (auto &ident : context->bracedIdents()->identList()->identDef()) {
            idents.push_back(ident->getText());
        }
        res = createNode<ExportDecl>(idents);
    }
    leave("ExportDecl");
    return res;
}

/*
stmtBlock  : SYNC? '{' stmtList? '}' ;
*/
any Constructor::visitStmtBlock(OpenCMLParser::StmtBlockContext *context) {
    enter("StmtBlock");
    node_ptr_t blockNode = createNode<StmtBlock>();
    if (context->stmtList()) {
        *blockNode << any_cast<node_ptr_t>(visitStmtList(context->stmtList()));
    }
    leave("StmtBlock");
    return blockNode;
}

/*
blockExpr  : stmtBlock | waitExpr ;
*/
any Constructor::visitBlockExpr(OpenCMLParser::BlockExprContext *context) {
    enter("BlockExpr");
    node_ptr_t res = nullptr;
    if (context->stmtBlock()) {
        res = any_cast<node_ptr_t>(visitStmtBlock(context->stmtBlock()));
    } else if (context->waitExpr()) {
        res = any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr()));
    }
    leave("BlockExpr");
    return res;
}

/*
waitStmt   : WAIT (stmtBlock | dataList) ;
*/
any Constructor::visitWaitStmt(OpenCMLParser::WaitStmtContext *context) {
    enter("WaitStmt");
    node_ptr_t res = nullptr;
    if (context->stmtBlock()) {
        res = any_cast<node_ptr_t>(visitStmtBlock(context->stmtBlock()));
    } else if (context->dataList()) {
        res = any_cast<node_ptr_t>(visitDataList(context->dataList()));
    }
    leave("WaitStmt");
    return res;
}

/*
lambdaExpr : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr ;
*/
any Constructor::visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) {
    enter("LambdaExpr");
    std::vector<LambdaExpr::Modifier> modifiers;
    auto modifier = context->modifiers();
    if (modifier) {
        if (modifier->ATOMIC().size() > 0) {
            modifiers.push_back(LambdaExpr::Modifier::ATOMIC);
        } else if (modifier->SHARED().size() > 0) {
            modifiers.push_back(LambdaExpr::Modifier::SHARED);
        } else if (modifier->SYNC().size() > 0) {
            modifiers.push_back(LambdaExpr::Modifier::SYNC);
        } else if (modifier->MACRO().size() > 0) {
            modifiers.push_back(LambdaExpr::Modifier::MACRO);
        }
    }

    node_ptr_t lambdaNode = createNode<LambdaExpr>(modifiers);
    if (context->typeExpr()) {
        *lambdaNode << any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr()));
    }
    if (context->angledParams()) {
        *lambdaNode << any_cast<node_ptr_t>(visitAngledParams(context->angledParams()));
    }
    if (context->parentParams()) {
        *lambdaNode << any_cast<node_ptr_t>(visitParentParams(context->parentParams()));
    }
    if (context->blockExpr()) {
        *lambdaNode << any_cast<node_ptr_t>(visitBlockExpr(context->blockExpr()));
    }
    leave("LambdaExpr");
    return lambdaNode;
}

/*
funcDecl   : (WITH angledParams)? EXPORT? implMark? modifiers? FUNC identDef parentParams (':' typeExpr)? stmtBlock ;
*/
any Constructor::visitFuncDecl(OpenCMLParser::FuncDeclContext *context) {
    enter("FuncDecl");
    std::vector<std::string> idents = {""};
    node_ptr_t exportNode = createNode<ExportDecl>(idents);
    std::string implMark;
    std::vector<FuncDecl::Modifier> modifiers;
    std::string identDef;
    if (context->implMark()) {
        implMark = context->implMark()->getText();
    }
    auto modifier = context->modifiers();
    if (modifier) {
        if (modifier->ATOMIC().size() > 0) {
            modifiers.push_back(FuncDecl::Modifier::ATOMIC);
        } else if (modifier->SHARED().size() > 0) {
            modifiers.push_back(FuncDecl::Modifier::SHARED);
        } else if (modifier->SYNC().size() > 0) {
            modifiers.push_back(FuncDecl::Modifier::SYNC);
        } else if (modifier->MACRO().size() > 0) {
            modifiers.push_back(FuncDecl::Modifier::MACRO);
        }
    }
    if (context->identDef()) {
        identDef = context->identDef()->getText();
    }
    node_ptr_t funcNode = createNode<FuncDecl>(implMark, modifiers, identDef);
    if (context->typeExpr()) {
        *funcNode << any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr()));
    }
    if (context->angledParams()) {
        *funcNode << any_cast<node_ptr_t>(visitAngledParams(context->angledParams()));
    }
    if (context->parentParams()) {
        node_ptr_t parentParamsNode = any_cast<node_ptr_t>(visitParentParams(context->parentParams()));
        if (parentParamsNode)
            *funcNode << parentParamsNode;
    }
    if (context->stmtBlock()) {
        *funcNode << any_cast<node_ptr_t>(visitStmtBlock(context->stmtBlock()));
    }
    node_ptr_t res = nullptr;
    if (context->EXPORT()) {
        *exportNode << funcNode;
        res = exportNode;
    } else {
        res = funcNode;
    }
    leave("FuncDecl");
    return res;
}

/*
parentIdents  : '(' identList? ','? ')' ;
*/
any Constructor::visitParentIdents(OpenCMLParser::ParentIdentsContext *context) {
    enter("ParentIdents");
    node_ptr_t res = nullptr;
    if (context->identList()) {
        res = any_cast<node_ptr_t>(visitIdentList(context->identList()));
    }
    leave("ParentIdents");
    return res;
}

/*
bracedIdents  : '{' identList? ','? '}' ;
*/
any Constructor::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {
    enter("BracedIdents");
    node_ptr_t res = nullptr;
    if (context->identList()) {
        res = any_cast<node_ptr_t>(visitIdentList(context->identList()));
    }
    leave("BracedIdents");
    return res;
}

/*
bracketIdents : '[' identList? ','? ']' ;
*/
any Constructor::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {
    enter("BracketIdents");
    node_ptr_t res = nullptr;
    if (context->identList()) {
        res = any_cast<node_ptr_t>(visitIdentList(context->identList()));
    }
    leave("BracketIdents");
    return res;
}

/*
carrier       : identList | parentIdents | bracedIdents | bracketIdents ;
*/
any Constructor::visitCarrier(OpenCMLParser::CarrierContext *context) {
    enter("Carrier");
    node_ptr_t res = nullptr;
    if (context->identList()) {
        res = any_cast<node_ptr_t>(visitIdentList(context->identList()));
    } else if (context->parentIdents()) {
        res = any_cast<node_ptr_t>(visitParentIdents(context->parentIdents()));
    }
    leave("Carrier");
    return res;
}

/*
letDecl    : (LET | VAR) carrier (':' typeList)? '=' valueList ;
*/
any Constructor::visitLetDecl(OpenCMLParser::LetDeclContext *context) {
    enter("LetDecl");
    std::string type;
    if (context->LET()) {
        type = context->LET()->getText();
    } else if (context->VAR()) {
        type = context->VAR()->getText();
    }
    node_ptr_t letNode = createNode<LetDecl>(type);
    if (context->carrier()) {
        *letNode << any_cast<node_ptr_t>(visitCarrier(context->carrier()));
    }
    if (context->typeList()) {
        *letNode << any_cast<node_ptr_t>(visitTypeList(context->typeList()));
    }
    if (context->valueList()) {
        *letNode << any_cast<node_ptr_t>(visitValueList(context->valueList()));
    }
    leave("LetDecl");
    return letNode;
}

/*
useDecl    : USE (identDef '=')? identRef ;
*/
any Constructor::visitUseDecl(OpenCMLParser::UseDeclContext *context) {
    enter("UseDecl");
    std::string identDef;
    std::string identRef;
    if (context->identDef()) {
        identDef = context->identDef()->getText();
    }
    if (context->identRef()) {
        identRef = context->identRef()->getText();
    }
    node_ptr_t useNode = createNode<UseDecl>(identDef, identRef);
    leave("UseDecl");
    return useNode;
}

/*
retStmt    : (RETURN | RAISE | THROW) valueList ;
*/
any Constructor::visitRetStmt(OpenCMLParser::RetStmtContext *context) {
    enter("RetStmt");
    std::string modifier;
    if (context->RETURN()) {
        modifier = context->RETURN()->getText();
    }
    if (context->RAISE()) {
        modifier = context->RAISE()->getText();
    }
    if (context->THROW()) {
        modifier = context->THROW()->getText();
    }
    node_ptr_t retNode = createNode<RetStmt>(modifier);
    if (context->valueList()) {
        *retNode << any_cast<node_ptr_t>(visitValueList(context->valueList()));
    }
    leave("RetStmt");
    return retNode;
}

/*
typeDecl   : implMark? TYPE identDef '=' (typeExpr | STRING) ;
*/
any Constructor::visitTypeDecl(OpenCMLParser::TypeDeclContext *context) {
    enter("TypeDecl");
    std::string implMark;
    std::string identDef;
    if (context->implMark()) {
        implMark = context->implMark()->getText();
    }
    if (context->identDef()) {
        identDef = context->identDef()->getText();
    }
    node_ptr_t typeNode = createNode<TypeDecl>(implMark, identDef);
    if (context->typeExpr()) {
        *typeNode << any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr()));
    }
    leave("TypeDecl");
    return typeNode;
}

/*
enumDecl   : ENUM identDef (OF typeExpr)? '=' '{' pairedValues ','? '}' ;
*/
any Constructor::visitEnumDecl(OpenCMLParser::EnumDeclContext *context) {
    enter("EnumDecl");
    std::string identDef;
    if (context->identDef()) {
        identDef = context->identDef()->getText();
    }
    node_ptr_t enumNode = createNode<EnumDecl>(identDef);
    if (context->typeExpr()) {
        *enumNode << any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr()));
    }
    if (context->pairedValues()) {
        *enumNode << any_cast<node_ptr_t>(visitPairedValues(context->pairedValues()));
    }
    leave("EnumDecl");
    return enumNode;
}

/*
implMark    : INNER | OUTER ;
*/
any Constructor::visitImplMark(OpenCMLParser::ImplMarkContext *context) {
    enter("ImplMark");
    // DO NOTHING
    leave("ImplMark");
    return nullptr;
}

/*
modifiers   : (ATOMIC | SHARED | SYNC | MACRO)+ ;
*/
any Constructor::visitModifiers(OpenCMLParser::ModifiersContext *context) {
    enter("Modifiers");
    // DO NOTHING
    leave("Modifiers");
    return nullptr;
}

/*
indexValue   : '...'? waitExpr ;
*/
any Constructor::visitIndexValue(OpenCMLParser::IndexValueContext *context) {
    enter("IndexValue");
    node_ptr_t res = nullptr;
    if (context->waitExpr())
        res = any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr()));
    leave("IndexValue");
    return res;
}

/*
keyTypePair  : identDef ':' typeExpr ;
*/
any Constructor::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) {
    enter("KeyTypePair");
    std::string ident;
    if (context->identDef()) {
        ident = context->identDef()->getText();
    }
    node_ptr_t keyTypeNode = createNode<KeyTypePair>(ident);
    if (context->typeExpr()) {
        *keyTypeNode << any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr()));
    }
    leave("KeyTypePair");
    return keyTypeNode;
}

/*
keyValuePair : identDef ':' waitExpr | '...' waitExpr ;
*/
any Constructor::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {
    enter("KeyValuePair");
    std::string ident;
    if (context->identDef()) {
        ident = context->identDef()->getText();
    }
    node_ptr_t keyValuePairNode = createNode<KeyValuePair>(ident);
    if (context->waitExpr()) {
        *keyValuePairNode << any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr()));
    }
    leave("KeyValuePair");
    return keyValuePairNode;
}

/*
keyParamPair : VAR? identDef ':' typeExpr ('=' waitExpr)? ;
*/
any Constructor::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {
    enter("KeyParamPair");
    bool isVar = false;
    std::string ident;
    if (context->VAR()) {
        isVar = true;
    }
    if (context->identDef()) {
        ident = context->identDef()->getText();
    }
    node_ptr_t keyParamPairNode = createNode<KeyParamPair>(isVar, ident);
    if (context->typeExpr()) {
        *keyParamPairNode << any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr()));
    }
    if (context->waitExpr()) {
        *keyParamPairNode << any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr()));
    }
    leave("KeyParamPair");
    return keyParamPairNode;
}

/*
dataList     : dataExpr (',' dataExpr)* ;
*/
any Constructor::visitDataList(OpenCMLParser::DataListContext *context) {
    enter("DataList");
    node_ptr_t dataListNode = createNode<DataList>();
    for (const auto &dataExpr : context->dataExpr()) {
        *dataListNode << any_cast<node_ptr_t>(visitDataExpr(dataExpr));
    }
    leave("DataList");
    return dataListNode;
}

/*
identList    : identDef (',' identDef)* ;
*/
any Constructor::visitIdentList(OpenCMLParser::IdentListContext *context) {
    enter("IdentList");
    std::vector<std::string> idents;
    for (const auto &identDef : context->identDef()) {
        idents.push_back(identDef->getText());
    }
    node_ptr_t identListNode = createNode<IdentList>(idents);
    leave("IdentList");
    return identListNode;
}

/*
valueList    : waitExpr (',' waitExpr)* ;
*/
any Constructor::visitValueList(OpenCMLParser::ValueListContext *context) {
    enter("ValueList");
    node_ptr_t valueListNode = createNode<ValueList>();
    if (context->waitExpr().size() > 0) {
        for (const auto &value : context->waitExpr()) {
            *valueListNode << any_cast<node_ptr_t>(visitWaitExpr(value));
        }
    }
    leave("ValueList");
    return valueListNode;
}

/*
indexValues  : indexValue (',' indexValue)* ;
*/
any Constructor::visitIndexValues(OpenCMLParser::IndexValuesContext *context) {
    enter("IndexValues");
    node_ptr_t indexValuesNode = createNode<IndexValues>();
    for (const auto &indexValue : context->indexValue()) {
        *indexValuesNode << any_cast<node_ptr_t>(visitIndexValue(indexValue));
    }
    leave("IndexValues");
    return indexValuesNode;
}

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
*/
any Constructor::visitPairedValues(OpenCMLParser::PairedValuesContext *context) {
    enter("PairedValues");
    node_ptr_t pairedValuesNode = createNode<PairedValues>();
    if (context->keyValuePair().size() > 0)
        for (const auto &keyValuePair : context->keyValuePair()) {
            *pairedValuesNode << any_cast<node_ptr_t>(visitKeyValuePair(keyValuePair));
        }
    leave("PairedValues");
    return pairedValuesNode;
}

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
any Constructor::visitPairedParams(OpenCMLParser::PairedParamsContext *context) {
    enter("PairedParams");
    node_ptr_t pairedParamsNode = createNode<PairedParams>();
    if (context->keyParamPair().size() > 0) {
        for (const auto &keyParamPair : context->keyParamPair()) {
            *pairedParamsNode << any_cast<node_ptr_t>(visitKeyParamPair(keyParamPair));
        }
    }
    leave("PairedParams");
    return pairedParamsNode;
}

/*
argumentList : indexValues (',' pairedValues)? | pairedValues ;
*/
any Constructor::visitArgumentList(OpenCMLParser::ArgumentListContext *context) {
    enter("ArgumentList");
    node_ptr_t argumentListNode = createNode<ArgumentList>();
    if (context->indexValues()) {
        *argumentListNode << any_cast<node_ptr_t>(visitIndexValues(context->indexValues()));
    }
    if (context->pairedValues()) {
        *argumentListNode << any_cast<node_ptr_t>(visitPairedValues(context->pairedValues()));
    }
    leave("ArgumentList");
    return argumentListNode;
}

/*
memberAccess : '[' waitExpr (':' waitExpr (':' waitExpr)?)? ']' ;
*/
any Constructor::visitMemberAccess(OpenCMLParser::MemberAccessContext *context) {
    enter("MemberAccess");
    node_ptr_t memberAccessNode = createNode<MemberAccess>();
    if (context->waitExpr(0)) {
        *memberAccessNode << any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr(0)));
    }
    if (context->waitExpr(1)) {
        *memberAccessNode << any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr(1)));
    }
    if (context->waitExpr(2)) {
        *memberAccessNode << any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr(2)));
    }
    leave("MemberAccess");
    return memberAccessNode;
}

/*
parentParams : '(' pairedParams? ','? ')' ;
*/
any Constructor::visitParentParams(OpenCMLParser::ParentParamsContext *context) {
    enter("ParentParams");
    node_ptr_t res = nullptr;
    const auto &pairedParams = context->pairedParams();
    if (pairedParams) {
        res = any_cast<node_ptr_t>(visitPairedParams(pairedParams));
    } else {
        res = createNode<ParentParams>();
    }
    leave("ParentParams");
    return res;
}

/*
parentArgues : '(' argumentList? ','? ')' ;
*/
any Constructor::visitParentArgues(OpenCMLParser::ParentArguesContext *context) {
    enter("ParentArgues");
    node_ptr_t res = nullptr;
    if (context->argumentList())
        res = any_cast<node_ptr_t>(visitArgumentList(context->argumentList()));
    else
        res = createNode<ParentArgues>();
    leave("ParentArgues");
    return res;
}

/*
angledParams : '<' pairedParams? ','? '>' ;
*/
any Constructor::visitAngledParams(OpenCMLParser::AngledParamsContext *context) {
    enter("AngledParams");
    node_ptr_t res = nullptr;
    if (context->pairedParams())
        res = any_cast<node_ptr_t>(visitPairedParams(context->pairedParams()));
    leave("AngledParams");
    return res;
}

/*
angledValues : '<' argumentList? ','? '>' ;
*/
any Constructor::visitAngledValues(OpenCMLParser::AngledValuesContext *context) {
    enter("AngledValues");
    node_ptr_t res = nullptr;
    if (context->argumentList())
        res = any_cast<node_ptr_t>(visitArgumentList(context->argumentList()));
    leave("AngledValues");
    return res;
}

/*
waitExpr : WAIT? dataExpr ;
*/
any Constructor::visitWaitExpr(OpenCMLParser::WaitExprContext *context) {
    enter("WaitExpr");
    bool isWait = false;
    if (context->WAIT()) {
        isWait = true;
    }
    node_ptr_t waitNode = createNode<WaitExpr>(isWait);
    if (context->dataExpr()) {
        *waitNode << any_cast<node_ptr_t>(visitDataExpr(context->dataExpr()));
    }
    leave("WaitExpr");
    return waitNode;
}

/*
pattern
    : identRef
    | literal
    | '(' (valueList | identList)? ','? ')'
    | '{' (pairedValues | identList)? ','? '}'
    | '_' // wildcard
    ;
*/
any Constructor::visitPattern(OpenCMLParser::PatternContext *context) {
    enter("Pattern");
    node_ptr_t res = nullptr;
    int num = context->getAltNumber();
    switch (num) {
    case 1: // ident ref
        res = any_cast<node_ptr_t>(visitIdentRef(context->identRef()));
        break;
    case 2: // literal
        res = any_cast<node_ptr_t>(visitLiteral(context->literal()));
        break;
    case 3: // value list | ident list
        if (context->valueList()) {
            res = any_cast<node_ptr_t>(visitValueList(context->valueList()));
        } else if (context->identList()) {
            res = any_cast<node_ptr_t>(visitIdentList(context->identList()));
        }
        break;
    case 4: // paired values | ident list
        if (context->pairedValues()) {
            res = any_cast<node_ptr_t>(visitPairedValues(context->pairedValues()));
        } else if (context->identList()) {
            res = any_cast<node_ptr_t>(visitIdentList(context->identList()));
        }
    case 5: // wildcard
        res = createNode<Wildcard>();
        break;
    default:
        throw std::runtime_error("visitPattern: invalid alt number");
        break;
    }
    leave("Pattern");
    return res;
}

/*
matchCase
    : CASE pattern ('|' pattern)* '=>' blockExpr
    ;
*/
any Constructor::visitMatchCase(OpenCMLParser::MatchCaseContext *context) {
    // TODO: match case
    enter("MatchCase");
    throw std::runtime_error("visitMatchCase: not implemented");
    leave("MatchCase");
    return nullptr;
}

/*
catchClause
    : CATCH identDef ':' typeExpr stmtBlock
    ;
*/
any Constructor::visitCatchClause(OpenCMLParser::CatchClauseContext *context) {
    // TODO: catch clause
    enter("CatchClause");
    throw std::runtime_error("visitCatchClause: not implemented");
    leave("CatchClause");
    return nullptr;
}

/*
ctrlExpr
    : IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
    | MATCH identRef '{' matchCase+ '}'
    | TRY stmtBlock catchClause+ (FINALLY stmtBlock)?
    ;
*/
any Constructor::visitCtrlExpr(OpenCMLParser::CtrlExprContext *context) {
    enter("CtrlExpr");
    node_ptr_t res = nullptr;
    if (context->IF()) { // if expr
        node_ptr_t ifNode = createNode<IfLoad>();
        node_ptr_t condNode = any_cast<node_ptr_t>(visitLogicalOrExpr(context->logicalOrExpr()));
        node_ptr_t thenNode = any_cast<node_ptr_t>(visitBlockExpr(context->blockExpr(0)));
        *ifNode << condNode << thenNode;
        if (context->ELSE()) { // else expr
            node_ptr_t elseNode = any_cast<node_ptr_t>(visitBlockExpr(context->blockExpr(1)));
            *ifNode << elseNode;
        }
        res = ifNode;
    } else if (context->MATCH()) { // match expr
        // TODO: match expr
    } else if (context->TRY()) { // try expr
        // TODO: try expr
    }
    leave("CtrlExpr");
    return res;
}

/*
dataExpr
    : assignExpr
    | ctrlExpr
    ;
*/
any Constructor::visitDataExpr(OpenCMLParser::DataExprContext *context) {
    enter("DataExpr");
    node_ptr_t res = nullptr;
    if (context->assignExpr()) {
        res = any_cast<node_ptr_t>(visitAssignExpr(context->assignExpr()));
    } else if (context->ctrlExpr()) {
        res = any_cast<node_ptr_t>(visitCtrlExpr(context->ctrlExpr()));
    }
    leave("DataExpr");
    return res;
}

/*
assignExpr
    : logicalOrExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=') logicalOrExpr)?
    ;
*/
any Constructor::visitAssignExpr(OpenCMLParser::AssignExprContext *context) {
    enter("AssignExpr");
    std::string strOp;
    AssignExpr::AssignOp op;
    node_ptr_t lhsNode = any_cast<node_ptr_t>(
        visitLogicalOrExpr(context->logicalOrExpr(0))); // get the left hand side of the assign expr
    for (size_t i = 1; i < context->logicalOrExpr().size(); i++) {
        strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "=") {
            op = AssignExpr::AssignOp::ASSIGN;
        } else if (strOp == "+=") {
            op = AssignExpr::AssignOp::ADD_ASSIGN;
        } else if (strOp == "-=") {
            op = AssignExpr::AssignOp::MINUS_ASSIGN;
        } else if (strOp == "*=") {
            op = AssignExpr::AssignOp::MUL_ASSIGN;
        } else if (strOp == "/=") {
            op = AssignExpr::AssignOp::DIV_ASSIGN;
        } else if (strOp == "%=") {
            op = AssignExpr::AssignOp::MOD_ASSIGN;
        } else if (strOp == "^=") {
            op = AssignExpr::AssignOp::POW_ASSIGN;
        } else if (strOp == "@=") {
            op = AssignExpr::AssignOp::MATRIX_MUL_ASSIGN;
        } else if (strOp == "&=") {
            op = AssignExpr::AssignOp::AND_ASSIGN;
        } else if (strOp == "|=") {
            op = AssignExpr::AssignOp::OR_ASSIGN;
        } else { // invalid operator
            op = AssignExpr::AssignOp::INVALID;
        }
        node_ptr_t rhsNode = any_cast<node_ptr_t>(
            visitLogicalOrExpr(context->logicalOrExpr(i))); // get the right hand side of the assign expr
        node_ptr_t assignNode = createNode<AssignExpr>(op);
        *assignNode << lhsNode << rhsNode;
        lhsNode = assignNode; // update the left hand side of the assign expr
    }
    leave("AssignExpr");
    return lhsNode;
}

/*
logicalOrExpr
    : logicalAndExpr ('||' logicalAndExpr)*
    ;
*/
any Constructor::visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) {
    enter("LogicalOrExpr");
    node_ptr_t lhsNode = any_cast<node_ptr_t>(
        visitLogicalAndExpr(context->logicalAndExpr(0))); // get the left hand side of the logical or expr
    for (size_t i = 1; i < context->logicalAndExpr().size(); ++i) {
        node_ptr_t rhsNode = any_cast<node_ptr_t>(
            visitLogicalAndExpr(context->logicalAndExpr(i))); // get the right hand side of the logical or expr
        node_ptr_t logicalOrNode = createNode<LogicalOrExpr>();
        *logicalOrNode << lhsNode << rhsNode;
        lhsNode = logicalOrNode;
    }
    leave("LogicalOrExpr");
    return lhsNode;
}

/*
logicalAndExpr
    : equalityExpr ('&&' equalityExpr)*
    ;
*/
any Constructor::visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) {
    enter("LogicalAndExpr");
    node_ptr_t lhsNode = any_cast<node_ptr_t>(
        visitEqualityExpr(context->equalityExpr(0))); // get the left hand side of the logical and expr
    for (size_t i = 1; i < context->equalityExpr().size(); ++i) {
        node_ptr_t rhsNode = any_cast<node_ptr_t>(
            visitEqualityExpr(context->equalityExpr(i))); // get the right hand side of the logical and expr
        node_ptr_t logicalAndNode = createNode<LogicalAndExpr>();
        *logicalAndNode << lhsNode << rhsNode;
        lhsNode = logicalAndNode;
    }
    leave("LogicalAndExpr");
    return lhsNode;
}

/*
equalityExpr
    : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
    ;
*/
any Constructor::visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) {
    enter("EqualityExpr");
    EqualityExpr::EqualityOp op;
    node_ptr_t lhsNode = any_cast<node_ptr_t>(
        visitRelationalExpr(context->relationalExpr(0))); // get the left hand side of the equality expr
    for (size_t i = 1; i < context->relationalExpr().size(); ++i) {
        std::string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "===") {
            op = EqualityExpr::EqualityOp::STRICT_EQUAL;
        } else if (strOp == "!==") {
            op = EqualityExpr::EqualityOp::NOT_STRICT_EQUAL;
        } else if (strOp == "==") {
            op = EqualityExpr::EqualityOp::EQUAL;
        } else if (strOp == "!=") {
            op = EqualityExpr::EqualityOp::NOT_EQUAL;
        } else { // invalid operator
            op = EqualityExpr::EqualityOp::INVALID;
        }

        node_ptr_t rhsNode = any_cast<node_ptr_t>(
            visitRelationalExpr(context->relationalExpr(i))); // get the right hand side of the equality expr
        node_ptr_t equalityNode = createNode<EqualityExpr>(op);
        *equalityNode << lhsNode << rhsNode;
        lhsNode = equalityNode; // update the left hand side
    }
    leave("EqualityExpr");
    return lhsNode;
}

/*
relationalExpr
    : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
    ;
*/
any Constructor::visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) {
    enter("RelationalExpr");
    RelationalExpr::RelationalOp op;
    node_ptr_t lhsNode = any_cast<node_ptr_t>(visitAdditiveExpr(context->additiveExpr(0)));
    for (size_t i = 1; i < context->additiveExpr().size(); ++i) {
        std::string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "<") {
            op = RelationalExpr::RelationalOp::LESS;
        } else if (strOp == ">") {
            op = RelationalExpr::RelationalOp::GREATER;
        } else if (strOp == "<=") {
            op = RelationalExpr::RelationalOp::LESS_EQUAL;
        } else if (strOp == ">=") {
            op = RelationalExpr::RelationalOp::GREATER_EQUAL;
        } else {
            op = RelationalExpr::RelationalOp::INVALID;
        }
        node_ptr_t rhsNode = any_cast<node_ptr_t>(visitAdditiveExpr(context->additiveExpr(i)));
        node_ptr_t relationalNode = createNode<RelationalExpr>(op);
        *relationalNode << lhsNode << rhsNode;
        lhsNode = relationalNode;
    }
    leave("RelationalExpr");
    return lhsNode;
}

/*
additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;
*/
any Constructor::visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) {
    enter("AdditiveExpr");
    AdditiveExpr::AdditiveOp op;
    node_ptr_t lhsNode = any_cast<node_ptr_t>(visitMultiplicativeExpr(context->multiplicativeExpr(0)));
    for (size_t i = 1; i < context->multiplicativeExpr().size(); i++) {
        std::string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "+") {
            op = AdditiveExpr::AdditiveOp::ADD;
        } else if (strOp == "-") {
            op = AdditiveExpr::AdditiveOp::MINUS;
        } else {
            op = AdditiveExpr::AdditiveOp::INVALID;
        }
        node_ptr_t rhsNode = any_cast<node_ptr_t>(visitMultiplicativeExpr(context->multiplicativeExpr(i)));
        node_ptr_t additiveNode = createNode<AdditiveExpr>(op);
        *additiveNode << lhsNode << rhsNode;
        lhsNode = additiveNode;
    }
    leave("AdditiveExpr");
    return lhsNode;
}

/*
multiplicativeExpr
    : nullableExpr (('*' | '/' | '^' | '@' | '%') nullableExpr)*
    ;
*/
any Constructor::visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) {
    enter("MultiplicativeExpr");
    MultiplicativeExpr::MultiplicativeOp op;
    node_ptr_t lhsNode = any_cast<node_ptr_t>(visitNullableExpr(context->nullableExpr(0)));
    for (size_t i = 1; i < context->nullableExpr().size(); i++) {
        std::string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "*") {
            op = MultiplicativeExpr::MultiplicativeOp::MUL;
        } else if (strOp == "/") {
            op = MultiplicativeExpr::MultiplicativeOp::DIV;
        } else if (strOp == "^") {
            op = MultiplicativeExpr::MultiplicativeOp::POW;
        } else if (strOp == "@") {
            op = MultiplicativeExpr::MultiplicativeOp::MATRIX_MUL;
        } else if (strOp == "%") {
            op = MultiplicativeExpr::MultiplicativeOp::MOD;
        } else {
            op = MultiplicativeExpr::MultiplicativeOp::INVALID;
        }
        node_ptr_t mulNode = createNode<MultiplicativeExpr>(op);
        node_ptr_t rhsNode = any_cast<node_ptr_t>(visitNullableExpr(context->nullableExpr(i)));
        *mulNode << lhsNode << rhsNode;
        lhsNode = mulNode;
    }
    leave("MultiplicativeExpr");
    return lhsNode;
}

/*
nullableExpr
    : unaryExpr (('??' | '!!') waitExpr)?
    ;
*/
any Constructor::visitNullableExpr(OpenCMLParser::NullableExprContext *context) {
    enter("NullableExpr");
    node_ptr_t res = any_cast<node_ptr_t>(visitUnaryExpr(context->unaryExpr()));

    if (context->children.size() > 1) {
        std::string strOp = context->children[1]->getText();
        NullableExpr::NullableOp op;
        if (strOp == "??") {
            op = NullableExpr::NullableOp::QUESTION_QUESTION;
        } else if (strOp == "!!") {
            op = NullableExpr::NullableOp::NOT_NOT;
        } else {
            op = NullableExpr::NullableOp::INVALID;
        }

        if (context->waitExpr()) {
            node_ptr_t nullableNode = createNode<NullableExpr>(op);
            *nullableNode << res << any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr()));
            res = nullableNode;
        }
    } else {
        if (context->waitExpr()) {
            node_ptr_t nullableNode = createNode<NullableExpr>(NullableExpr::NullableOp::INVALID);
            *nullableNode << res << any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr()));
            res = nullableNode;
        }
    }

    leave("NullableExpr");
    return res;
}

/*
unaryExpr
    : linkExpr ((AS | IS) typeExpr)?
    | ('!' | '-' | '~') linkExpr
    ;
*/
any Constructor::visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) {
    enter("UnaryExpr");
    node_ptr_t res = nullptr;

    int num = context->getAltNumber();

    switch (num) {
    case 1: {
        UnaryExpr::TypeOp op;
        if (context->AS()) {
            op = UnaryExpr::TypeOp::AS;
        } else if (context->IS()) {
            op = UnaryExpr::TypeOp::IS;
        } else {
            op = UnaryExpr::TypeOp::INVALID;
        }
        node_ptr_t linkNode = any_cast<node_ptr_t>(visitLinkExpr(context->linkExpr()));
        res = createNode<UnaryExpr>(UnaryExpr::UnaryOp::INVALID, op);
        *res << linkNode;
        break;
    }
    case 2: {
        UnaryExpr::UnaryOp op;
        std::string strOp = context->children[0]->getText();
        if (strOp == "!") {
            op = UnaryExpr::UnaryOp::NOT;
        } else if (strOp == "-") {
            op = UnaryExpr::UnaryOp::LINK;
        } else if (strOp == "~") {
            op = UnaryExpr::UnaryOp::BIT_NOT;
        } else {
            op = UnaryExpr::UnaryOp::INVALID;
        }
        node_ptr_t linkNode = any_cast<node_ptr_t>(visitLinkExpr(context->linkExpr()));
        res = createNode<UnaryExpr>(op, UnaryExpr::TypeOp::INVALID);
        *res << linkNode;
        break;
    }
    }
    leave("UnaryExpr");
    return res;
}

/*
linkExpr
    : bindExpr (('->' | '?->') bindExpr)*
    ;
*/
any Constructor::visitLinkExpr(OpenCMLParser::LinkExprContext *context) {
    enter("LinkExpr");
    node_ptr_t lhsNode = any_cast<node_ptr_t>(visitBindExpr(context->bindExpr(0)));
    LinkExpr::LinkOp op;
    for (size_t i = 1; i < context->bindExpr().size(); i++) {
        std::string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "->") {
            op = LinkExpr::LinkOp::ARROW;
        } else if (strOp == "?->") {
            op = LinkExpr::LinkOp::QUESTION_ARROW;
        } else {
            op = LinkExpr::LinkOp::INVALID;
        }
        node_ptr_t rhsNode = any_cast<node_ptr_t>(visitBindExpr(context->bindExpr(i)));
        node_ptr_t linkNode = createNode<LinkExpr>(op);
        *linkNode << lhsNode << rhsNode;
        lhsNode = linkNode;
    }
    leave("LinkExpr");
    return lhsNode;
}

/*
bindExpr
    : withExpr (('..' | '?..') withExpr)*
    ;
*/
any Constructor::visitBindExpr(OpenCMLParser::BindExprContext *context) {
    enter("BindExpr");
    node_ptr_t lhsNode = any_cast<node_ptr_t>(visitWithExpr(context->withExpr(0)));
    BindExpr::BindOp op;
    for (size_t i = 1; i < context->withExpr().size(); i++) {
        std::string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "..") {
            op = BindExpr::BindOp::DOUBLE_DOT;
        } else if (strOp == "?..") {
            op = BindExpr::BindOp::QUESTION_DOUBLE_DOT;
        } else {
            op = BindExpr::BindOp::INVALID;
        }
        node_ptr_t rhsNode = any_cast<node_ptr_t>(visitWithExpr(context->withExpr(i)));
        node_ptr_t bindNode = createNode<BindExpr>(op);
        *bindNode << lhsNode << rhsNode;
        lhsNode = bindNode;
    }
    leave("BindExpr");
    return lhsNode;
}

/*
withExpr
    : annoExpr (('.' | '?.') annoExpr)*
    ;
*/
any Constructor::visitWithExpr(OpenCMLParser::WithExprContext *context) {
    enter("WithExpr");
    if (context->children.size() == 1) { // no with expr, return the anno expr
        return any_cast<node_ptr_t>(visitAnnoExpr(context->annoExpr(0)));
    }
    node_ptr_t lhsNode = any_cast<node_ptr_t>(visitAnnoExpr(context->annoExpr(0)));
    WithExpr::WithOp op;
    for (size_t i = 1; i < context->annoExpr().size(); i++) {
        std::string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == ".") {
            op = WithExpr::WithOp::DOT;
        } else if (strOp == "?.") {
            op = WithExpr::WithOp::QUESTION_DOT;
        } else {
            op = WithExpr::WithOp::INVALID;
        }
        node_ptr_t rhsNode = any_cast<node_ptr_t>(visitAnnoExpr(context->annoExpr(i)));
        node_ptr_t withNode = createNode<WithExpr>(op);
        *withNode << lhsNode << rhsNode;
        lhsNode = withNode;
    }
    leave("WithExpr");
    return lhsNode;
}

/*
annoExpr
    : primaryData ({isAdjacent()}? (memberAccess | parentArgues | angledValues | '!'))*
    ;
*/
any Constructor::visitAnnoExpr(OpenCMLParser::AnnoExprContext *context) {
    enter("AnnoExpr");
    node_ptr_t lhsNode = any_cast<node_ptr_t>(visitPrimaryData(context->primaryData()));
    for (size_t i = 1; i < context->children.size(); i++) {
        auto child = context->children[i];
        if (child->getText() == "!") {
            node_ptr_t annoExprNode = createNode<AnnoExpr>(true);
            *annoExprNode << lhsNode;
            lhsNode = annoExprNode;
        } else if (!context->memberAccess().empty()) {
            for (auto &memberAccess : context->memberAccess()) {
                node_ptr_t res = any_cast<node_ptr_t>(visitMemberAccess(memberAccess));
                node_ptr_t annoExprNode = createNode<AnnoExpr>(false);
                *annoExprNode << lhsNode << res;
                lhsNode = annoExprNode;
            }
        } else if (!context->parentArgues().empty()) {
            for (auto &parentArgue : context->parentArgues()) {
                node_ptr_t res = any_cast<node_ptr_t>(visitParentArgues(parentArgue));
                node_ptr_t annoExprNode = createNode<AnnoExpr>(false);
                *annoExprNode << lhsNode << res;
                lhsNode = annoExprNode;
            }
        } else if (!context->angledValues().empty()) {
            for (auto &angledValue : context->angledValues()) {
                node_ptr_t res = any_cast<node_ptr_t>(visitAngledValues(angledValue));
                node_ptr_t annoExprNode = createNode<AnnoExpr>(false);
                *annoExprNode << lhsNode << res;
                lhsNode = annoExprNode;
            }
        }
    }
    leave("AnnoExpr");
    return lhsNode;
}

/*
dictExpr
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct of dict is immutable
    ;
*/
any Constructor::visitDictExpr(OpenCMLParser::DictExprContext *context) {
    enter("DictExpr");
    node_ptr_t dictNode = createNode<DictExpr>(); // create the dict expr node
    if (context->pairedValues()) {
        *dictNode << any_cast<node_ptr_t>(
            visitPairedValues(context->pairedValues())); // get the paired values of the dict expr node
    }
    leave("DictExpr");
    return dictNode;
}

/*
listExpr
    : '[' ((indexValues ','?) | waitExpr FOR identRef IN waitExpr (IF waitExpr)?)? ']'
    ;
*/
any Constructor::visitListExpr(OpenCMLParser::ListExprContext *context) {
    enter("ListExpr");
    node_ptr_t listNode = createNode<ListExpr>();
    if (context->indexValues()) {
        *listNode << any_cast<node_ptr_t>(visitIndexValues(context->indexValues()));
    } else {
        for (auto &waitExpr : context->waitExpr()) {
            *listNode << any_cast<node_ptr_t>(visitWaitExpr(waitExpr));
        }
        if (context->identRef()) {
            *listNode << any_cast<node_ptr_t>(visitIdentRef(context->identRef()));
        }
        if (context->waitExpr(1)) {
            *listNode << any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr(1)));
        }
        if (context->waitExpr(2)) {
            *listNode << any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr(2)));
        }
    }
    leave("ListExpr");
    return listNode;
}

/*
primaryData
    : identRef
    | literal
    | listExpr
    | dictExpr
    | '(' waitExpr ')'
    | '(' valueList? ','? ')' // for tuple
    | lambdaExpr
    ;
*/
any Constructor::visitPrimaryData(OpenCMLParser::PrimaryDataContext *context) {
    enter("PrimaryData");
    node_ptr_t res = nullptr;
    if (context->identRef()) {
        res = any_cast<node_ptr_t>(visitIdentRef(context->identRef()));
    } else if (context->literal()) {
        res = any_cast<node_ptr_t>(visitLiteral(context->literal()));
    } else if (context->listExpr()) {
        res = any_cast<node_ptr_t>(visitListExpr(context->listExpr()));
    } else if (context->dictExpr()) {
        res = any_cast<node_ptr_t>(visitDictExpr(context->dictExpr()));
    } else if (context->waitExpr()) {
        res = any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr()));
    } else if (context->valueList()) {
        res = any_cast<node_ptr_t>(visitValueList(context->valueList()));
    } else if (context->lambdaExpr()) {
        res = any_cast<node_ptr_t>(visitLambdaExpr(context->lambdaExpr()));
    }
    leave("PrimaryData");
    return res;
}

/*
literal
    : INTEGER
    | REAL
    | STRING
    | MULTI_STR
    | FSTRING
    | TRUE
    | FALSE
    | NULL
    ;
*/
any Constructor::visitLiteral(OpenCMLParser::LiteralContext *context) {
    enter("Literal: " + to_string(context->getAltNumber()));
    data_ptr_t data = nullptr;
    if (context->INTEGER()) {
        data = dynamic_pointer_cast<Data>(
            make_shared<PrimaryData<int64_t>>(parseNumber<int64_t>(context->INTEGER()->getText())));
    } else if (context->REAL()) {
        data = dynamic_pointer_cast<Data>(
            make_shared<PrimaryData<double>>(parseNumber<double>(context->REAL()->getText())));
    } else if(context->STRING()) {
        const auto &text = context->STRING()->getText();
        data = dynamic_pointer_cast<Data>(make_shared<StringData>(text.substr(1, text.size() - 2))); 
    } else if(context->MULTI_STR()) {
        const auto &text = context->MULTI_STR()->getText();
        data = dynamic_pointer_cast<Data>(make_shared<StringData>(text.substr(3, text.size() - 6))); 
    } else if(context->FSTRING()) {
        // TODO: Implement FSTRING
        const auto &text = context->FSTRING()->getText();
        data = dynamic_pointer_cast<Data>(make_shared<StringData>(text.substr(2, text.size() - 3))); 
    } else if(context->TRUE()) {
        data = dynamic_pointer_cast<Data>(make_shared<PrimaryData<bool>>(true)); 
    } else if(context->FALSE()) {
        data = dynamic_pointer_cast<Data>(make_shared<PrimaryData<bool>>(false)); 
    } else if(context->NULL_()) {
        data = dynamic_pointer_cast<Data>(make_shared<NullData>());
    }
any res = createNode<Literal>(data);
leave("Literal");
return res;
}
;

/*
typeExpr
    : unionType ('?' unionType?)?
    ;
*/
any Constructor::visitTypeExpr(OpenCMLParser::TypeExprContext *context) {
    enter("TypeExpr");
    node_ptr_t res = nullptr;
    if (context->unionType(0)) {
        res = any_cast<node_ptr_t>(visitUnionType(context->unionType(0)));
    }
    if (context->unionType(1)) {
        node_ptr_t typeNode = createNode<TypeExpr>();
        *typeNode << res << any_cast<node_ptr_t>(visitUnionType(context->unionType(1)));
        res = typeNode;
    }
    leave("TypeExpr");
    return res;
}

/*
unionType
    : interType ('|' interType)*
    ;
*/
any Constructor::visitUnionType(OpenCMLParser::UnionTypeContext *context) {
    enter("UnionType");
    node_ptr_t lhsNode = any_cast<node_ptr_t>(visitInterType(context->interType(0)));
    for (size_t i = 1; i < context->interType().size(); ++i) {
        node_ptr_t rhsNode = any_cast<node_ptr_t>(visitInterType(context->interType(i)));
        node_ptr_t unionTypeNode = createNode<UnionType>();
        *unionTypeNode << lhsNode << rhsNode;
        lhsNode = unionTypeNode;
    }
    leave("UnionType");
    return lhsNode;
}

/*
interType
    : typeUnit (('&' | '^') typeUnit)*
    ;
*/
any Constructor::visitInterType(OpenCMLParser::InterTypeContext *context) {
    enter("InterType");
    node_ptr_t lhsNode = any_cast<node_ptr_t>(visitTypeUnit(context->typeUnit(0)));
    for (size_t i = 1; i < context->typeUnit().size(); ++i) {
        std::string strOp = context->children[i * 2 - 1]->getText();
        InterType::InterOp op;
        if (strOp == "&") {
            op = InterType::InterOp::AND;
        } else if (strOp == "^") {
            op = InterType::InterOp::XOR;
        } else {
            op = InterType::InterOp::INVALID;
        }
        node_ptr_t rhsNode = any_cast<node_ptr_t>(visitTypeUnit(context->typeUnit(i)));
        node_ptr_t interTypeNode = createNode<InterType>(op);
        *interTypeNode << lhsNode << rhsNode;
        lhsNode = interTypeNode;
    }
    leave("InterType");
    return lhsNode;
}

/*
typeUnit : (identDef OF)? listType ;
*/
any Constructor::visitTypeUnit(OpenCMLParser::TypeUnitContext *context) {
    enter("TypeUnit");
    node_ptr_t res = nullptr;
    if (context->identDef()) {
        throw std::runtime_error("visitTypeUnit: identDef is not implemented yet");
    } else if (context->listType()) {
        res = any_cast<node_ptr_t>(visitListType(context->listType()));
    }
    leave("TypeUnit");
    return res;
}

/*
listType
    : argsType ('[' ']')*
    ;
*/
any Constructor::visitListType(OpenCMLParser::ListTypeContext *context) {
    enter("ListType");
    node_ptr_t res = nullptr;
    if (context->argsType()) {
        res = any_cast<node_ptr_t>(visitArgsType(context->argsType()));
    }
    leave("ListType");
    return res;
}

/*
typeOrData : typeExpr | primaryData ;
*/
any Constructor::visitTypeOrData(OpenCMLParser::TypeOrDataContext *context) {
    enter("TypeOrData");
    node_ptr_t res = nullptr;
    if (context->typeExpr()) {
        res = any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr()));
    } else if (context->primaryData()) {
        res = any_cast<node_ptr_t>(visitPrimaryData(context->primaryData()));
    }
    leave("TypeOrData");
    return res;
}

/*
argsType
    : primaryType ('<' typeOrData (',' typeOrData)* '>')?
    ;
*/
any Constructor::visitArgsType(OpenCMLParser::ArgsTypeContext *context) {
    enter("ArgsType");
    node_ptr_t argsTypeNode = createNode<ArgsType>();
    node_ptr_t primaryTypeNode = any_cast<node_ptr_t>(visitPrimaryType(context->primaryType()));
    *argsTypeNode << primaryTypeNode;
    for (auto &typeOrData : context->typeOrData()) {
        node_ptr_t typeOrDataNode = any_cast<node_ptr_t>(visitTypeOrData(typeOrData));
        *argsTypeNode << typeOrDataNode;
    }
    leave("ArgsType");
    return argsTypeNode;
}

/*
primaryType
    : INNER_ATOM_TYPE
    | dictType
    | identRef
    | '(' typeExpr ')'
    | tupleType
    | lambdaType
    | TYPEOF waitExpr
    | TYPEAS identDef
    ;
*/
any Constructor::visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) {
    enter("PrimaryType");
    node_ptr_t res = nullptr;
    if (context->INNER_ATOM_TYPE()) {
        std::string type = context->INNER_ATOM_TYPE()->getText();
        res = createNode<PrimaryType_>(type, "");
    } else if (context->dictType()) {
        res = any_cast<node_ptr_t>(visitDictType(context->dictType()));
    } else if (context->identRef()) {
        res = any_cast<node_ptr_t>(visitIdentRef(context->identRef()));
    } else if (context->typeExpr()) {
        res = any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr()));
    } else if (context->tupleType()) {
        res = any_cast<node_ptr_t>(visitTupleType(context->tupleType()));
    } else if (context->lambdaType()) {
        res = any_cast<node_ptr_t>(visitLambdaType(context->lambdaType()));
    } else if (context->waitExpr()) {
        res = any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr()));
    } else if (context->identDef()) {
        std::string ident = context->identDef()->getText();
        res = createNode<PrimaryType_>("", ident);
    }
    leave("PrimaryType");
    return res;
}

/*
dictType
    : '{' (keyTypePair (',' keyTypePair)*)? ','? '}'
    ;
*/
any Constructor::visitDictType(OpenCMLParser::DictTypeContext *context) {
    enter("DictType");
    node_ptr_t lhsNode = any_cast<node_ptr_t>(visitKeyTypePair(context->keyTypePair(0)));
    for (size_t i = 1; i < context->keyTypePair().size(); ++i) {
        node_ptr_t rhsNode = any_cast<node_ptr_t>(visitKeyTypePair(context->keyTypePair(i)));
        node_ptr_t dictTypeNode = createNode<DictType>();
        *dictTypeNode << lhsNode << rhsNode;
        lhsNode = dictTypeNode;
    }
    leave("DictType");
    return lhsNode;
}

/*
typeList
    : typeExpr (',' typeExpr)*
    ;
*/
any Constructor::visitTypeList(OpenCMLParser::TypeListContext *context) {
    enter("TypeList");
    node_ptr_t res = any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr(0)));
    if (context->children.size() > 1) {
        node_ptr_t typeListNode = createNode<TypeList>();
        *typeListNode << res;
        res = typeListNode;
        for (size_t i = 1; i < context->typeExpr().size(); ++i) {
            node_ptr_t typeExprNode = any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr(i)));
            *res << typeExprNode;
        }
    }
    leave("TypeList");
    return res;
}

/*
tupleType
    : '(' typeList? ','? ')'
    ;
*/
any Constructor::visitTupleType(OpenCMLParser::TupleTypeContext *context) {
    enter("TupleType");
    node_ptr_t res =
        any_cast<node_ptr_t>(visitTypeList(context->typeList())); // get the type list of the tuple type node;
    leave("TupleType");
    return res;
}

/*
lambdaType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;
*/
any Constructor::visitLambdaType(OpenCMLParser::LambdaTypeContext *context) {
    enter("LambdaType");
    node_ptr_t res = nullptr;
    auto modifier = context->modifiers();
    std::vector<LambdaType::LambdaTypeModifiers> lambdaModifiers;
    if (context->modifiers()) {
        if (modifier->ATOMIC().size() > 0) {
            lambdaModifiers.push_back(LambdaType::LambdaTypeModifiers::ATOMIC);
        } else if (modifier->SHARED().size() > 0) {
            lambdaModifiers.push_back(LambdaType::LambdaTypeModifiers::SHARED);
        } else if (modifier->SYNC().size() > 0) {
            lambdaModifiers.push_back(LambdaType::LambdaTypeModifiers::SYNC);
        } else if (modifier->MACRO().size() > 0) {
            lambdaModifiers.push_back(LambdaType::LambdaTypeModifiers::MACRO);
        } else {
            lambdaModifiers.push_back(LambdaType::LambdaTypeModifiers::INVALID);
        }
    }
    res = createNode<LambdaType>(lambdaModifiers);
    if (context->angledParams()) {
        *res << any_cast<node_ptr_t>(visitAngledParams(context->angledParams()));
    }
    if (context->parentParams()) {
        *res << any_cast<node_ptr_t>(visitParentParams(context->parentParams()));
    }
    if (context->typeExpr()) {
        *res << any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr()));
    }
    leave("LambdaType");
    return res;
}

/*
identDef : IDENTIFIER ;
*/
any Constructor::visitIdentDef(OpenCMLParser::IdentDefContext *context) {
    enter("IdentDef");
    std::string ident = context->IDENTIFIER()->getText();
    node_ptr_t res = createNode<IdentDef>(ident);
    leave("IdentDef");
    return res;
}

/*
identRef : (IDENTIFIER '::')* IDENTIFIER ;
*/
any Constructor::visitIdentRef(OpenCMLParser::IdentRefContext *context) {
    enter("IdentRef");
    std::vector<std::string> belongsTo;
    std::string ident;
    if (context->IDENTIFIER().size() == 1) { // only ident
        belongsTo = {};
        ident = context->IDENTIFIER(0)->getText();
    } else {
        for (size_t i = 0; i < context->IDENTIFIER().size() - 1; i++) {
            belongsTo.push_back(context->IDENTIFIER(i)->getText());
        }
        ident = context->children[context->IDENTIFIER().size() - 1]->getText();
    }
    node_ptr_t res = createNode<IdentRef>(ident, belongsTo);
    ;
    leave("IdentRef");
    return res;
}
