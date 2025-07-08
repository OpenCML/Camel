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
#include "utils/type.h"

#define DEBUG_LEVEL 0

using namespace std;
using namespace AST;

template <typename LoadType, typename... Args> node_ptr_t createNodeBy(Args &&...args) {
    return std::dynamic_pointer_cast<Node>(std::make_shared<LoadType>(std::forward<Args>(args)...));
}

template <typename LoadType> std::shared_ptr<LoadType> unwrapNodeAs(std::shared_ptr<Node> &node) {
    return std::dynamic_pointer_cast<LoadType>(node->load());
}

inline node_ptr_t any2node(const std::any &a) { return std::any_cast<node_ptr_t>(a); }

/*
program : SEP? (decl SEP?)* EOF;
*/
any Constructor::visitProgram(OpenCMLParser::ProgramContext *context) {
    enter("Program");

    root_ = std::make_shared<Node>(module_);

    for (const auto &decl : context->decl()) {
        node_ptr_t node = any2node(visitDecl(decl));
        if (node) {
            *root_ << node;
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
    | dataDecl
    | funcDecl
    | typeDecl
    | useDecl
    ;
*/
any Constructor::visitDecl(OpenCMLParser::DeclContext *context) {
    enter("Decl");
    any res = visit(context->children[0]);
    leave("Decl");
    return res;
}

/*
stmt
    : dataDecl
    | funcDecl
    | typeDecl
    | dataExpr
    | useDecl
    | retStmt
    | blockStmt
    ;
*/
any Constructor::visitStmt(OpenCMLParser::StmtContext *context) {
    enter("Stmt");
    if (context->dataExpr()) {
        node_ptr_t expr = any2node(visitDataExpr(context->dataExpr()));
        node_ptr_t stmt = createNodeBy<ExprStmtLoad>();
        *stmt << expr;
        leave("Stmt");
        return stmt;
    }
    any res = visit(context->children[0]);
    leave("Stmt");
    return res;
}

/*
stmtList : stmt (SEP? stmt)* SEP? ;
*/
any Constructor::visitStmtList(OpenCMLParser::StmtListContext *context) {
    enter("StmtList");
    node_ptr_t block = createNodeBy<StmtBlockLoad>();
    if (context->stmt().size() > 0) {
        for (auto &stmt : context->stmt()) {
            *block << any2node(visitStmt(stmt));
        }
    }
    leave("StmtList");
    return block;
}

/*
moduleDecl : MODULE identDef ;
*/
any Constructor::visitModuleDecl(OpenCMLParser::ModuleDeclContext *context) {
    enter("ModuleDecl");
    Reference ref(context->identDef()->getText());
    module_->setRef(ref);
    leave("ModuleDecl");
    return nullptr;
}

/*
importDecl : IMPORT (STRING | (identDef | bracedIdents) FROM STRING) ;
*/
any Constructor::visitImportDecl(OpenCMLParser::ImportDeclContext *context) {
    enter("ImportDecl");
    if (context->STRING()) {
        import_->setPath(context->STRING()->getText());
    }
    if (context->identDef()) {
        import_->setAs(Reference(context->identDef()->getText()));
    } else if (context->bracedIdents()) {
        std::vector<Reference> refs;
        for (auto &ident : context->bracedIdents()->identList()->identDef()) {
            refs.emplace_back(ident->getText());
        }
        import_->setRefs(refs);
    }
    leave("ImportDecl");
    return nullptr;
}

/*
exportDecl : EXPORT (dataDecl | typeDecl | bracedIdents) ;
*/
any Constructor::visitExportDecl(OpenCMLParser::ExportDeclContext *context) {
    enter("ExportDecl");
    node_ptr_t res = nullptr;
    if (context->dataDecl()) {
        res = any2node(visitDataDecl(context->dataDecl()));
        // TODO: 这里需要处理导出数据声明的情况
    } else if (context->typeDecl()) {
        res = any2node(visitTypeDecl(context->typeDecl()));
    } else if (context->bracedIdents()) {
        for (auto &ident : context->bracedIdents()->identList()->identDef()) {
            export_->addRef(Reference(ident->getText()));
        }
    }
    leave("ExportDecl");
    return res;
}

/*
blockStmt  : WAIT? stmtBlock ;
*/
any Constructor::visitBlockStmt(OpenCMLParser::BlockStmtContext *context) {
    enter("BlockStmt");
    node_ptr_t res = any2node(visitStmtBlock(context->stmtBlock()));
    if (context->WAIT()) {
        unwrapNodeAs<StmtBlockLoad>(res)->setWait(true);
    }
    leave("BlockStmt");
    return res;
}

/*
stmtBlock  : SYNC? '{' stmtList? '}' ;
*/
any Constructor::visitStmtBlock(OpenCMLParser::StmtBlockContext *context) {
    enter("StmtBlock");
    node_ptr_t blockNode = nullptr;
    if (context->stmtList()) {
        blockNode = any2node(visitStmtList(context->stmtList()));
        if (context->SYNC()) {
            // If SYNC is present, we treat this as a synchronous block statement.
            unwrapNodeAs<StmtBlockLoad>(blockNode)->setSync(true);
        }
    } else {
        // If no statements are present, we create an empty block.
        blockNode = createNodeBy<StmtBlockLoad>();
    }
    leave("StmtBlock");
    return blockNode;
}

/*
blockExpr  : stmtBlock | dataExpr ;
*/
any Constructor::visitBlockExpr(OpenCMLParser::BlockExprContext *context) {
    enter("BlockExpr");
    node_ptr_t res = nullptr;
    if (context->stmtBlock()) {
        res = any2node(visitStmtBlock(context->stmtBlock()));
    } else if (context->dataExpr()) {
        node_ptr_t expr = any2node(visitDataExpr(context->dataExpr()));
        node_ptr_t stmt = createNodeBy<ExprStmtLoad>();
        res = createNodeBy<StmtBlockLoad>();
        *stmt << expr;
        *res << stmt;
    }
    leave("BlockExpr");
    return res;
}

/*
funcData   : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr ;
*/
any Constructor::visitFuncData(OpenCMLParser::FuncDataContext *context) {
    enter("FuncData");
    node_ptr_t funcTypeNode = createNodeBy<FuncTypeLoad>();
    auto funcType = unwrapNodeAs<FuncTypeLoad>(funcTypeNode);

    auto modifier = context->modifiers();
    if (modifier) {
        if (modifier->ATOMIC().size() > 0) {
            funcType->setAtomic(true);
        }
        if (modifier->SHARED().size() > 0) {
            funcType->setShared(true);
        }
        if (modifier->SYNC().size() > 0) {
            funcType->setSync(true);
        }
        if (modifier->MACRO().size() > 0) {
            funcType->setMacro(true);
        }
    }

    if (context->angledParams()) {
        node_ptr_t withParams = any2node(visitAngledParams(context->angledParams()));
        *funcTypeNode << withParams;
    }

    if (context->parentParams()) {
        node_ptr_t normParams = any2node(visitParentParams(context->parentParams()));
        *funcTypeNode << normParams;
    }

    node_ptr_t typeOptNode = createNodeBy<OptionalLoad>("Type");
    if (context->typeExpr()) {
        node_ptr_t typeNode = any2node(visitTypeExpr(context->typeExpr()));
        *typeOptNode << typeNode;
    }
    *funcTypeNode << typeOptNode;

    node_ptr_t blockNode = any2node(visitBlockExpr(context->blockExpr()));

    node_ptr_t funcNode = createNodeBy<FuncDataLoad>();
    *funcNode << funcTypeNode;
    *funcNode << blockNode;

    leave("FuncData");
    return funcNode;
}

/*
funcDecl   :
        (WITH angledParams)?
        EXPORT? implMark? modifiers?
        FUNC identDef parentParams (':' typeExpr)? stmtBlock ;
*/
any Constructor::visitFuncDecl(OpenCMLParser::FuncDeclContext *context) {
    enter("FuncDecl");

    node_ptr_t funcTypeNode = createNodeBy<FuncTypeLoad>();
    auto funcType = unwrapNodeAs<FuncTypeLoad>(funcTypeNode);

    auto implMark = context->implMark();
    if (implMark) {
        if (implMark->getText() == "inner") {
            funcType->setImplMark(ImplMark::Inner);
        } else {
            funcType->setImplMark(ImplMark::Outer);
        }
    }

    auto modifier = context->modifiers();
    if (modifier) {
        if (modifier->ATOMIC().size() > 0) {
            funcType->setAtomic(true);
        }
        if (modifier->SHARED().size() > 0) {
            funcType->setShared(true);
        }
        if (modifier->SYNC().size() > 0) {
            funcType->setSync(true);
        }
        if (modifier->MACRO().size() > 0) {
            funcType->setMacro(true);
        }
    }

    if (context->angledParams()) {
        node_ptr_t withParams = any2node(visitAngledParams(context->angledParams()));
        *funcTypeNode << withParams;
    }

    if (context->parentParams()) {
        node_ptr_t normParams = any2node(visitParentParams(context->parentParams()));
        *funcTypeNode << normParams;
    }

    node_ptr_t typeOptNode = createNodeBy<OptionalLoad>("Type");
    if (context->typeExpr()) {
        node_ptr_t typeNode = any2node(visitTypeExpr(context->typeExpr()));
        *typeOptNode << typeNode;
    }
    *funcTypeNode << typeOptNode;

    Reference ref(context->identDef()->getText());
    if (context->EXPORT()) {
        export_->addRef(ref);
    }

    node_ptr_t blockNode = any2node(visitStmtBlock(context->stmtBlock()));

    node_ptr_t funcNode = createNodeBy<FuncDataLoad>(ref);
    *funcNode << funcTypeNode;
    *funcNode << blockNode;

    leave("FuncDecl");
    return funcNode;
}

/*
parentIdents  : '(' identList? ','? ')' ;
*/
any Constructor::visitParentIdents(OpenCMLParser::ParentIdentsContext *context) {
    enter("ParentIdents");
    vector<Reference> refs;
    if (context->identList()) {
        refs = any_cast<vector<Reference>>(visitIdentList(context->identList()));
    }
    leave("ParentIdents");
    return refs;
}

/*
bracedIdents  : '{' identList? ','? '}' ;
*/
any Constructor::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {
    enter("BracedIdents");
    vector<Reference> refs;
    if (context->identList()) {
        refs = any_cast<vector<Reference>>(visitIdentList(context->identList()));
    }
    leave("BracedIdents");
    return refs;
}

/*
bracketIdents : '[' identList? ','? ']' ;
*/
any Constructor::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {
    enter("BracketIdents");
    vector<Reference> refs;
    if (context->identList()) {
        refs = any_cast<vector<Reference>>(visitIdentList(context->identList()));
    }
    leave("BracketIdents");
    return refs;
}

/*
carrier       : identList | parentIdents | bracedIdents | bracketIdents ;
*/
any Constructor::visitCarrier(OpenCMLParser::CarrierContext *context) {
    enter("Carrier");
    UnpackType type = UnpackType::Tuple;
    vector<Reference> refs;
    if (context->identList()) {
        refs = any_cast<vector<Reference>>(visitIdentList(context->identList()));
    } else if (context->parentIdents()) {
        refs = any_cast<vector<Reference>>(visitParentIdents(context->parentIdents()));
    } else if (context->bracedIdents()) {
        type = UnpackType::Dict;
        refs = any_cast<vector<Reference>>(visitBracedIdents(context->bracedIdents()));
    } else if (context->bracketIdents()) {
        type = UnpackType::List;
        refs = any_cast<vector<Reference>>(visitBracketIdents(context->bracketIdents()));
    }
    leave("Carrier");
    return make_tuple(type, refs);
}

/*
dataDecl   : (LET | VAR) carrier (':' typeList)? '=' dataList ;
*/
any Constructor::visitDataDecl(OpenCMLParser::DataDeclContext *context) {
    enter("DataDecl");
    bool isVar = false;
    if (context->VAR()) {
        isVar = true;
    }
    auto [type, refs] = any_cast<std::tuple<UnpackType, vector<Reference>>>(visitCarrier(context->carrier()));
    node_ptr_t dataDeclNode = createNodeBy<DataDeclLoad>(isVar, type, refs);

    if (context->typeList()) {
        *dataDeclNode << any2node(visitTypeList(context->typeList()));
    } else {
        node_ptr_t typeListNode = createNodeBy<RepeatedLoad>("Type");
        *dataDeclNode << typeListNode;
    }

    *dataDeclNode << any2node(visitDataList(context->dataList()));
    leave("DataDecl");
    return dataDeclNode;
}

/*
typeDecl   : implMark? TYPE identDef '=' (typeExpr | STRING) ;
*/
any Constructor::visitTypeDecl(OpenCMLParser::TypeDeclContext *context) {
    enter("TypeDecl");
    string uri;
    ImplMark implMark = ImplMark::Graph;
    Reference ref(context->identDef()->getText());
    if (context->implMark()) {
        if (context->implMark()->INNER()) {
            implMark = ImplMark::Inner;
        } else {
            implMark = ImplMark::Outer;
        }
    }
    if (context->STRING()) {
        uri = context->STRING()->getText();
        // Remove quotes from the string
        uri = uri.substr(1, uri.size() - 2);
    }

    node_ptr_t typeDeclNode = createNodeBy<TypeDeclLoad>(ref, implMark, uri);

    node_ptr_t typeOptNode = createNodeBy<OptionalLoad>("Type");
    if (context->typeExpr()) {
        *typeOptNode << any2node(visitTypeExpr(context->typeExpr()));
    }

    *typeDeclNode << typeOptNode;
    leave("TypeDecl");
    return typeDeclNode;
}

/*
useDecl    : USE (identDef '=')? identRef ;
*/
any Constructor::visitUseDecl(OpenCMLParser::UseDeclContext *context) {
    enter("UseDecl");
    Reference ref;
    if (context->identDef()) {
        ref.set(context->identDef()->getText());
    }
    Reference alias(context->identRef()->getText());
    node_ptr_t nameDeclNode = createNodeBy<NameDeclLoad>(ref, alias);
    leave("UseDecl");
    return nameDeclNode;
}

/*
retStmt    : (RETURN | RAISE | THROW) dataList ;
*/
any Constructor::visitRetStmt(OpenCMLParser::RetStmtContext *context) {
    enter("RetStmt");
    ExitType exitType;
    if (context->RETURN()) {
        exitType = ExitType::Return;
    } else if (context->RAISE()) {
        exitType = ExitType::Raise;
    } else if (context->THROW()) {
        exitType = ExitType::Throw;
    } else {
        // If no return type is specified, default to Return
        exitType = ExitType::Return;
    }
    node_ptr_t exitNode = createNodeBy<ExitStmtLoad>(exitType);
    if (context->dataList()) {
        *exitNode << any2node(visitDataList(context->dataList()));
    }
    leave("RetStmt");
    return exitNode;
}

/*
implMark    : INNER | OUTER ;
*/
any Constructor::visitImplMark(OpenCMLParser::ImplMarkContext *context) {
    enter("ImplMark");
    // NOTHING TO DO
    leave("ImplMark");
    return nullptr;
}

/*
modifiers   : (ATOMIC | SHARED | SYNC | MACRO)+ ;
*/
any Constructor::visitModifiers(OpenCMLParser::ModifiersContext *context) {
    enter("Modifiers");
    // NOTHING TO DO
    leave("Modifiers");
    return nullptr;
}

/*
indexValue   : '...'? dataExpr ;
*/
any Constructor::visitIndexValue(OpenCMLParser::IndexValueContext *context) {
    enter("IndexValue");
    node_ptr_t res = any2node(visitDataExpr(context->dataExpr()));
    leave("IndexValue");
    return res;
}

/*
keyTypePair  : identDef ':' typeExpr ;
*/
any Constructor::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) {
    enter("KeyTypePair");
    Reference ref(context->identDef()->getText());
    node_ptr_t namedTypeNode = createNodeBy<NamedTypeLoad>(ref);
    if (context->typeExpr()) {
        *namedTypeNode << any2node(visitTypeExpr(context->typeExpr()));
    }
    leave("KeyTypePair");
    return namedTypeNode;
}

/*
keyValuePair : identDef ':' dataExpr | '...' dataExpr ;
*/
any Constructor::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {
    enter("KeyValuePair");
    Reference ref(context->identDef()->getText());
    node_ptr_t namedDataNode = createNodeBy<NamedDataLoad>(ref);
    if (context->dataExpr()) {
        *namedDataNode << any2node(visitDataExpr(context->dataExpr()));
    }
    leave("KeyValuePair");
    return namedDataNode;
}

/*
keyParamPair : VAR? identDef ':' typeExpr ('=' dataExpr)? ;
*/
any Constructor::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {
    enter("KeyParamPair");
    bool isVar = false;
    if (context->VAR()) {
        isVar = true;
    }
    Reference ref(context->identDef()->getText());
    node_ptr_t namedPairNode = createNodeBy<NamedPairLoad>(ref, isVar);
    *namedPairNode << any2node(visitTypeExpr(context->typeExpr()));
    node_ptr_t dataExprOptNode = createNodeBy<OptionalLoad>("DataExpr");
    if (context->dataExpr()) {
        *dataExprOptNode << any2node(visitDataExpr(context->dataExpr()));
    }
    *namedPairNode << dataExprOptNode;
    leave("KeyParamPair");
    return namedPairNode;
}

/*
dataList     : dataExpr (',' dataExpr)* ;
*/
any Constructor::visitDataList(OpenCMLParser::DataListContext *context) {
    enter("DataList");
    node_ptr_t rep = createNodeBy<RepeatedLoad>("Data");
    for (const auto &dataExpr : context->dataExpr()) {
        *rep << any2node(visitDataExpr(dataExpr));
    }
    leave("DataList");
    return rep;
}

/*
identList    : identDef (',' identDef)* ;
*/
any Constructor::visitIdentList(OpenCMLParser::IdentListContext *context) {
    enter("IdentList");
    std::vector<Reference> refs;
    for (const auto &identDef : context->identDef()) {
        refs.emplace_back(identDef->getText());
    }
    leave("IdentList");
    return refs;
}

/*
indexValues  : indexValue (',' indexValue)* ;
*/
any Constructor::visitIndexValues(OpenCMLParser::IndexValuesContext *context) {
    enter("IndexValues");
    node_ptr_t rep = createNodeBy<RepeatedLoad>("Data");
    for (const auto &indexValue : context->indexValue()) {
        *rep << any2node(visitIndexValue(indexValue));
    }
    leave("IndexValues");
    return rep;
}

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
*/
any Constructor::visitPairedValues(OpenCMLParser::PairedValuesContext *context) {
    enter("PairedValues");
    node_ptr_t rep = createNodeBy<RepeatedLoad>("NamedData");
    for (const auto &keyValuePair : context->keyValuePair()) {
        *rep << any2node(visitKeyValuePair(keyValuePair));
    }
    leave("PairedValues");
    return rep;
}

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
any Constructor::visitPairedParams(OpenCMLParser::PairedParamsContext *context) {
    enter("PairedParams");
    node_ptr_t rep = createNodeBy<RepeatedLoad>("NamedPair");
    for (const auto &keyParamPair : context->keyParamPair()) {
        *rep << any2node(visitKeyParamPair(keyParamPair));
    }
    leave("PairedParams");
    return rep;
}

/*
argumentList : indexValues (',' pairedValues)? | pairedValues ;
*/
any Constructor::visitArgumentList(OpenCMLParser::ArgumentListContext *context) {
    enter("ArgumentList");
    node_ptr_t dataList = any2node(visitIndexValues(context->indexValues()));
    node_ptr_t namedDataList = createNodeBy<RepeatedLoad>("NamedData");
    if (context->pairedValues()) {
        namedDataList = any2node(visitPairedValues(context->pairedValues()));
    }
    leave("ArgumentList");
    return make_pair(dataList, namedDataList);
}

/*
memberAccess : '[' dataExpr (':' dataExpr (':' dataExpr)?)? ']' ;
*/
any Constructor::visitMemberAccess(OpenCMLParser::MemberAccessContext *context) {
    enter("MemberAccess");
    node_ptr_t dataList = createNodeBy<RepeatedLoad>("Data");
    if (context->dataExpr(0)) {
        *dataList << any2node(visitDataExpr(context->dataExpr(0)));
    }
    if (context->dataExpr(1)) {
        *dataList << any2node(visitDataExpr(context->dataExpr(1)));
    }
    if (context->dataExpr(2)) {
        *dataList << any2node(visitDataExpr(context->dataExpr(2)));
    }
    leave("MemberAccess");
    return dataList;
}

/*
parentParams : '(' pairedParams? ','? ')' ;
*/
any Constructor::visitParentParams(OpenCMLParser::ParentParamsContext *context) {
    enter("ParentParams");
    node_ptr_t rep = createNodeBy<RepeatedLoad>("NamedPair");
    const auto &pairedParams = context->pairedParams();
    if (pairedParams) {
        rep = any2node(visitPairedParams(pairedParams));
    }
    leave("ParentParams");
    return rep;
}

/*
parentArgues : '(' argumentList? ','? ')' ;
*/
any Constructor::visitParentArgues(OpenCMLParser::ParentArguesContext *context) {
    enter("ParentArgues");
    node_ptr_t dataList = createNodeBy<RepeatedLoad>("Data");
    node_ptr_t namedDataList = createNodeBy<RepeatedLoad>("NamedData");
    if (context->argumentList()) {
        auto res = any_cast<std::pair<node_ptr_t, node_ptr_t>>(visitArgumentList(context->argumentList()));
        dataList = res.first;
        namedDataList = res.second;
    }
    leave("ParentArgues");
    return make_pair(dataList, namedDataList);
}

/*
angledParams : '<' pairedParams? ','? '>' ;
*/
any Constructor::visitAngledParams(OpenCMLParser::AngledParamsContext *context) {
    enter("AngledParams");
    node_ptr_t res = createNodeBy<RepeatedLoad>("NamedPair");
    const auto &pairedParams = context->pairedParams();
    if (pairedParams) {
        res = any2node(visitPairedParams(pairedParams));
    }
    leave("AngledParams");
    return res;
}

/*
angledValues : '<' argumentList? ','? '>' ;
*/
any Constructor::visitAngledValues(OpenCMLParser::AngledValuesContext *context) {
    enter("AngledValues");
    node_ptr_t dataList = createNodeBy<RepeatedLoad>("Data");
    node_ptr_t namedDataList = createNodeBy<RepeatedLoad>("NamedData");
    if (context->argumentList()) {
        auto res = any_cast<std::pair<node_ptr_t, node_ptr_t>>(visitArgumentList(context->argumentList()));
        dataList = res.first;
        namedDataList = res.second;
    }
    leave("AngledValues");
    return make_pair(dataList, namedDataList);
}

/*
pattern
    : identRef
    | literal
    | '(' (dataList | identList)? ','? ')'
    | '{' (pairedValues | identList)? ','? '}'
    | '_' // wildcard
    ;
*/
any Constructor::visitPattern(OpenCMLParser::PatternContext *context) {
    enter("Pattern");
    throw std::runtime_error("visitPattern: not implemented yet");
    leave("Pattern");
    return nullptr;
}

/*
matchCase
    : CASE pattern ('|' pattern)* '=>' blockExpr
    ;
*/
any Constructor::visitMatchCase(OpenCMLParser::MatchCaseContext *context) {
    // TODO: match case
    enter("MatchCase");
    throw std::runtime_error("visitMatchCase: not implemented yet");
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
    throw std::runtime_error("visitCatchClause: not implemented yet");
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
    throw std::runtime_error("visitCtrlExpr: not implemented yet");
    leave("CtrlExpr");
    return nullptr;
}

/*
dataExpr
    : waitExpr
    | ctrlExpr
    ;
*/
any Constructor::visitDataExpr(OpenCMLParser::DataExprContext *context) {
    enter("DataExpr");
    node_ptr_t res = nullptr;
    if (context->waitExpr()) {
        res = any2node(visitWaitExpr(context->waitExpr()));
    } else if (context->ctrlExpr()) {
        res = any2node(visitCtrlExpr(context->ctrlExpr()));
    }
    leave("DataExpr");
    return res;
}

/*
waitExpr : WAIT? assignExpr ;
*/
any Constructor::visitWaitExpr(OpenCMLParser::WaitExprContext *context) {
    enter("WaitExpr");
    node_ptr_t dataNode = any2node(visitAssignExpr(context->assignExpr()));
    if (context->WAIT()) {
        unwrapNodeAs<DataLoad>(dataNode)->setWaited(true);
    }
    leave("WaitExpr");
    return dataNode;
}

/*
assignExpr
    : logicalOrExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=') logicalOrExpr)?
    ;
*/
any Constructor::visitAssignExpr(OpenCMLParser::AssignExprContext *context) {
    enter("AssignExpr");
    node_ptr_t lhsNode = any2node(visitLogicalOrExpr(context->logicalOrExpr(0)));
    for (size_t i = 1; i < context->logicalOrExpr().size(); i++) {
        DataOp op;
        string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "=") {
            op = DataOp::Assign;
        } else if (strOp == "+=") {
            op = DataOp::AssignAdd;
        } else if (strOp == "-=") {
            op = DataOp::AssignSub;
        } else if (strOp == "*=") {
            op = DataOp::AssignMul;
        } else if (strOp == "/=") {
            op = DataOp::AssignDiv;
        } else if (strOp == "%=") {
            op = DataOp::AssignMod;
        } else if (strOp == "^=") {
            op = DataOp::AssignExp;
        } else if (strOp == "@=") {
            op = DataOp::AssignMat;
        } else if (strOp == "&=") {
            op = DataOp::AssignAnd;
        } else if (strOp == "|=") {
            op = DataOp::AssignOr;
        } else {
            throw std::runtime_error("Invalid assignment operator: " + strOp);
        }
        node_ptr_t rhsNode = any2node(visitLogicalOrExpr(context->logicalOrExpr(i)));
        node_ptr_t dataExprNode = createNodeBy<DataExprLoad>(op);
        *dataExprNode << lhsNode << rhsNode;
        lhsNode = dataExprNode;
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
    node_ptr_t lhsNode = any2node(visitLogicalAndExpr(context->logicalAndExpr(0)));
    for (size_t i = 1; i < context->logicalAndExpr().size(); ++i) {
        node_ptr_t rhsNode = any2node(visitLogicalAndExpr(context->logicalAndExpr(i)));
        node_ptr_t dataExprNode = createNodeBy<DataExprLoad>(DataOp::Or);
        *dataExprNode << lhsNode << rhsNode;
        lhsNode = dataExprNode;
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
    node_ptr_t lhsNode = any2node(visitEqualityExpr(context->equalityExpr(0)));
    for (size_t i = 1; i < context->equalityExpr().size(); ++i) {
        node_ptr_t rhsNode = any2node(visitEqualityExpr(context->equalityExpr(i)));
        node_ptr_t dataExprNode = createNodeBy<DataExprLoad>(DataOp::And);
        *dataExprNode << lhsNode << rhsNode;
        lhsNode = dataExprNode;
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
    node_ptr_t lhsNode =
        any2node(visitRelationalExpr(context->relationalExpr(0))); // get the left hand side of the equality expr
    for (size_t i = 1; i < context->relationalExpr().size(); ++i) {
        DataOp op;
        string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "===") {
            op = DataOp::StrictEq;
        } else if (strOp == "!==") {
            op = DataOp::StrictNeq;
        } else if (strOp == "==") {
            op = DataOp::Eq;
        } else if (strOp == "!=") {
            op = DataOp::Neq;
        } else {
            throw std::runtime_error("Invalid equality operator: " + strOp);
        }
        node_ptr_t rhsNode = any2node(visitRelationalExpr(context->relationalExpr(i)));
        node_ptr_t dataExprNode = createNodeBy<DataExprLoad>(op);
        *dataExprNode << lhsNode << rhsNode;
        lhsNode = dataExprNode;
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
    node_ptr_t lhsNode = any2node(visitAdditiveExpr(context->additiveExpr(0)));
    for (size_t i = 1; i < context->additiveExpr().size(); ++i) {
        DataOp op;
        string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "<") {
            op = DataOp::Less;
        } else if (strOp == ">") {
            op = DataOp::Greater;
        } else if (strOp == "<=") {
            op = DataOp::LessEq;
        } else if (strOp == ">=") {
            op = DataOp::GreaterEq;
        } else {
            throw std::runtime_error("Invalid relational operator: " + strOp);
        }
        node_ptr_t rhsNode = any2node(visitAdditiveExpr(context->additiveExpr(i)));
        node_ptr_t dataExprNode = createNodeBy<DataExprLoad>(op);
        *dataExprNode << lhsNode << rhsNode;
        lhsNode = dataExprNode;
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
    node_ptr_t lhsNode = any2node(visitMultiplicativeExpr(context->multiplicativeExpr(0)));
    for (size_t i = 1; i < context->multiplicativeExpr().size(); i++) {
        DataOp op;
        string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "+") {
            op = DataOp::Add;
        } else if (strOp == "-") {
            op = DataOp::Sub;
        } else {
            throw std::runtime_error("Invalid additive operator: " + strOp);
        }
        node_ptr_t rhsNode = any2node(visitMultiplicativeExpr(context->multiplicativeExpr(i)));
        node_ptr_t dataExprNode = createNodeBy<DataExprLoad>(op);
        *dataExprNode << lhsNode << rhsNode;
        lhsNode = dataExprNode;
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
    DataOp op;
    node_ptr_t lhsNode = any2node(visitNullableExpr(context->nullableExpr(0)));
    for (size_t i = 1; i < context->nullableExpr().size(); i++) {
        string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "*") {
            op = DataOp::Mul;
        } else if (strOp == "/") {
            op = DataOp::Div;
        } else if (strOp == "^") {
            op = DataOp::Exp;
        } else if (strOp == "@") {
            op = DataOp::Mat;
        } else if (strOp == "%") {
            op = DataOp::Mod;
        } else {
            throw std::runtime_error("Invalid multiplicative operator: " + strOp);
        }
        node_ptr_t rhsNode = any2node(visitNullableExpr(context->nullableExpr(i)));
        node_ptr_t dataExprNode = createNodeBy<DataExprLoad>(op);
        *dataExprNode << lhsNode << rhsNode;
        lhsNode = dataExprNode;
    }
    leave("MultiplicativeExpr");
    return lhsNode;
}

/*
nullableExpr
    : unaryExpr (('??' | '!!') dataExpr)?
    ;
*/
any Constructor::visitNullableExpr(OpenCMLParser::NullableExprContext *context) {
    enter("NullableExpr");
    node_ptr_t res = any2node(visitUnaryExpr(context->unaryExpr()));

    if (context->children.size() > 1) {
        string strOp = context->children[1]->getText();
        DataOp op;
        if (strOp == "??") {
            op = DataOp::NullThen;
        } else if (strOp == "!!") {
            op = DataOp::ErrorThen;
        } else {
            throw std::runtime_error("Invalid nullable operator: " + strOp);
        }

        if (context->dataExpr()) {
            node_ptr_t dataExprNode = createNodeBy<DataExprLoad>(op);
            *dataExprNode << res << any2node(visitDataExpr(context->dataExpr()));
            res = dataExprNode;
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

    switch (context->getAltNumber()) {
    case 1: {
        DataOp op;
        if (context->AS()) {
            op = DataOp::As;
        } else if (context->IS()) {
            op = DataOp::Is;
        } else {
            throw std::runtime_error("Invalid unary operator: " + context->children[0]->getText());
        }
        node_ptr_t dataNode = any2node(visitLinkExpr(context->linkExpr()));
        node_ptr_t typeNode = any2node(visitTypeExpr(context->typeExpr()));
        res = createNodeBy<DataExprLoad>(op);
        *res << dataNode << typeNode;
        break;
    }
    case 2: {
        DataOp op;
        string strOp = context->children[0]->getText();
        if (strOp == "!") {
            op = DataOp::Not;
        } else if (strOp == "-") {
            op = DataOp::Neg;
        } else if (strOp == "~") {
            op = DataOp::Inv;
        } else {
            throw std::runtime_error("Invalid unary operator: " + strOp);
        }
        node_ptr_t dataNode = any2node(visitLinkExpr(context->linkExpr()));
        res = createNodeBy<DataExprLoad>(op);
        *res << dataNode;
        break;
    }
    default:
        throw std::runtime_error("Invalid alternative number in UnaryExpr: " + std::to_string(context->getAltNumber()));
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
    node_ptr_t lhsNode = any2node(visitBindExpr(context->bindExpr(0)));
    for (size_t i = 1; i < context->bindExpr().size(); i++) {
        string strOp = context->children[i * 2 - 1]->getText();
        node_ptr_t dataNode = createNodeBy<DataExprLoad>(DataOp::Call);
        node_ptr_t rhsNode = any2node(visitBindExpr(context->bindExpr(i)));
        if (strOp == "?->") {
            node_ptr_t notNullNode = createNodeBy<DataExprLoad>(DataOp::NotNullThen);
            *notNullNode << lhsNode;
            lhsNode = notNullNode;
        }
        *dataNode << lhsNode << rhsNode;
        lhsNode = dataNode;
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
    node_ptr_t lhsNode = any2node(visitWithExpr(context->withExpr(0)));
    for (size_t i = 1; i < context->withExpr().size(); i++) {
        string strOp = context->children[i * 2 - 1]->getText();
        node_ptr_t dataNode = createNodeBy<DataExprLoad>(DataOp::Bind);
        node_ptr_t rhsNode = any2node(visitWithExpr(context->withExpr(i)));
        if (strOp == "?..") {
            node_ptr_t notNullNode = createNodeBy<DataExprLoad>(DataOp::NotNullThen);
            *notNullNode << lhsNode;
            lhsNode = notNullNode;
        }
        *dataNode << lhsNode << rhsNode;
        lhsNode = dataNode;
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
    node_ptr_t lhsNode = any2node(visitAnnoExpr(context->annoExpr(0)));
    for (size_t i = 1; i < context->annoExpr().size(); i++) {
        string strOp = context->children[i * 2 - 1]->getText();
        node_ptr_t dataNode = createNodeBy<DataExprLoad>(DataOp::With);
        node_ptr_t rhsNode = any2node(visitAnnoExpr(context->annoExpr(i)));
        if (strOp == "?.") {
            node_ptr_t notNullNode = createNodeBy<DataExprLoad>(DataOp::NotNullThen);
            *notNullNode << lhsNode;
            lhsNode = notNullNode;
        }
        *dataNode << lhsNode << rhsNode;
        lhsNode = dataNode;
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
    node_ptr_t lhsNode = any2node(visitPrimaryData(context->primaryData()));
    for (size_t i = 1; i < context->children.size(); i++) {
        auto child = context->children[i];
        if (antlr4::RuleContext::is(child)) {
            node_ptr_t exprNode = nullptr;
            node_ptr_t rhsNode = nullptr;
            if (tt::is_instance_of<OpenCMLParser::MemberAccessContext>(child)) {
                exprNode = createNodeBy<DataExprLoad>(DataOp::Index);
                rhsNode = any2node(visitMemberAccess(tt::as<OpenCMLParser::MemberAccessContext>(child)));
            } else if (tt::is_instance_of<OpenCMLParser::ParentArguesContext>(child)) {
                exprNode = createNodeBy<DataExprLoad>(DataOp::Call);
                rhsNode = any2node(visitParentArgues(tt::as<OpenCMLParser::ParentArguesContext>(child)));
            } else if (tt::is_instance_of<OpenCMLParser::AngledValuesContext>(child)) {
                exprNode = createNodeBy<DataExprLoad>(DataOp::With);
                rhsNode = any2node(visitAngledValues(tt::as<OpenCMLParser::AngledValuesContext>(child)));
            }
            *exprNode << lhsNode << rhsNode;
            lhsNode = exprNode;
        } else {
            unwrapNodeAs<DataLoad>(lhsNode)->setNotNull(true);
        }
    }
    leave("AnnoExpr");
    return lhsNode;
}

/*
dictData
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct of dict is immutable
    ;
*/
any Constructor::visitDictData(OpenCMLParser::DictDataContext *context) {
    enter("DictData");
    node_ptr_t dataNode = nullptr;
    if (context->pairedValues()) {
        dataNode = any2node(visitPairedValues(context->pairedValues()));
    } else {
        dataNode = createNodeBy<RepeatedLoad>("NamedData");
    }
    leave("DictData");
    return dataNode;
}

/*
listData
    : '[' ((indexValues ','?) | dataExpr FOR identRef IN dataExpr (IF dataExpr)?)? ']'
    ;
*/
any Constructor::visitListData(OpenCMLParser::ListDataContext *context) {
    enter("ListData");
    node_ptr_t dataNode = nullptr;
    if (context->indexValues()) {
        dataNode = any2node(visitIndexValues(context->indexValues()));
    } else {
        dataNode = createNodeBy<RepeatedLoad>("Data");
        // TODO: Handle list comprehension
    }
    leave("ListData");
    return dataNode;
}

/*
tupleData
    : '(' dataList? ','? ')'
    ;
*/
any Constructor::visitTupleData(OpenCMLParser::TupleDataContext *context) {
    enter("TupleData");
    node_ptr_t dataNode = nullptr;
    if (context->dataList()) {
        dataNode = any2node(visitDataList(context->dataList()));
    } else {
        dataNode = createNodeBy<RepeatedLoad>("Data");
    }
    leave("TupleData");
    return dataNode;
}

/*
primaryData
    : identRef
    | literal
    | listData
    | dictData
    | '(' dataExpr ')'
    | tupleData
    | funcData
    ;
*/
any Constructor::visitPrimaryData(OpenCMLParser::PrimaryDataContext *context) {
    enter("PrimaryData");
    any res;
    if (context->dataExpr()) {
        res = visitDataExpr(context->dataExpr());
    } else {
        res = visit(context->children[0]);
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
    node_ptr_t dataNode = nullptr;
    switch (context->getAltNumber()) {
    case 1: { // INTEGER
        dataNode = createNodeBy<LiteralLoad>(Literal(LiteralType::Integer, context->INTEGER()->getText()));
        break;
    }
    case 2: { // REAL
        dataNode = createNodeBy<LiteralLoad>(Literal(LiteralType::Real, context->REAL()->getText()));
        break;
    }
    case 3: { // STRING
        dataNode = createNodeBy<LiteralLoad>(Literal(LiteralType::String, context->STRING()->getText()));
        break;
    }
    case 4: { // MULTI_STR
        dataNode = createNodeBy<LiteralLoad>(Literal(LiteralType::String, context->MULTI_STR()->getText()));
        break;
    }
    case 5: { // FSTRING
        dataNode = createNodeBy<LiteralLoad>(Literal(LiteralType::String, context->FSTRING()->getText()));
        break;
    }
    case 6: { // TRUE
        dataNode = createNodeBy<LiteralLoad>(Literal(LiteralType::Boolean, "true"));
        break;
    }
    case 7: { // FALSE
        dataNode = createNodeBy<LiteralLoad>(Literal(LiteralType::Boolean, "false"));
        break;
    }
    case 8: { // NULL
        dataNode = createNodeBy<LiteralLoad>(Literal(LiteralType::Null, "null"));
        break;
    }

    default:
        break;
    }
    leave("Literal");
    return dataNode;
}

/*
typeExpr
    : unionType ('?' unionType?)?
    ;
*/
any Constructor::visitTypeExpr(OpenCMLParser::TypeExprContext *context) {
    enter("TypeExpr");
    node_ptr_t lhsNode = any2node(visitUnionType(context->unionType(0)));
    if (context->unionType().size() > 1) {
        node_ptr_t typeExprNode = createNodeBy<TypeExprLoad>(TypeOp::ErrorThen);
        *typeExprNode << any2node(visitUnionType(context->unionType(1)));
        lhsNode = typeExprNode;
    }
    leave("TypeExpr");
    return lhsNode;
}

/*
unionType
    : interType ('|' interType)*
    ;
*/
any Constructor::visitUnionType(OpenCMLParser::UnionTypeContext *context) {
    enter("UnionType");
    node_ptr_t lhsNode = any2node(visitInterType(context->interType(0)));
    for (size_t i = 1; i < context->interType().size(); ++i) {
        node_ptr_t rhsNode = any2node(visitInterType(context->interType(i)));
        node_ptr_t typeExprNode = createNodeBy<TypeExprLoad>(TypeOp::Union);
        *typeExprNode << lhsNode << rhsNode;
        lhsNode = typeExprNode;
    }
    leave("UnionType");
    return lhsNode;
}

/*
interType
    : diffType ('&' diffType)*
    ;
*/
any Constructor::visitInterType(OpenCMLParser::InterTypeContext *context) {
    enter("InterType");
    node_ptr_t lhsNode = any2node(visitDiffType(context->diffType(0)));
    for (size_t i = 1; i < context->diffType().size(); ++i) {
        node_ptr_t rhsNode = any2node(visitDiffType(context->diffType(i)));
        node_ptr_t typeExprNode = createNodeBy<TypeExprLoad>(TypeOp::Inter);
        *typeExprNode << lhsNode << rhsNode;
        lhsNode = typeExprNode;
    }
    leave("InterType");
    return lhsNode;
}

/*
diffType
    : keyUnionDiffType ('\\' keyUnionDiffType)*
    ;
*/
any Constructor::visitDiffType(OpenCMLParser::DiffTypeContext *context) {
    enter("DiffType");
    node_ptr_t lhsNode = any2node(visitKeyUnionDiffType(context->keyUnionDiffType(0)));
    for (size_t i = 1; i < context->keyUnionDiffType().size(); ++i) {
        node_ptr_t rhsNode = any2node(visitKeyUnionDiffType(context->keyUnionDiffType(i)));
        node_ptr_t typeExprNode = createNodeBy<TypeExprLoad>(TypeOp::Diff);
        *typeExprNode << lhsNode << rhsNode;
        lhsNode = typeExprNode;
    }
    leave("DiffType");
    return lhsNode;
}

/*
keyUnionDiffType
    : keyInterType (('+' | '-') keyInterType)*
    ;
*/
any Constructor::visitKeyUnionDiffType(OpenCMLParser::KeyUnionDiffTypeContext *context) {
    enter("KeyUnionDiffType");
    node_ptr_t lhsNode = any2node(visitKeyInterType(context->keyInterType(0)));
    for (size_t i = 1; i < context->keyInterType().size(); ++i) {
        TypeOp op;
        string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "+") {
            op = TypeOp::KeyUnion;
        } else if (strOp == "-") {
            op = TypeOp::KeyDiff;
        } else {
            throw std::runtime_error("Invalid key union diff operator: " + strOp);
        }
        node_ptr_t rhsNode = any2node(visitKeyInterType(context->keyInterType(i)));
        node_ptr_t typeExprNode = createNodeBy<TypeExprLoad>(op);
        *typeExprNode << lhsNode << rhsNode;
        lhsNode = typeExprNode;
    }
    leave("KeyUnionDiffType");
    return lhsNode;
}

/*
keyInterType
    : typeUnit ('^' typeUnit)*
    ;
*/
any Constructor::visitKeyInterType(OpenCMLParser::KeyInterTypeContext *context) {
    enter("KeyInterType");
    node_ptr_t lhsNode = any2node(visitTypeUnit(context->typeUnit(0)));
    for (size_t i = 1; i < context->typeUnit().size(); ++i) {
        node_ptr_t rhsNode = any2node(visitTypeUnit(context->typeUnit(i)));
        node_ptr_t typeExprNode = createNodeBy<TypeExprLoad>(TypeOp::KeyInter);
        *typeExprNode << lhsNode << rhsNode;
        lhsNode = typeExprNode;
    }
    leave("KeyInterType");
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
        res = any2node(visitListType(context->listType()));
    }
    leave("TypeUnit");
    return res;
}

/*
listType
    : specType ('[' ']')*
    ;
*/
any Constructor::visitListType(OpenCMLParser::ListTypeContext *context) {
    enter("ListType");
    size_t dims = (context->children.size() - 1) / 2;
    node_ptr_t res = createNodeBy<ListTypeLoad>(dims);
    *res << any2node(visitSpecType(context->specType()));
    leave("ListType");
    return res;
}

/*
typeOrData : typeExpr | CONST dataExpr ;
*/
any Constructor::visitTypeOrData(OpenCMLParser::TypeOrDataContext *context) {
    enter("TypeOrData");
    node_ptr_t res = nullptr;
    if (context->typeExpr()) {
        res = any2node(visitTypeExpr(context->typeExpr()));
    } else if (context->dataExpr()) {
        res = any2node(visitDataExpr(context->dataExpr()));
    } else {
        throw std::runtime_error("Invalid TypeOrData context");
    }
    leave("TypeOrData");
    return res;
}

/*
specType
    : primaryType ('<' typeOrData (',' typeOrData)* '>')?
    ;
*/
any Constructor::visitSpecType(OpenCMLParser::SpecTypeContext *context) {
    enter("SpecType");
    node_ptr_t res = any2node(visitPrimaryType(context->primaryType()));
    node_ptr_t specTypeNode = createNodeBy<TypeExprLoad>(TypeOp::Specialize);
    if (context->typeOrData().size() > 0) {
        node_ptr_t repeatNode = createNodeBy<RepeatedLoad>("TypeOrData");
        for (auto &typeOrData : context->typeOrData()) {
            *repeatNode << any2node(visitTypeOrData(typeOrData));
        }
        *specTypeNode << res << repeatNode;
        res = specTypeNode;
    }
    leave("SpecType");
    return res;
}

/*
primaryType
    : INNER_ATOM_TYPE
    | dictType
    | identRef
    | '(' typeExpr ')'
    | tupleType
    | funcType
    | TYPEOF dataExpr
    | TYPEAS identDef
    ;
*/
any Constructor::visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) {
    enter("PrimaryType");
    node_ptr_t res = nullptr;
    if (context->INNER_ATOM_TYPE()) {
        Reference ref(context->INNER_ATOM_TYPE()->getText());
        res = createNodeBy<RefTypeLoad>(ref);
    } else if (context->dictType()) {
        res = any2node(visitDictType(context->dictType()));
    } else if (context->identRef()) {
        res = createNodeBy<RefTypeLoad>(any_cast<Reference>(visitIdentRef(context->identRef())));
    } else if (context->typeExpr()) {
        res = any2node(visitTypeExpr(context->typeExpr()));
    } else if (context->tupleType()) {
        res = any2node(visitTupleType(context->tupleType()));
    } else if (context->funcType()) {
        res = any2node(visitFuncType(context->funcType()));
    } else if (context->dataExpr()) {
        res = createNodeBy<TypeExprLoad>(TypeOp::TypeOf);
        *res << any2node(visitDataExpr(context->dataExpr()));
    } else if (context->identDef()) {
        res = createNodeBy<TypeExprLoad>(TypeOp::TypeAs);
        Reference ref(context->identDef()->getText());
        *res << createNodeBy<RefTypeLoad>(ref);
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
    node_ptr_t repeatNode = createNodeBy<RepeatedLoad>("NamedPair");
    for (auto &pair : context->keyTypePair()) {
        node_ptr_t pairNode = any2node(visitKeyTypePair(pair));
        *repeatNode << pairNode;
    }
    leave("DictType");
    return repeatNode;
}

/*
typeList
    : typeExpr (',' typeExpr)*
    ;
*/
any Constructor::visitTypeList(OpenCMLParser::TypeListContext *context) {
    enter("TypeList");
    node_ptr_t repeatNode = createNodeBy<RepeatedLoad>("Type");
    for (auto &typeExpr : context->typeExpr()) {
        node_ptr_t typeExprNode = any2node(visitTypeExpr(typeExpr));
        *repeatNode << typeExprNode;
    }
    leave("TypeList");
    return repeatNode;
}

/*
tupleType
    : '(' typeList? ','? ')'
    ;
*/
any Constructor::visitTupleType(OpenCMLParser::TupleTypeContext *context) {
    enter("TupleType");
    node_ptr_t res = createNodeBy<TupleTypeLoad>();
    if (context->typeList()) {
        node_ptr_t repeatNode = any2node(visitTypeList(context->typeList()));
        *res << repeatNode;
    } else {
        *res << createNodeBy<RepeatedLoad>("Type");
    }
    leave("TupleType");
    return res;
}

/*
funcType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;
*/
any Constructor::visitFuncType(OpenCMLParser::FuncTypeContext *context) {
    enter("FuncType");

    node_ptr_t funcTypeNode = createNodeBy<FuncTypeLoad>();
    auto funcType = unwrapNodeAs<FuncTypeLoad>(funcTypeNode);

    auto modifier = context->modifiers();
    if (modifier) {
        if (modifier->ATOMIC().size() > 0) {
            funcType->setAtomic(true);
        }
        if (modifier->SHARED().size() > 0) {
            funcType->setShared(true);
        }
        if (modifier->SYNC().size() > 0) {
            funcType->setSync(true);
        }
        if (modifier->MACRO().size() > 0) {
            funcType->setMacro(true);
        }
    }

    if (context->angledParams()) {
        node_ptr_t withParams = any2node(visitAngledParams(context->angledParams()));
        *funcTypeNode << withParams;
    }

    if (context->parentParams()) {
        node_ptr_t normParams = any2node(visitParentParams(context->parentParams()));
        *funcTypeNode << normParams;
    }

    node_ptr_t typeOptNode = createNodeBy<OptionalLoad>("Type");
    if (context->typeExpr()) {
        node_ptr_t typeNode = any2node(visitTypeExpr(context->typeExpr()));
        *typeOptNode << typeNode;
    }
    *funcTypeNode << typeOptNode;

    leave("FuncType");
    return funcTypeNode;
}

/*
identDef : IDENTIFIER ;
*/
any Constructor::visitIdentDef(OpenCMLParser::IdentDefContext *context) {
    enter("IdentDef");
    Reference ref(context->IDENTIFIER()->getText());
    leave("IdentDef");
    return ref;
}

/*
identRef : (IDENTIFIER '::')* IDENTIFIER ;
*/
any Constructor::visitIdentRef(OpenCMLParser::IdentRefContext *context) {
    enter("IdentRef");
    Reference ref(context->getText());
    leave("IdentRef");
    return ref;
}
