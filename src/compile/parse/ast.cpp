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
 * Updated: Apr. 12, 2025
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
    any res;

    switch (context->getAltNumber()) {
    case 1: // letDecl
        res = visitLetDecl(context->letDecl());
        break;
    case 2: // useDecl
        res = visitUseDecl(context->useDecl());
        break;
    case 3: // funcDecl
        res = visitFuncDecl(context->funcDecl());
        break;
    case 4: // typeDecl
        res = visitTypeDecl(context->typeDecl());
        break;
    case 5: // enumDecl
        res = visitEnumDecl(context->enumDecl());
        break;
    case 6: // retStmt
        res = visitRetStmt(context->retStmt());
        break;
    case 7: // waitStmt
        res = visitWaitStmt(context->waitStmt());
        break;
    case 8: // dataExpr
        res = visitDataExpr(context->dataExpr());
        break;
    case 9: // stmtBlock
        res = visitStmtBlock(context->stmtBlock());
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
    if (context->identDef()) { // module decl with name
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
    std::string from;
    std::string ident;
    if (context->STRING()) { // import decl with name and from
        from = context->STRING()->getText();
    }
    node_ptr_t bracedIdents;
    if (context->identDef()) { // import decl with name and from
        ident = context->identDef()->getText();
    } else if (context->bracedIdents()) { // import decl with idents and from
        bracedIdents = any_cast<node_ptr_t>(visitBracedIdents(context->bracedIdents()));
    }
    node_ptr_t importNode = createNode<ImportDecl>(from, ident);
    *importNode << bracedIdents;
    leave("ImportDecl");
    return importNode;
}

/*
exportDecl : EXPORT (letDecl | typeDecl | bracedIdents) ;
*/
any Constructor::visitExportDecl(OpenCMLParser::ExportDeclContext *context) {
    enter("ExportDecl");
    node_ptr_t res;
    if (context->letDecl()) { // export decl with let decl
        res = any_cast<node_ptr_t>(visitLetDecl(context->letDecl()));
    } else if (context->typeDecl()) { // export decl with type decl
        res = any_cast<node_ptr_t>(visitTypeDecl(context->typeDecl()));
    } else if (context->bracedIdents()) { // export decl with braced idents
        res = any_cast<node_ptr_t>(visitBracedIdents(context->bracedIdents()));
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
    if (context->stmtList()) { // stmt block with stmt list
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
    node_ptr_t res;
    if (context->stmtBlock()) { // block expr with stmt block
        res = any_cast<node_ptr_t>(visitStmtBlock(context->stmtBlock()));
    } else if (context->waitExpr()) { // block expr with wait expr
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
    node_ptr_t res;
    if (context->stmtBlock()) { // wait stmt with stmt block
        res = any_cast<node_ptr_t>(visitStmtBlock(context->stmtBlock()));
    } else if (context->dataList()) { // wait stmt with data list
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
    std::string modifiers;
    std::string type;
    if (context->modifiers()) { // lambda expr with modifiers
        modifiers = context->modifiers()->getText();
    }
    if (context->typeExpr()) { // lambda expr with type expr
        type = context->typeExpr()->getText();
    }
    node_ptr_t lambdaNode = createNode<LambdaExpr>(modifiers, type);
    if (context->angledParams()) { // lambda expr with angled params
        *lambdaNode << any_cast<node_ptr_t>(visitAngledParams(context->angledParams()));
    }
    if (context->parentParams()) { // lambda expr with parent params
        *lambdaNode << any_cast<node_ptr_t>(visitParentParams(context->parentParams()));
    }
    if (context->blockExpr()) { // lambda expr with block expr
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
    node_ptr_t exportNode = createNode<ExportDecl>();
    std::string implMark;
    std::string modifiers;
    std::string identDef;
    std::string type;
    if (context->implMark()) { // func decl with impl mark
        implMark = context->implMark()->getText();
    }
    if (context->modifiers()) { // func decl with modifiers
        modifiers = context->modifiers()->getText();
    }
    if (context->identDef()) { // func decl with ident def
        identDef = context->identDef()->getText();
    }
    if (context->typeExpr()) { // func decl with type expr
        type = context->typeExpr()->getText();
    }
    node_ptr_t funcNode = createNode<FuncDecl>(implMark, modifiers, identDef, type);
    if (context->angledParams()) { // func decl with angled params
        *funcNode << any_cast<node_ptr_t>(visitAngledParams(context->angledParams()));
    }
    if (context->parentParams()) { // func decl with parent params
        *funcNode << any_cast<node_ptr_t>(visitParentParams(context->parentParams()));
    }
    if (context->typeExpr()) { // func decl with type expr
        *funcNode << any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr()));
    }
    if (context->stmtBlock()) { // func decl with stmt block
        *funcNode << any_cast<node_ptr_t>(visitStmtBlock(context->stmtBlock()));
    }
    node_ptr_t res;
    if (context->EXPORT()) { // func decl with export
        *exportNode << funcNode;
        res = exportNode;
    } else { // func decl without export
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
    node_ptr_t res;
    res = any_cast<node_ptr_t>(visitIdentList(context->identList()));
    leave("ParentIdents");
    return res;
}

/*
bracedIdents  : '{' identList? ','? '}' ;
*/
any Constructor::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {
    enter("BracedIdents");
    node_ptr_t res;
    res = any_cast<node_ptr_t>(visitIdentList(context->identList()));
    leave("BracedIdents");
    return res;
}

/*
carrier       : identList | parentIdents | bracedIdents | bracketIdents ;
*/
any Constructor::visitCarrier(OpenCMLParser::CarrierContext *context) {
    enter("Carrier");
    node_ptr_t res;
    if (context->identList()) { // carrier with ident list
        res = any_cast<node_ptr_t>(visitIdentList(context->identList()));
    } else if (context->parentIdents()) { // carrier with parent idents
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
    if (context->LET()) { // let decl with let
        type = context->LET()->getText();
    } else if (context->VAR()) { // let decl with var
        type = context->VAR()->getText();
    }
    node_ptr_t letNode = createNode<LetDecl>(type);
    if (context->carrier()) { // let decl with carrier
        *letNode << any_cast<node_ptr_t>(visitCarrier(context->carrier()));
    }
    if (context->typeList()) { // let decl with type list
        *letNode << any_cast<node_ptr_t>(visitTypeList(context->typeList()));
    }
    if (context->valueList()) { // let decl with value list
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
    if (context->identDef()) { // use decl with ident def
        identDef = context->identDef()->getText();
    }
    if (context->identRef()) { // use decl with ident ref
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
    if (context->RETURN()) { // ret stmt with return
        modifier = context->RETURN()->getText();
    }
    if (context->RAISE()) { // ret stmt with raise
        modifier = context->RAISE()->getText();
    }
    if (context->THROW()) { // ret stmt with throw
        modifier = context->THROW()->getText();
    }
    node_ptr_t retNode = createNode<RetStmt>(modifier);
    if (context->valueList()) { // ret stmt with value list
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
    std::string type;
    if (context->implMark()) { // type decl with impl mark
        implMark = context->implMark()->getText();
    }
    if (context->identDef()) { // type decl with ident def
        identDef = context->identDef()->getText();
    }
    if (context->typeExpr()) { // type decl with type expr
        type = context->typeExpr()->getText();
    }
    node_ptr_t typeNode = createNode<TypeDecl>(implMark, identDef, type);
    leave("TypeDecl");
    return typeNode;
}

/*
enumDecl   : ENUM identDef (OF typeExpr)? '=' '{' pairedValues ','? '}' ;
*/
any Constructor::visitEnumDecl(OpenCMLParser::EnumDeclContext *context) {
    enter("EnumDecl");
    std::string identDef;
    std::string type;
    if (context->identDef()) { // enum decl with ident def
        identDef = context->identDef()->getText();
    }
    if (context->typeExpr()) { // enum decl with type expr
        type = context->typeExpr()->getText();
    }
    node_ptr_t enumNode = createNode<EnumDecl>(identDef, type);
    if (context->pairedValues()) { // enum decl with paired values
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
    std::string implMark;
    if (context->INNER()) { // impl mark with inner
        implMark = context->INNER()->getText();
    } else if (context->OUTER()) { // impl mark with outer
        implMark = context->OUTER()->getText();
    }
    node_ptr_t implNode = createNode<ImplMark>(implMark);
    leave("ImplMark");
    return implNode;
}

/*
modifiers   : (ATOMIC | SHARED | SYNC | MACRO)+ ;
*/
any Constructor::visitModifiers(OpenCMLParser::ModifiersContext *context) {
    enter("Modifiers");
    std::vector<std::string> modifiers;
    if (context->ATOMIC().size() > 0) { // modifiers with atomic
        for (auto &modifier : context->ATOMIC()) {
            modifiers.push_back(modifier->getText());
        }
    } else if (context->SHARED().size() > 0) { // modifiers with shared
        for (auto &modifier : context->SHARED()) {
            modifiers.push_back(modifier->getText());
        }
    } else if (context->SYNC().size() > 0) { // modifiers with sync
        for (auto &modifier : context->SYNC()) {
            modifiers.push_back(modifier->getText());
        }
    } else if (context->MACRO().size() > 0) { // modifiers with macro
        for (auto &modifier : context->MACRO()) {
            modifiers.push_back(modifier->getText());
        }
    }
    node_ptr_t modNode = createNode<Modifiers>(modifiers);
    leave("Modifiers");
    return modNode;
}

/*
indexValue   : '...'? waitExpr ;
*/
any Constructor::visitIndexValue(OpenCMLParser::IndexValueContext *context) {
    enter("IndexValue");
    node_ptr_t res;
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
    std::string type;
    if (context->identDef()) { // key type pair with ident def
        ident = context->identDef()->getText();
    }
    if (context->typeExpr()) { // key type pair with type expr
        type = context->typeExpr()->getText();
    }
    node_ptr_t keyTypeNode = createNode<KeyTypePair>(ident, type);
    leave("KeyTypePair");
    return keyTypeNode;
}

/*
keyValuePair : identDef ':' waitExpr | '...' waitExpr ;
*/
any Constructor::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {
    enter("KeyValuePair");
    std::string ident;
    if (context->identDef()) { // key value pair with ident def
        ident = context->identDef()->getText();
    }
    node_ptr_t keyValuePairNode = createNode<KeyValuePair>(ident);
    if (context->waitExpr()) { // key value pair with wait expr
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
    std::string type;
    if (context->VAR()) { // key param pair with var
        isVar = true;
    }
    if (context->identDef()) { // key param pair with ident def
        ident = context->identDef()->getText();
    }
    if (context->typeExpr()) { // key param pair with type expr
        type = context->typeExpr()->getText();
    }
    node_ptr_t keyParamPairNode = createNode<KeyParamPair>(isVar, ident, type);
    if (context->waitExpr()) { // key param pair with wait expr
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
    for (const auto &dataExpr : context->dataExpr()) { // data list with data expr
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
    for (const auto &identDef : context->identDef()) { // ident list with ident def
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
    for (const auto &value : context->waitExpr()) { // value list with wait expr
        *valueListNode << any_cast<node_ptr_t>(visitWaitExpr(value));
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
    for (const auto &indexValue : context->indexValue()) { // index values with index value
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
    for (const auto &keyValuePair : context->keyValuePair()) { // paired values with paired value
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
    for (const auto &keyParamPair : context->keyParamPair()) { // paired params with paired param
        *pairedParamsNode << any_cast<node_ptr_t>(visitKeyParamPair(keyParamPair));
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
    if (context->indexValues()) { // argument list with index values
        *argumentListNode << any_cast<node_ptr_t>(visitIndexValues(context->indexValues()));
    }
    if (context->pairedValues()) { // argument list with paired values
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
    if (context->waitExpr(0)) { // member access with wait expr
        *memberAccessNode << any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr(0)));
    }
    if (context->waitExpr(1)) { // member access with wait expr 1
        *memberAccessNode << any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr(1)));
    }
    if (context->waitExpr(2)) { // member access with wait expr 2
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
    node_ptr_t res;
    res = any_cast<node_ptr_t>(visitPairedParams(context->pairedParams()));
    leave("ParentParams");
    return res;
}

/*
parentArgues : '(' argumentList? ','? ')' ;
*/
any Constructor::visitParentArgues(OpenCMLParser::ParentArguesContext *context) {
    enter("ParentArgues");
    node_ptr_t res;
    res = any_cast<node_ptr_t>(visitArgumentList(context->argumentList()));
    leave("ParentArgues");
    return res;
}

/*
angledParams : '<' pairedParams? ','? '>' ;
*/
any Constructor::visitAngledParams(OpenCMLParser::AngledParamsContext *context) {
    enter("AngledParams");
    node_ptr_t res;
    res = any_cast<node_ptr_t>(visitPairedParams(context->pairedParams()));
    leave("AngledParams");
    return res;
}

/*
angledValues : '<' argumentList? ','? '>' ;
*/
any Constructor::visitAngledValues(OpenCMLParser::AngledValuesContext *context) {
    enter("AngledValues");
    node_ptr_t res;
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
    if (context->WAIT()) { // wait expr with wait
        isWait = true;
    }
    node_ptr_t waitNode = createNode<WaitExpr>(isWait);
    if (context->WAIT()) { // wait expr with wait
        *waitNode << any_cast<node_ptr_t>(visitDataExpr(context->dataExpr()));
    } else { // wait expr without wait
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
    node_ptr_t res;
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
    node_ptr_t res;
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
    node_ptr_t res;
    if (context->assignExpr()) { // data expr with assign expr
        res = any_cast<node_ptr_t>(visitAssignExpr(context->assignExpr()));
    } else if (context->ctrlExpr()) { // data expr with ctrl expr
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
    for (size_t i = 1; i < context->children.size(); i++) {
        strOp = context->children[i * 2 - 1]->getText(); // get the operator of the assign expr
        if (strOp == "=") {                              // assign expr with equal
            op = AssignExpr::AssignOp::ASSIGN;
        } else if (strOp == "+=") { // assign expr with plus equal
            op = AssignExpr::AssignOp::ADD_ASSIGN;
        } else if (strOp == "-=") { // assign expr with minus equal
            op = AssignExpr::AssignOp::MINUS_ASSIGN;
        } else if (strOp == "*=") { // assign expr with multiply equal
            op = AssignExpr::AssignOp::MUL_ASSIGN;
        } else if (strOp == "/=") { // assign expr with divide equal
            op = AssignExpr::AssignOp::DIV_ASSIGN;
        } else if (strOp == "%=") { // assign expr with modulo equal
            op = AssignExpr::AssignOp::MOD_ASSIGN;
        } else if (strOp == "^=") { // assign expr with xor equal
            op = AssignExpr::AssignOp::POW_ASSIGN;
        } else if (strOp == "@=") { // assign expr with at equal
            op = AssignExpr::AssignOp::MATRIX_MUL_ASSIGN;
        } else if (strOp == "&=") { // assign expr with and equal
            op = AssignExpr::AssignOp::AND_ASSIGN;
        } else if (strOp == "|=") { // assign expr with or equal
            op = AssignExpr::AssignOp::OR_ASSIGN;
        } else { // invalid operator
            op = AssignExpr::AssignOp::INVALID;
        }
        node_ptr_t rhsNode = any_cast<node_ptr_t>(
            visitLogicalOrExpr(context->logicalOrExpr(i))); // get the right hand side of the assign expr
        node_ptr_t assignNode = createNode<AssignExpr>(op);
        *assignNode << lhsNode << rhsNode;
        lhsNode = assignNode;
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
        visitLogicalAndExpr(context->logicalAndExpr(0)));   // get the left hand side of the logical or expr
    for (size_t i = 1; i < context->children.size(); ++i) { // logical or expr with logical or expr
        node_ptr_t rhsNode = any_cast<node_ptr_t>(
            visitLogicalAndExpr(context->logicalAndExpr(i)));   // get the right hand side of the logical or expr
        node_ptr_t logicalOrNode = createNode<LogicalOrExpr>(); // create the logical or expr node
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
        visitEqualityExpr(context->equalityExpr(0)));       // get the left hand side of the logical and expr
    for (size_t i = 1; i < context->children.size(); ++i) { // logical or expr with logical and expr
        node_ptr_t rhsNode = any_cast<node_ptr_t>(
            visitEqualityExpr(context->equalityExpr(i)));         // get the right hand side of the logical and expr
        node_ptr_t logicalAndNode = createNode<LogicalAndExpr>(); // create the logical and expr node
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
        visitRelationalExpr(context->relationalExpr(0)));            // get the left hand side of the equality expr
    for (size_t i = 1; i < context->children.size(); ++i) {          // equality expr with equality expr
        std::string strOp = context->children[i * 2 - 1]->getText(); // get the operator of the equality expr
        if (strOp == "===") {                                        // equality expr with equal equal equal
            op = EqualityExpr::EqualityOp::STRICT_EQUAL;
        } else if (strOp == "!==") { // equality expr with not equal equal equal
            op = EqualityExpr::EqualityOp::NOT_STRICT_EQUAL;
        } else if (strOp == "==") { // equality expr with equal equal
            op = EqualityExpr::EqualityOp::EQUAL;
        } else if (strOp == "!=") { // equality expr with not equal
            op = EqualityExpr::EqualityOp::NOT_EQUAL;
        } else { // invalid operator
            op = EqualityExpr::EqualityOp::INVALID;
        }

        node_ptr_t rhsNode = any_cast<node_ptr_t>(
            visitRelationalExpr(context->relationalExpr(i)));   // get the right hand side of the equality expr
        node_ptr_t equalityNode = createNode<EqualityExpr>(op); // create the equality expr node
        *equalityNode << lhsNode << rhsNode; // add the left hand side and right hand side to the equality expr node
        lhsNode = equalityNode;              // update the left hand side to the equality expr node
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
    node_ptr_t lhsNode = any_cast<node_ptr_t>(
        visitAdditiveExpr(context->additiveExpr(0)));                // get the left hand side of the relational expr
    for (size_t i = 1; i < context->children.size(); ++i) {          // relational expr with relational expr
        std::string strOp = context->children[i * 2 - 1]->getText(); // get the operator of the relational expr
        if (strOp == "<") {                                          // relational expr with less than
            op = RelationalExpr::RelationalOp::LESS;
        } else if (strOp == ">") { // relational expr with greater than
            op = RelationalExpr::RelationalOp::GREATER;
        } else if (strOp == "<=") { // relational expr with less than or equal to
            op = RelationalExpr::RelationalOp::LESS_EQUAL;
        } else if (strOp == ">=") { // relational expr with greater than or equal to
            op = RelationalExpr::RelationalOp::GREATER_EQUAL;
        } else { // invalid operator
            op = RelationalExpr::RelationalOp::INVALID;
        }
        node_ptr_t rhsNode = any_cast<node_ptr_t>(
            visitAdditiveExpr(context->additiveExpr(i)));           // get the right hand side of the relational expr
        node_ptr_t relationalNode = createNode<RelationalExpr>(op); // create the relational expr node
        *relationalNode << lhsNode << rhsNode; // add the left hand side and right hand side to the relational expr node
        lhsNode = relationalNode;              // update the left hand side to the relational expr node
    }
    return lhsNode;
    leave("RelationalExpr");
}

/*
additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;
*/
any Constructor::visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) {
    enter("AdditiveExpr");
    AdditiveExpr::AdditiveOp op;
    node_ptr_t lhsNode = any_cast<node_ptr_t>(
        visitMultiplicativeExpr(context->multiplicativeExpr(0)));    // get the left hand side of the additive expr
    for (size_t i = 1; i < context->children.size(); i++) {          // additive expr with additive expr
        std::string strOp = context->children[i * 2 - 1]->getText(); // get the operator of the additive expr
        if (strOp == "+") {                                          // additive expr with plus
            op = AdditiveExpr::AdditiveOp::ADD;
        } else if (strOp == "-") { // additive expr with minus
            op = AdditiveExpr::AdditiveOp::SUB;
        }
        node_ptr_t rhsNode = any_cast<node_ptr_t>(
            visitMultiplicativeExpr(context->multiplicativeExpr(i))); // get the right hand side of the additive expr
        node_ptr_t additiveNode = createNode<AdditiveExpr>(op);       // create the additive expr node
        *additiveNode << lhsNode << rhsNode; // add the left hand side and right hand side to the additive expr node
        lhsNode = additiveNode;              // update the left hand side to the additive expr node
    }
    return lhsNode;
    leave("AdditiveExpr");
}

/*
multiplicativeExpr
    : nullableExpr (('*' | '/' | '^' | '@' | '%') nullableExpr)*
    ;
*/
any Constructor::visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) {
    enter("MultiplicativeExpr");
    MultiplicativeExpr::MultiplicativeOp op;
    node_ptr_t lhsNode = any_cast<node_ptr_t>(context->nullableExpr(0));
    for (size_t i = 1; i < context->children.size(); i++) {          // multiplicative expr with multiplicative expr
        std::string strOp = context->children[i * 2 - 1]->getText(); // get the operator
        if (strOp == "*") {                                          // multiplicative expr with mul
            op = MultiplicativeExpr::MultiplicativeOp::MUL;
        } else if (strOp == "/") { // multiplicative expr with div
            op = MultiplicativeExpr::MultiplicativeOp::DIV;
        } else if (strOp == "^") { // multiplicative expr with pow
            op = MultiplicativeExpr::MultiplicativeOp::POW;
        } else if (strOp == "@") { // multiplicative expr with bit and
            op = MultiplicativeExpr::MultiplicativeOp::MATRIX_MUL;
        } else if (strOp == "%") { // multiplicative expr with bit or
            op = MultiplicativeExpr::MultiplicativeOp::MOD;
        } else { // multiplicative expr with other operators
            op = MultiplicativeExpr::MultiplicativeOp::INVALID;
        }
        node_ptr_t mulNode = createNode<MultiplicativeExpr>(op);
        node_ptr_t rhsNode = any_cast<node_ptr_t>(visitNullableExpr(context->nullableExpr(i)));
        *mulNode << lhsNode << rhsNode;
        lhsNode = mulNode;
    }
    leave("MultiplicativeExpr");
    return lhsNode;
};

/*
nullableExpr
    : unaryExpr (('??' | '!!') waitExpr)?
    ;
*/
any Constructor::visitNullableExpr(OpenCMLParser::NullableExprContext *context) {
    enter("NullableExpr");
    node_ptr_t res = any_cast<node_ptr_t>(visitUnaryExpr(context->unaryExpr()));
    std::string strOp =
        context->children[context->children.size() - 1]->getText(); // get the operator of the nullable expr
    NullableExpr ::NullableOp op;
    if (strOp == "??") { // nullable expr with double question mark
        op = NullableExpr::NullableOp::QUESTION_QUESTION;
    } else if (strOp == "!!") { // nullable expr with double exclamation mark
        op = NullableExpr::NullableOp::NOT_NOT;
    } else { // invalid operator
        op = NullableExpr::NullableOp::INVALID;
    }
    if (context->waitExpr()) { // nullable expr with wait expr
        node_ptr_t nullableNode = createNode<NullableExpr>(op);
        *nullableNode << res << any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr()));
        res = nullableNode;
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
    node_ptr_t res;

    int num = context->getAltNumber();
    UnaryExpr::UnaryOp op;
    switch (num) {
    case 1: {
        if (context->AS()) { // unary expr with AS keyword
            op = UnaryExpr::UnaryOp::AS;
        } else if (context->IS()) { // unary expr with IS keyword
            op = UnaryExpr::UnaryOp::IS;
        } else { // invalid operator
            op = UnaryExpr::UnaryOp::INVALID;
        }
        node_ptr_t linkNode = any_cast<node_ptr_t>(visitLinkExpr(context->linkExpr()));
        res = createNode<UnaryExpr>(op);
        *res << linkNode;
        break;
    }
    case 2: {
        std::string strOp = context->children[0]->getText();
        if (strOp == "!") { // unary expr with exclamation mark
            op = UnaryExpr::UnaryOp::NOT;
        } else if (strOp == "-") { // unary expr with minus sign
            op = UnaryExpr::UnaryOp::LINK;
        } else if (strOp == "~") { // unary expr with tilde sign
            op = UnaryExpr::UnaryOp::BIT_NOT;
        } else { // invalid operator
            op = UnaryExpr::UnaryOp::INVALID;
        }
        node_ptr_t linkNode = any_cast<node_ptr_t>(visitLinkExpr(context->linkExpr()));
        res = createNode<UnaryExpr>(op);
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
    node_ptr_t lhsNode =
        any_cast<node_ptr_t>(visitBindExpr(context->bindExpr(0))); // get the left hand side of the link expr
    LinkExpr::LinkOp op;
    for (size_t i = 1; i < context->children.size(); i++) {          // link expr with link expr
        std::string strOp = context->children[i * 2 - 1]->getText(); // get the operator of the link expr
        if (strOp == "->") {                                         // link expr with arrow
            op = LinkExpr::LinkOp::ARROW;
        } else if (strOp == "?->") { // link expr with question arrow
            op = LinkExpr::LinkOp::QUESTION_ARROW;
        } else { // invalid operator
            op = LinkExpr::LinkOp::INVALID;
        }
        node_ptr_t rhsNode =
            any_cast<node_ptr_t>(visitBindExpr(context->bindExpr(i))); // get the right hand side of the link expr
        node_ptr_t linkNode = createNode<LinkExpr>(op);                // create the link expr node
        *linkNode << lhsNode << rhsNode; // add the left hand side and right hand side to the link expr node
        lhsNode = linkNode;              // update the left hand side to the link expr node
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
    node_ptr_t lhsNode =
        any_cast<node_ptr_t>(visitWithExpr(context->withExpr(0))); // get the left hand side of the bind expr
    BindExpr::BindOp op;
    for (size_t i = 1; i < context->children.size(); i++) {          // bind expr with bind expr
        std::string strOp = context->children[i * 2 - 1]->getText(); // get the operator of the bind expr
        if (strOp == "..") {                                         // bind expr with double dot
            op = BindExpr::BindOp::DOUBLE_DOT;
        } else if (strOp == "?..") { // bind expr with question double dot
            op = BindExpr::BindOp::QUESTION_DOUBLE_DOT;
        } else { // invalid operator
            op = BindExpr::BindOp::INVALID;
        }
        node_ptr_t rhsNode =
            any_cast<node_ptr_t>(visitWithExpr(context->withExpr(i))); // get the right hand side of the bind expr
        node_ptr_t bindNode = createNode<BindExpr>(op);                // create the bind expr node
        *bindNode << lhsNode << rhsNode; // add the left hand side and right hand side to the bind expr node
        lhsNode = bindNode;              // update the left hand side to the bind expr node
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
    node_ptr_t lhsNode =
        any_cast<node_ptr_t>(visitAnnoExpr(context->annoExpr(0))); // get the left hand side of the with expr
    WithExpr::WithOp op;
    for (size_t i = 1; i < context->children.size(); i++) {          // with expr with with expr
        std::string strOp = context->children[i * 2 - 1]->getText(); // get the operator of the with expr
        if (strOp == ".") {                                          // with expr with dot
            op = WithExpr::WithOp::DOT;
        } else if (strOp == "?.") { // with expr with question dot
            op = WithExpr::WithOp::QUESTION_DOT;
        } else { // invalid operator
            op = WithExpr::WithOp::INVALID;
        }
        node_ptr_t rhsNode =
            any_cast<node_ptr_t>(visitAnnoExpr(context->annoExpr(i))); // get the right hand side of the with expr
        node_ptr_t withNode = createNode<WithExpr>(op);                // create the with expr node
        *withNode << lhsNode << rhsNode; // add the left hand side and right hand side to the with expr node
        lhsNode = withNode;              // update the left hand side to the with expr node
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
    node_ptr_t lhsNode =
        any_cast<node_ptr_t>(visitPrimaryData(context->primaryData())); // get the left hand side of the anno expr
    for (size_t i = 0; i < context->children.size(); i++) {             // anno expr with anno expr
        if (context->children[i]->getText() == "!") {                   // anno expr with exclamation mark
            node_ptr_t annoExprNode = createNode<AnnoExpr>(true);
            *annoExprNode << lhsNode;
            lhsNode = annoExprNode;
        } else {
            node_ptr_t res;
            res = any_cast<node_ptr_t>(visit(context->children[i]));
            node_ptr_t annoExprNode = createNode<AnnoExpr>(false);
            *annoExprNode << lhsNode << res;
            lhsNode = annoExprNode;
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
    node_ptr_t listNode = createNode<ListExpr>(); // create the list expr node
    if (context->indexValues()) {                 // list expr with index values
        *listNode << any_cast<node_ptr_t>(
            visitIndexValues(context->indexValues())); // get the index values of the list expr node
    } else {
        for (auto &waitExpr : context->waitExpr()) {                    // list expr with wait expr
            *listNode << any_cast<node_ptr_t>(visitWaitExpr(waitExpr)); // get the wait expr of the list expr node
        }
        if (context->identRef()) { // list expr with ident ref
            *listNode << any_cast<node_ptr_t>(
                visitIdentRef(context->identRef())); // get the ident ref of the list expr node
        }
        if (context->waitExpr(1)) { // list expr with wait expr
            *listNode << any_cast<node_ptr_t>(
                visitWaitExpr(context->waitExpr(1))); // get the wait expr of the list expr node
        }
        if (context->waitExpr(2)) { // list expr with wait expr
            *listNode << any_cast<node_ptr_t>(
                visitWaitExpr(context->waitExpr(2))); // get the wait expr of the list expr node
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
    node_ptr_t res;
    if (context->identRef()) {                                            // primary data with ident ref
        res = any_cast<node_ptr_t>(visitIdentRef(context->identRef()));   // get the ident ref of the primary data node
    } else if (context->literal()) {                                      // primary data with literal
        res = any_cast<node_ptr_t>(visitLiteral(context->literal()));     // get the literal of the primary data node
    } else if (context->listExpr()) {                                     // primary data with list expr
        res = any_cast<node_ptr_t>(visitListExpr(context->listExpr()));   // get the list expr of the primary data node
    } else if (context->dictExpr()) {                                     // primary data with dict expr
        res = any_cast<node_ptr_t>(visitDictExpr(context->dictExpr()));   // get the dict expr of the primary data node
    } else if (context->waitExpr()) {                                     // primary data with wait expr
        res = any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr()));   // get the wait expr of the primary data node
    } else if (context->valueList()) {                                    // primary data with value list
        res = any_cast<node_ptr_t>(visitValueList(context->valueList())); // get the value list of the primary data node
    } else if (context->lambdaExpr()) {                                   // primary data with lambda expr
        res = any_cast<node_ptr_t>(
            visitLambdaExpr(context->lambdaExpr())); // get the lambda expr of the primary data node
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
    enter("Literal");
    node_ptr_t res;
    if (context->INTEGER()) {                                          // literal with integer
        res = createNode<Literal>(Literal::LiteralType::INTERGER);     // create the integer literal node
    } else if (context->REAL()) {                                      // literal with real
        res = createNode<Literal>(Literal::LiteralType::REAL);         // create the real literal node
    } else if (context->STRING()) {                                    // literal with string
        res = createNode<Literal>(Literal::LiteralType::STRING);       // create the string literal node
    } else if (context->MULTI_STR()) {                                 // literal with multi string
        res = createNode<Literal>(Literal::LiteralType::MULTI_STR);    // create the multi string literal node
    } else if (context->FSTRING()) {                                   // literal with fstring
        res = createNode<Literal>(Literal::LiteralType::FSTRING);      // create the fstring literal node
    } else if (context->TRUE()) {                                      // literal with true
        res = createNode<Literal>(Literal::LiteralType::TRUE);         // create the true literal node
    } else if (context->FALSE()) {                                     // literal with false
        res = createNode<Literal>(Literal::LiteralType::FALSE);        // create the false literal node
    } else if (context->NULL_()) {                                     // literal with null
        res = createNode<Literal>(Literal::LiteralType::NULL_LITERAL); // create the null literal node
    }
    leave("Literal");
    return res;
}

/*
typeExpr
    : unionType ('?' unionType?)?
    ;
*/
any Constructor::visitTypeExpr(OpenCMLParser::TypeExprContext *context) {
    enter("TypeExpr");
    node_ptr_t res;
    if (context->unionType(0)) {                                           // type expr with union type
        res = any_cast<node_ptr_t>(visitUnionType(context->unionType(0))); // get the union type of the type expr node
    }
    if (context->unionType(1)) {                      // type expr with union type
        node_ptr_t typeNode = createNode<TypeExpr>(); // create the type expr node
        *typeNode << res
                  << any_cast<node_ptr_t>(
                         visitUnionType(context->unionType(1))); // get the union type of the type expr node
        res = typeNode;
    }
    leave("TypeExpr");
    return res;
}

/*
unionType
    : unionUnit ('|' unionUnit)*
    ;
*/
any Constructor::visitUnionType(OpenCMLParser::UnionTypeContext *context) {
    enter("UnionType");
    node_ptr_t lhsNode = any_cast<node_ptr_t>(visitUnionUnit(context->unionUnit(0)));
    for (size_t i = 1; i < context->children.size(); ++i) { // union type with union type
        node_ptr_t rhsNode = any_cast<node_ptr_t>(visitUnionUnit(context->unionUnit(i)));
        node_ptr_t unionTypeNode = createNode<UnionType>(); // create the union type node
        *unionTypeNode << lhsNode << rhsNode;
        lhsNode = unionTypeNode;
    }
    leave("UnionType");
    return lhsNode;
}

/*
unionUnit : (identDef OF)? listType ;
*/
any Constructor::visitUnionUnit(OpenCMLParser::UnionUnitContext *context) {
    enter("UnionUnit");
    node_ptr_t res =
        any_cast<node_ptr_t>(visitListType(context->listType()));   // get the list type of the union unit node;
    if (context->identDef()) {                                      // union unit with ident def
        std::string identdef = context->identDef()->getText();      // get the ident def of the union unit node;
        node_ptr_t unionUnitNode = createNode<UnionUnit>(identdef); // create the union unit node;
        *unionUnitNode << res;                                      // add the list type to the union unit node;
        res = unionUnitNode;                                        // update the res to the union unit node;
    }
    leave("UnionUnit");
    return res;
}

/*
listType
    : argsType ('[' ']')*
    ;
*/
any Constructor::visitListType(OpenCMLParser::ListTypeContext *context) {
    enter("ListType");
    node_ptr_t argsType =
        any_cast<node_ptr_t>(visitArgsType(context->argsType())); // get the args type of the list type node;
    node_ptr_t listTypeNode = createNode<ListType>(context->children.size()); // create the list type node;
    *listTypeNode << argsType; // add the args type to the list type node;
    leave("ListType");
    return listTypeNode;
}

/*
typeOrData : typeExpr | primaryData ;
*/
any Constructor::visitTypeOrData(OpenCMLParser::TypeOrDataContext *context) {
    enter("TypeOrData");
    node_ptr_t res;
    if (context->typeExpr()) {                                          // type or data with type expr
        res = any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr())); // get the type expr of the type or data node;
    } else if (context->primaryData()) {                                // type or data with primary data
        res = any_cast<node_ptr_t>(
            visitPrimaryData(context->primaryData())); // get the primary data of the type or data node;
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
    node_ptr_t argsTypeNode = createNode<ArgsType>(); // create the args type node;
    node_ptr_t primaryTypeNode =
        any_cast<node_ptr_t>(visitPrimaryType(context->primaryType())); // get the primary type of the args type node;
    *argsTypeNode << primaryTypeNode;
    for (auto &typeOrData : context->typeOrData()) { // args type with type or data
        node_ptr_t typeOrDataNode =
            any_cast<node_ptr_t>(visitTypeOrData(typeOrData)); // get the type or data of the args type node;
        *argsTypeNode << typeOrDataNode;                       // add the type or data to the args type node;
    }
    leave("ArgsType");
    return argsTypeNode;
}

/*
primaryType
    : INNER_ATOM_TYPE
    | dictExprType
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
    node_ptr_t res;
    if (context->INNER_ATOM_TYPE()) {                             // primary type with inner atom type
        std::string type = context->INNER_ATOM_TYPE()->getText(); // get the inner atom type of the primary type node;
        res = createNode<PrimaryType>(type, "");                  // create the primary type node;
    } else if (context->dictExprType()) {                         // primary type with dict expr type
        res = any_cast<node_ptr_t>(
            visitDictExprType(context->dictExprType())); // get the dict expr type of the primary type node;
    } else if (context->identRef()) {                    // primary type with ident ref
        res = any_cast<node_ptr_t>(visitIdentRef(context->identRef())); // get the ident ref of the primary type node;
    } else if (context->typeExpr()) {                                   // primary type with type exprq
        res = any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr())); // get the type expr of the primary type node;
    } else if (context->tupleType()) {                                  // primary type with tuple type
        res =
            any_cast<node_ptr_t>(visitTupleType(context->tupleType())); // get the tuple type of the primary type node;
    } else if (context->lambdaType()) {                                 // primary type with lambda type
        res = any_cast<node_ptr_t>(
            visitLambdaType(context->lambdaType()));     // get the lambda type of the primary type node;
    } else if (context->TYPEOF()) {                      // primary type with typeof
        std::string type = context->TYPEOF()->getText(); // get the typeof of the primary type node;
        res = createNode<PrimaryType>(type, "");         // create the primary type node;
        node_ptr_t waitNode =
            any_cast<node_ptr_t>(visitWaitExpr(context->waitExpr())); // get the wait expr of the primary type node;
        *res << waitNode;
    } else if (context->TYPEAS()) {                         // primary type with typeas
        std::string type = context->TYPEAS()->getText();    // get the typeas of the primary type node;
        std::string ident = context->identDef()->getText(); // get the wait expr of the primary type node;
        res = createNode<PrimaryType>(type, ident);         // create the primary type node;
    }
    leave("PrimaryType");
    return res;
}

/*
dictExprType
    : dictType (('&' | '^') dictType)*
    ;
*/
any Constructor::visitDictExprType(OpenCMLParser::DictExprTypeContext *context) {
    enter("DictExprType");
    node_ptr_t lhsNode =
        any_cast<node_ptr_t>(visitDictType(context->dictType(0))); // get the dict type of the dict expr type node;
    DictExprType::DictExprTypeOp op;
    for (size_t i = 1; i < context->children.size(); ++i) {          // dict expr type with dict type
        std::string strOp = context->children[i * 2 - 1]->getText(); // get the operator of the dict expr type node;
        if (strOp == "&") {                                          // dict expr type with ampersand
            op = DictExprType::DictExprTypeOp::AMPERSAND;
        } else if (strOp == "^") { // dict expr type with caret
            op = DictExprType::DictExprTypeOp::CARET;
        } else { // invalid operator
            op = DictExprType::DictExprTypeOp::INVALID;
        }
        node_ptr_t rhsNode =
            any_cast<node_ptr_t>(visitDictType(context->dictType(i))); // get the dict type of the dict expr type node;
        node_ptr_t dictExprTypeNode = createNode<DictExprType>(op);    // create the dict expr type node;
        *dictExprTypeNode << lhsNode << rhsNode;
        lhsNode = dictExprTypeNode;
    }
    leave("DictExprType");
    return lhsNode;
}

/*
typeList
    : typeExpr (',' typeExpr)*
    ;
*/
any Constructor::visitTypeList(OpenCMLParser::TypeListContext *context) {
    enter("TypeList");
    node_ptr_t res =
        any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr(0))); // get the type expr of the type list node;
    if (context->children.size() > 1) {
        node_ptr_t typeListNode = createNode<TypeList>(); // create the type list node;
        *typeListNode << res;                             // add the type expr to the type list node;
        res = typeListNode;
        for (size_t i = 1; i < context->children.size(); ++i) { // type list with type expr
            node_ptr_t typeExprNode =
                any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr(i))); // get the type expr of the type list node;
            *res << typeExprNode;                                          // add the type expr to the type list node;
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
    node_ptr_t res;
    if (context->modifiers()) {                                     // lambda type with modifiers
        std::string strModifiers = context->modifiers()->getText(); // get the modifiers of the lambda type node;
        LambdaType::LambdaTypeModifiers modifier;
        if (strModifiers == "ATOMIC") { // lambda type with mutable modifier
            modifier = LambdaType::LambdaTypeModifiers::ATOMIC;
        } else if (strModifiers == "HARED") { // lambda type with mutable modifier
            modifier = LambdaType::LambdaTypeModifiers::HARED;
        } else if (strModifiers == "SYNC") { // lambda type with mutable modifier
            modifier = LambdaType::LambdaTypeModifiers::SYNC;
        } else if (strModifiers == "MACRO") { // lambda type with mutable modifier
            modifier = LambdaType::LambdaTypeModifiers::MACRO;
        } else {
            modifier = LambdaType::LambdaTypeModifiers::INVALID; // invalid modifier
        }
        res = createNode<LambdaType>(modifier); // create the lambda type node;
    }
    if (context->angledParams()) { // lambda type with angled params
        *res << any_cast<node_ptr_t>(
            visitAngledParams(context->angledParams())); // get the angled params of the lambda type node;
    }
    if (context->parentParams()) { // lambda type with parent params
        *res << any_cast<node_ptr_t>(
            visitParentParams(context->parentParams())); // get the parent params of the lambda type node;
    }
    if (context->typeExpr()) {
        *res << any_cast<node_ptr_t>(visitTypeExpr(context->typeExpr())); // get the type expr of the lambda type node;
    }
    leave("LambdaType");
    return res;
}

/*
identDef : IDENTIFIER ;
*/
any Constructor::visitIdentDef(OpenCMLParser::IdentDefContext *context) {
    enter("IdentDef");
    std::string ident = context->IDENTIFIER()->getText(); // get the identifier of the ident def node;
    node_ptr_t res = createNode<IdentDef>(ident);         // create the ident def node;
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
    for (size_t i = 0; i < context->children.size() - 1; i++) { // ident ref with identifier
        belongsTo.push_back(context->children[i]->getText());
    }
    ident = context->children[context->children.size() - 1]->getText(); // get the identifier of the ident ref node;
    node_ptr_t res = createNode<IdentRef>(ident, belongsTo);            // create the ident ref node;
    leave("IdentRef");
    return res;
}
