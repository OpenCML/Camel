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
#include "utils/log.h"
#include "utils/token.h"
#include "utils/type.h"

#define DEBUG_LEVEL 0

using namespace std;
using namespace AST;

template <typename LoadType, typename... Args> node_ptr_t createNodeAs(Args &&...args) {
    return std::make_shared<Node>(
        std::dynamic_pointer_cast<Load>(std::make_shared<LoadType>(std::forward<Args>(args)...)));
}

template <typename LoadType> std::shared_ptr<LoadType> unwrapNodeAs(std::shared_ptr<Node> &node) {
    return std::dynamic_pointer_cast<LoadType>(node->load());
}

inline std::pair<size_t, size_t> extractTokenRangeFromContext(const antlr4::ParserRuleContext *context) {
    if (context && context->getStart() && context->getStop()) {
        return {context->getStart()->getTokenIndex(), context->getStop()->getTokenIndex() + 1};
    } else {
        return {0, 0};
    }
}

inline void setNodeTokenRange(node_ptr_t node, pair<size_t, size_t> range) { node->load()->setTokenRange(range); }
inline void setNodeTokenRange(node_ptr_t node, size_t start, size_t end) { node->load()->setTokenRange(start, end); }

inline void setNodeTokenRangeByContext(node_ptr_t node, const antlr4::ParserRuleContext *context) {
    node->load()->setTokenRange(extractTokenRangeFromContext(context));
}

template <typename Context> inline void setNodeTokenRangeByContexts(node_ptr_t node, const vector<Context *> context) {
    if (context.empty()) {
        node->load()->setTokenRange(0, 0);
        return;
    }
    size_t start = context.front()->getStart()->getTokenIndex();
    size_t end = context.back()->getStop()->getTokenIndex() + 1;
    node->load()->setTokenRange(start, end);
}

inline node_ptr_t any2node(const std::any &a) { return std::any_cast<node_ptr_t>(a); }

/*
program : SEP? (decl SEP?)* EOF;
*/
any Constructor::visitProgram(OpenCMLParser::ProgramContext *context) {
    ENTER("Program");

    root_ = std::make_shared<Node>(module_);

    node_ptr_t stmts = createNodeAs<RepeatedLoad>("Stmts");
    setNodeTokenRangeByContext(stmts, context);
    for (const auto &decl : context->decl()) {
        any res = visitDecl(decl);
        if (res.has_value()) {
            node_ptr_t node = any2node(res);
            if (node) {
                *stmts << node;
            }
        }
    }

    node_ptr_t importRep = createNodeAs<RepeatedLoad>("Import");
    for (auto &import_ : imports_) {
        *importRep << std::make_shared<Node>(import_);
    }
    *root_ << importRep;

    node_ptr_t exportOpt = createNodeAs<OptionalLoad>("Export");
    if (!export_->isEmpty()) {
        *exportOpt << std::make_shared<Node>(export_);
    }
    *root_ << exportOpt;

    *root_ << stmts;

    LEAVE("Program");
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
    ENTER("Decl");
    any res = visit(context->children[0]);
    LEAVE("Decl");
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
    ENTER("Stmt");
    if (context->dataExpr()) {
        node_ptr_t expr = any2node(visitDataExpr(context->dataExpr()));
        node_ptr_t stmt = createNodeAs<ExprStmtLoad>();
        setNodeTokenRangeByContext(stmt, context->dataExpr());
        *stmt << expr;
        LEAVE("Stmt");
        return stmt;
    }
    any res = visit(context->children[0]);
    LEAVE("Stmt");
    return res;
}

/*
stmtList : stmt (SEP? stmt)* SEP? ;
*/
any Constructor::visitStmtList(OpenCMLParser::StmtListContext *context) {
    ENTER("StmtList");
    node_ptr_t block = createNodeAs<StmtBlockLoad>();
    setNodeTokenRangeByContext(block, context);
    node_ptr_t rep = createNodeAs<RepeatedLoad>("Stmt");
    setNodeTokenRangeByContext(rep, context);
    if (context->stmt().size() > 0) {
        for (auto &stmt : context->stmt()) {
            *rep << any2node(visitStmt(stmt));
        }
    }
    *block << rep;
    LEAVE("StmtList");
    return block;
}

/*
moduleDecl : MODULE identDef ;
*/
any Constructor::visitModuleDecl(OpenCMLParser::ModuleDeclContext *context) {
    ENTER("ModuleDecl");
    Reference ref(context->identDef()->getText());
    module_->setRef(ref);
    LEAVE("ModuleDecl");
    return std::any();
}

/*
importDecl : IMPORT (STRING | (identDef | bracedIdents) FROM STRING) ;
*/
any Constructor::visitImportDecl(OpenCMLParser::ImportDeclContext *context) {
    ENTER("ImportDecl");
    auto import_ = std::make_shared<ImportLoad>();
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
    imports_.push_back(import_);
    LEAVE("ImportDecl");
    return std::any();
}

/*
exportDecl : EXPORT (dataDecl | typeDecl | bracedIdents) ;
*/
any Constructor::visitExportDecl(OpenCMLParser::ExportDeclContext *context) {
    ENTER("ExportDecl");
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
    LEAVE("ExportDecl");
    if (res) {
        return res;
    } else {
        return std::any();
    }
}

/*
blockStmt  : WAIT? stmtBlock ;
*/
any Constructor::visitBlockStmt(OpenCMLParser::BlockStmtContext *context) {
    ENTER("BlockStmt");
    node_ptr_t res = any2node(visitStmtBlock(context->stmtBlock()));
    if (context->WAIT()) {
        unwrapNodeAs<StmtBlockLoad>(res)->wait();
    }
    LEAVE("BlockStmt");
    return res;
}

/*
stmtBlock  : SYNC? '{' stmtList? '}' ;
*/
any Constructor::visitStmtBlock(OpenCMLParser::StmtBlockContext *context) {
    ENTER("StmtBlock");
    node_ptr_t blockNode = nullptr;
    if (context->stmtList()) {
        blockNode = any2node(visitStmtList(context->stmtList()));
        if (context->SYNC()) {
            // If SYNC is present, we treat this as a synchronous block statement.
            unwrapNodeAs<StmtBlockLoad>(blockNode)->sync();
        }
    } else {
        // If no statements are present, we create an empty block.
        blockNode = createNodeAs<StmtBlockLoad>();
        setNodeTokenRangeByContext(blockNode, context);
        *blockNode << createNodeAs<RepeatedLoad>("Stmt");
    }
    LEAVE("StmtBlock");
    return blockNode;
}

/*
blockExpr  : stmtBlock | dataExpr ;
*/
any Constructor::visitBlockExpr(OpenCMLParser::BlockExprContext *context) {
    ENTER("BlockExpr");
    node_ptr_t res = nullptr;
    if (context->stmtBlock()) {
        res = any2node(visitStmtBlock(context->stmtBlock()));
    } else if (context->dataExpr()) {
        node_ptr_t expr = any2node(visitDataExpr(context->dataExpr()));
        node_ptr_t stmt = createNodeAs<ExprStmtLoad>();
        setNodeTokenRangeByContext(stmt, context->dataExpr());
        res = createNodeAs<StmtBlockLoad>();
        setNodeTokenRangeByContext(res, context);
        node_ptr_t rep = createNodeAs<RepeatedLoad>("Stmt");
        setNodeTokenRangeByContext(rep, context);
        *stmt << expr;
        *rep << stmt;
        *res << rep;
    }
    LEAVE("BlockExpr");
    return res;
}

/*
funcData   : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr ;
*/
any Constructor::visitFuncData(OpenCMLParser::FuncDataContext *context) {
    ENTER("FuncData");
    node_ptr_t funcTypeNode = createNodeAs<FuncTypeLoad>();
    setNodeTokenRangeByContext(funcTypeNode, context);
    auto funcType = unwrapNodeAs<FuncTypeLoad>(funcTypeNode);

    auto modifier = context->modifiers();
    ModifierSet modSet;
    if (modifier) {
        if (modifier->ATOMIC().size() > 0) {
            modSet.insert(Modifier::Atomic);
        }
        if (modifier->SHARED().size() > 0) {
            modSet.insert(Modifier::Shared);
        }
        if (modifier->SYNC().size() > 0) {
            modSet.insert(Modifier::Sync);
        }
        if (modifier->MACRO().size() > 0) {
            modSet.insert(Modifier::Macro);
        }
    }
    funcType->setModifiers(modSet);

    if (context->angledParams()) {
        node_ptr_t withParams = any2node(visitAngledParams(context->angledParams()));
        *funcTypeNode << withParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    if (context->parentParams()) {
        node_ptr_t normParams = any2node(visitParentParams(context->parentParams()));
        *funcTypeNode << normParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    node_ptr_t typeOptNode = createNodeAs<OptionalLoad>("Type");
    setNodeTokenRange(typeOptNode, context->getStart()->getTokenIndex(),
                      (context->typeExpr() ? context->typeExpr()->getStop()->getTokenIndex()
                                           : context->parentParams()->getStart()->getTokenIndex()) +
                          1);
    if (context->typeExpr()) {
        node_ptr_t typeNode = any2node(visitTypeExpr(context->typeExpr()));
        *typeOptNode << typeNode;
    }
    *funcTypeNode << typeOptNode;

    node_ptr_t blockNode = any2node(visitBlockExpr(context->blockExpr()));

    node_ptr_t funcNode = createNodeAs<FuncDataLoad>();
    setNodeTokenRangeByContext(funcNode, context);
    *funcNode << funcTypeNode;
    *funcNode << blockNode;

    LEAVE("FuncData");
    return funcNode;
}

/*
funcDecl   :
        (WITH angledParams)?
        EXPORT? implMark? modifiers?
        FUNC identDef parentParams (':' typeExpr)? stmtBlock ;
*/
any Constructor::visitFuncDecl(OpenCMLParser::FuncDeclContext *context) {
    ENTER("FuncDecl");

    node_ptr_t funcTypeNode = createNodeAs<FuncTypeLoad>();
    pair<size_t, size_t> typeRange = {context->getStart()->getTokenIndex(),
                                      context->stmtBlock()->getStart()->getTokenIndex() + 1};
    setNodeTokenRange(funcTypeNode, typeRange);
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
    ModifierSet modSet;
    if (modifier) {
        if (modifier->ATOMIC().size() > 0) {
            modSet.insert(Modifier::Atomic);
        }
        if (modifier->SHARED().size() > 0) {
            modSet.insert(Modifier::Shared);
        }
        if (modifier->SYNC().size() > 0) {
            modSet.insert(Modifier::Sync);
        }
        if (modifier->MACRO().size() > 0) {
            modSet.insert(Modifier::Macro);
        }
    }
    funcType->setModifiers(modSet);

    if (context->angledParams()) {
        node_ptr_t withParams = any2node(visitAngledParams(context->angledParams()));
        *funcTypeNode << withParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    if (context->parentParams()) {
        node_ptr_t normParams = any2node(visitParentParams(context->parentParams()));
        *funcTypeNode << normParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    node_ptr_t typeOptNode = createNodeAs<OptionalLoad>("Type");
    setNodeTokenRange(typeOptNode, typeRange);
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

    node_ptr_t funcNode = createNodeAs<FuncDataLoad>(ref);
    setNodeTokenRangeByContext(funcNode, context);
    *funcNode << funcTypeNode;
    *funcNode << blockNode;

    node_ptr_t funcDeclNode = createNodeAs<FuncDeclLoad>(ref);
    *funcDeclNode << funcNode;
    setNodeTokenRangeByContext(funcDeclNode, context);

    LEAVE("FuncDecl");
    return funcDeclNode;
}

/*
parentIdents  : '(' identList? ','? ')' ;
*/
any Constructor::visitParentIdents(OpenCMLParser::ParentIdentsContext *context) {
    ENTER("ParentIdents");
    vector<Reference> refs;
    if (context->identList()) {
        refs = any_cast<vector<Reference>>(visitIdentList(context->identList()));
    }
    LEAVE("ParentIdents");
    return refs;
}

/*
bracedIdents  : '{' identList? ','? '}' ;
*/
any Constructor::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {
    ENTER("BracedIdents");
    vector<Reference> refs;
    if (context->identList()) {
        refs = any_cast<vector<Reference>>(visitIdentList(context->identList()));
    }
    LEAVE("BracedIdents");
    return refs;
}

/*
bracketIdents : '[' identList? ','? ']' ;
*/
any Constructor::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {
    ENTER("BracketIdents");
    vector<Reference> refs;
    if (context->identList()) {
        refs = any_cast<vector<Reference>>(visitIdentList(context->identList()));
    }
    LEAVE("BracketIdents");
    return refs;
}

/*
carrier       : identList | parentIdents | bracedIdents | bracketIdents ;
*/
any Constructor::visitCarrier(OpenCMLParser::CarrierContext *context) {
    ENTER("Carrier");
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
    LEAVE("Carrier");
    return make_tuple(type, refs);
}

/*
dataDecl   : (LET | VAR) carrier (':' typeList)? '=' dataList ;
*/
any Constructor::visitDataDecl(OpenCMLParser::DataDeclContext *context) {
    ENTER("DataDecl");
    bool isVar = false;
    if (context->VAR()) {
        isVar = true;
    }
    auto [type, refs] = any_cast<std::tuple<UnpackType, vector<Reference>>>(visitCarrier(context->carrier()));
    node_ptr_t dataDeclNode = createNodeAs<DataDeclLoad>(isVar, type, refs);
    setNodeTokenRangeByContext(dataDeclNode, context);

    if (context->typeList()) {
        *dataDeclNode << any2node(visitTypeList(context->typeList()));
    } else {
        node_ptr_t typeListNode = createNodeAs<RepeatedLoad>("Type");
        *dataDeclNode << typeListNode;
    }

    *dataDeclNode << any2node(visitDataList(context->dataList()));
    LEAVE("DataDecl");
    return dataDeclNode;
}

/*
typeDecl   : implMark? TYPE identDef '=' (typeExpr | STRING) ;
*/
any Constructor::visitTypeDecl(OpenCMLParser::TypeDeclContext *context) {
    ENTER("TypeDecl");
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

    node_ptr_t typeDeclNode = createNodeAs<TypeDeclLoad>(ref, implMark, uri);
    setNodeTokenRangeByContext(typeDeclNode, context);

    node_ptr_t typeOptNode = createNodeAs<OptionalLoad>("Type");
    if (context->typeExpr()) {
        setNodeTokenRangeByContext(typeOptNode, context->typeExpr());
        *typeOptNode << any2node(visitTypeExpr(context->typeExpr()));
    }

    *typeDeclNode << typeOptNode;
    LEAVE("TypeDecl");
    return typeDeclNode;
}

/*
useDecl    : USE (identDef '=')? identRef ;
*/
any Constructor::visitUseDecl(OpenCMLParser::UseDeclContext *context) {
    ENTER("UseDecl");
    Reference ref;
    if (context->identDef()) {
        ref.parse(context->identDef()->getText());
    }
    Reference alias(context->identRef()->getText());
    node_ptr_t nameDeclNode = createNodeAs<NameDeclLoad>(ref, alias);
    setNodeTokenRangeByContext(nameDeclNode, context);
    LEAVE("UseDecl");
    return nameDeclNode;
}

/*
retStmt    : (RETURN | RAISE | THROW) dataList ;
*/
any Constructor::visitRetStmt(OpenCMLParser::RetStmtContext *context) {
    ENTER("RetStmt");
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
    node_ptr_t exitNode = createNodeAs<ExitStmtLoad>(exitType);
    setNodeTokenRangeByContext(exitNode, context);
    if (context->dataList()) {
        *exitNode << any2node(visitDataList(context->dataList()));
    }
    LEAVE("RetStmt");
    return exitNode;
}

/*
implMark    : INNER | OUTER ;
*/
any Constructor::visitImplMark(OpenCMLParser::ImplMarkContext *context) {
    ENTER("ImplMark");
    // NOTHING TO DO
    LEAVE("ImplMark");
    return nullptr;
}

/*
modifiers   : (ATOMIC | SHARED | SYNC | MACRO)+ ;
*/
any Constructor::visitModifiers(OpenCMLParser::ModifiersContext *context) {
    ENTER("Modifiers");
    // NOTHING TO DO
    LEAVE("Modifiers");
    return nullptr;
}

/*
indexValue   : '...'? dataExpr ;
*/
any Constructor::visitIndexValue(OpenCMLParser::IndexValueContext *context) {
    ENTER("IndexValue");
    node_ptr_t res = any2node(visitDataExpr(context->dataExpr()));
    LEAVE("IndexValue");
    return res;
}

/*
keyTypePair  : identDef ':' typeExpr ;
*/
any Constructor::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) {
    ENTER("KeyTypePair");
    Reference ref(context->identDef()->getText());
    node_ptr_t namedTypeNode = createNodeAs<NamedTypeLoad>(ref);
    setNodeTokenRangeByContext(namedTypeNode, context);
    if (context->typeExpr()) {
        *namedTypeNode << any2node(visitTypeExpr(context->typeExpr()));
    }
    LEAVE("KeyTypePair");
    return namedTypeNode;
}

/*
keyValuePair : identDef ':' dataExpr | '...' dataExpr ;
*/
any Constructor::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {
    ENTER("KeyValuePair");
    Reference ref(context->identDef()->getText());
    node_ptr_t namedDataNode = createNodeAs<NamedDataLoad>(ref);
    setNodeTokenRangeByContext(namedDataNode, context);
    if (context->dataExpr()) {
        *namedDataNode << any2node(visitDataExpr(context->dataExpr()));
    }
    LEAVE("KeyValuePair");
    return namedDataNode;
}

/*
keyParamPair : VAR? identDef ':' typeExpr ('=' dataExpr)? ;
*/
any Constructor::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {
    ENTER("KeyParamPair");
    bool isVar = false;
    if (context->VAR()) {
        isVar = true;
    }
    Reference ref(context->identDef()->getText());
    node_ptr_t namedPairNode = createNodeAs<NamedPairLoad>(ref, isVar);
    setNodeTokenRangeByContext(namedPairNode, context);
    *namedPairNode << any2node(visitTypeExpr(context->typeExpr()));
    node_ptr_t dataExprOptNode = createNodeAs<OptionalLoad>("DataExpr");
    if (context->dataExpr()) {
        setNodeTokenRangeByContext(dataExprOptNode, context->dataExpr());
        *dataExprOptNode << any2node(visitDataExpr(context->dataExpr()));
    }
    *namedPairNode << dataExprOptNode;
    LEAVE("KeyParamPair");
    return namedPairNode;
}

/*
dataList     : dataExpr (',' dataExpr)* ;
*/
any Constructor::visitDataList(OpenCMLParser::DataListContext *context) {
    ENTER("DataList");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(rep, context);
    for (const auto &dataExpr : context->dataExpr()) {
        *rep << any2node(visitDataExpr(dataExpr));
    }
    LEAVE("DataList");
    return rep;
}

/*
identList    : identDef (',' identDef)* ;
*/
any Constructor::visitIdentList(OpenCMLParser::IdentListContext *context) {
    ENTER("IdentList");
    std::vector<Reference> refs;
    for (const auto &identDef : context->identDef()) {
        refs.emplace_back(identDef->getText());
    }
    LEAVE("IdentList");
    return refs;
}

/*
indexValues  : indexValue (',' indexValue)* ;
*/
any Constructor::visitIndexValues(OpenCMLParser::IndexValuesContext *context) {
    ENTER("IndexValues");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(rep, context);
    for (const auto &indexValue : context->indexValue()) {
        *rep << any2node(visitIndexValue(indexValue));
    }
    LEAVE("IndexValues");
    return rep;
}

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
*/
any Constructor::visitPairedValues(OpenCMLParser::PairedValuesContext *context) {
    ENTER("PairedValues");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("NamedData");
    setNodeTokenRangeByContext(rep, context);
    for (const auto &keyValuePair : context->keyValuePair()) {
        *rep << any2node(visitKeyValuePair(keyValuePair));
    }
    LEAVE("PairedValues");
    return rep;
}

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
any Constructor::visitPairedParams(OpenCMLParser::PairedParamsContext *context) {
    ENTER("PairedParams");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("NamedPair");
    setNodeTokenRangeByContext(rep, context);
    for (const auto &keyParamPair : context->keyParamPair()) {
        *rep << any2node(visitKeyParamPair(keyParamPair));
    }
    LEAVE("PairedParams");
    return rep;
}

/*
argumentList : indexValues (',' pairedValues)? | pairedValues ;
*/
any Constructor::visitArgumentList(OpenCMLParser::ArgumentListContext *context) {
    ENTER("ArgumentList");
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    if (context->indexValues()) {
        setNodeTokenRangeByContext(dataList, context->indexValues());
        dataList = any2node(visitIndexValues(context->indexValues()));
    }
    node_ptr_t namedDataList = createNodeAs<RepeatedLoad>("NamedData");
    if (context->pairedValues()) {
        setNodeTokenRangeByContext(namedDataList, context->pairedValues());
        namedDataList = any2node(visitPairedValues(context->pairedValues()));
    }
    LEAVE("ArgumentList");
    return make_pair(dataList, namedDataList);
}

/*
indices : '[' dataExpr (':' dataExpr (':' dataExpr)?)? ']' ;
*/
any Constructor::visitIndices(OpenCMLParser::IndicesContext *context) {
    ENTER("Indices");
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(dataList, context);
    if (context->dataExpr(0)) {
        *dataList << any2node(visitDataExpr(context->dataExpr(0)));
    }
    if (context->dataExpr(1)) {
        *dataList << any2node(visitDataExpr(context->dataExpr(1)));
    }
    if (context->dataExpr(2)) {
        *dataList << any2node(visitDataExpr(context->dataExpr(2)));
    }
    LEAVE("Indices");
    return dataList;
}

/*
parentParams : '(' pairedParams? ','? ')' ;
*/
any Constructor::visitParentParams(OpenCMLParser::ParentParamsContext *context) {
    ENTER("ParentParams");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("NamedPair");
    setNodeTokenRangeByContext(rep, context);
    const auto &pairedParams = context->pairedParams();
    if (pairedParams) {
        rep = any2node(visitPairedParams(pairedParams));
    }
    LEAVE("ParentParams");
    return rep;
}

/*
parentArgues : '(' argumentList? ','? ')' ;
*/
any Constructor::visitParentArgues(OpenCMLParser::ParentArguesContext *context) {
    ENTER("ParentArgues");
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(dataList, context);
    node_ptr_t namedDataList = createNodeAs<RepeatedLoad>("NamedData");
    if (context->argumentList()) {
        auto res = any_cast<std::pair<node_ptr_t, node_ptr_t>>(visitArgumentList(context->argumentList()));
        dataList = res.first;
        namedDataList = res.second;
    }
    LEAVE("ParentArgues");
    return make_pair(dataList, namedDataList);
}

/*
angledParams : '<' pairedParams? ','? '>' ;
*/
any Constructor::visitAngledParams(OpenCMLParser::AngledParamsContext *context) {
    ENTER("AngledParams");
    node_ptr_t res = createNodeAs<RepeatedLoad>("NamedPair");
    setNodeTokenRangeByContext(res, context);
    const auto &pairedParams = context->pairedParams();
    if (pairedParams) {
        res = any2node(visitPairedParams(pairedParams));
    }
    LEAVE("AngledParams");
    return res;
}

/*
angledValues : '<' argumentList? ','? '>' ;
*/
any Constructor::visitAngledValues(OpenCMLParser::AngledValuesContext *context) {
    ENTER("AngledValues");
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(dataList, context);
    node_ptr_t namedDataList = createNodeAs<RepeatedLoad>("NamedData");
    if (context->argumentList()) {
        auto res = any_cast<std::pair<node_ptr_t, node_ptr_t>>(visitArgumentList(context->argumentList()));
        dataList = res.first;
        namedDataList = res.second;
    }
    LEAVE("AngledValues");
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
    ENTER("Pattern");
    throw std::runtime_error("visitPattern: not implemented yet");
    LEAVE("Pattern");
    return nullptr;
}

/*
matchCase
    : CASE pattern ('|' pattern)* '=>' blockExpr
    ;
*/
any Constructor::visitMatchCase(OpenCMLParser::MatchCaseContext *context) {
    // TODO: match case
    ENTER("MatchCase");
    throw std::runtime_error("visitMatchCase: not implemented yet");
    LEAVE("MatchCase");
    return nullptr;
}

/*
catchClause
    : CATCH identDef ':' typeExpr stmtBlock
    ;
*/
any Constructor::visitCatchClause(OpenCMLParser::CatchClauseContext *context) {
    // TODO: catch clause
    ENTER("CatchClause");
    throw std::runtime_error("visitCatchClause: not implemented yet");
    LEAVE("CatchClause");
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
    ENTER("CtrlExpr");
    node_ptr_t ctrlNode = nullptr;
    switch (context->getAltNumber()) {
    case 1: // IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
    {
        ctrlNode = createNodeAs<IfExprLoad>();
        setNodeTokenRangeByContext(ctrlNode, context);
        *ctrlNode << any2node(visitLogicalOrExpr(context->logicalOrExpr()));
        *ctrlNode << any2node(visitBlockExpr(context->blockExpr(0)));
        node_ptr_t elseBlockNode = createNodeAs<OptionalLoad>("StmtBlock");
        *ctrlNode << elseBlockNode;
        if (context->blockExpr(1)) {
            setNodeTokenRangeByContext(elseBlockNode, context->blockExpr(1));
            *elseBlockNode << any2node(visitBlockExpr(context->blockExpr(1)));
        } else {
            node_ptr_t stmts = createNodeAs<StmtBlockLoad>();
            *stmts << createNodeAs<RepeatedLoad>("Stmt");
            *elseBlockNode << stmts;
        }
    } break;

    default:
        throw std::runtime_error("visitCtrlExpr: unsupported control expression type");
    }
    LEAVE("CtrlExpr");
    return ctrlNode;
}

/*
dataExpr
    : waitExpr
    | ctrlExpr
    ;
*/
any Constructor::visitDataExpr(OpenCMLParser::DataExprContext *context) {
    ENTER("DataExpr");
    node_ptr_t res = nullptr;
    if (context->waitExpr()) {
        res = any2node(visitWaitExpr(context->waitExpr()));
    } else if (context->ctrlExpr()) {
        res = any2node(visitCtrlExpr(context->ctrlExpr()));
    }
    LEAVE("DataExpr");
    return res;
}

/*
waitExpr : WAIT? assignExpr ;
*/
any Constructor::visitWaitExpr(OpenCMLParser::WaitExprContext *context) {
    ENTER("WaitExpr");
    node_ptr_t dataNode = any2node(visitAssignExpr(context->assignExpr()));
    if (context->WAIT()) {
        unwrapNodeAs<DataLoad>(dataNode)->wait();
    }
    LEAVE("WaitExpr");
    return dataNode;
}

/*
assignExpr
    : logicalOrExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=') logicalOrExpr)?
    ;
*/
any Constructor::visitAssignExpr(OpenCMLParser::AssignExprContext *context) {
    ENTER("AssignExpr");
    node_ptr_t lhsNode = any2node(visitLogicalOrExpr(context->logicalOrExpr(0)));
    for (size_t i = 1; i < context->logicalOrExpr().size(); i++) {
        BinaryDataOp op;
        string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "=") {
            op = BinaryDataOp::Assign;
        } else if (strOp == "+=") {
            op = BinaryDataOp::AssignAdd;
        } else if (strOp == "-=") {
            op = BinaryDataOp::AssignSub;
        } else if (strOp == "*=") {
            op = BinaryDataOp::AssignMul;
        } else if (strOp == "/=") {
            op = BinaryDataOp::AssignDiv;
        } else if (strOp == "%=") {
            op = BinaryDataOp::AssignMod;
        } else if (strOp == "^=") {
            op = BinaryDataOp::AssignExp;
        } else if (strOp == "@=") {
            op = BinaryDataOp::AssignMat;
        } else if (strOp == "&=") {
            op = BinaryDataOp::AssignAnd;
        } else if (strOp == "|=") {
            op = BinaryDataOp::AssignOr;
        } else {
            throw std::runtime_error("Invalid assignment operator: " + strOp);
        }
        node_ptr_t rhsNode = any2node(visitLogicalOrExpr(context->logicalOrExpr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        // TODO: set the token range for dataExprNode
        setNodeTokenRangeByContext(dataExprNode, context->logicalOrExpr(i));
        *dataExprNode << lhsNode << rhsNode;
        lhsNode = dataExprNode;
    }
    LEAVE("AssignExpr");
    return lhsNode;
}

/*
logicalOrExpr
    : logicalAndExpr ('||' logicalAndExpr)*
    ;
*/
any Constructor::visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) {
    ENTER("LogicalOrExpr");
    node_ptr_t lhsNode = any2node(visitLogicalAndExpr(context->logicalAndExpr(0)));
    for (size_t i = 1; i < context->logicalAndExpr().size(); ++i) {
        node_ptr_t rhsNode = any2node(visitLogicalAndExpr(context->logicalAndExpr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(BinaryDataOp::Or);
        setNodeTokenRangeByContext(dataExprNode, context->logicalAndExpr(i));
        *dataExprNode << lhsNode << rhsNode;
        lhsNode = dataExprNode;
    }
    LEAVE("LogicalOrExpr");
    return lhsNode;
}

/*
logicalAndExpr
    : equalityExpr ('&&' equalityExpr)*
    ;
*/
any Constructor::visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) {
    ENTER("LogicalAndExpr");
    node_ptr_t lhsNode = any2node(visitEqualityExpr(context->equalityExpr(0)));
    for (size_t i = 1; i < context->equalityExpr().size(); ++i) {
        node_ptr_t rhsNode = any2node(visitEqualityExpr(context->equalityExpr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(BinaryDataOp::And);
        setNodeTokenRangeByContext(dataExprNode, context->equalityExpr(i));
        *dataExprNode << lhsNode << rhsNode;
        lhsNode = dataExprNode;
    }
    LEAVE("LogicalAndExpr");
    return lhsNode;
}

/*
equalityExpr
    : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
    ;
*/
any Constructor::visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) {
    ENTER("EqualityExpr");
    node_ptr_t lhsNode =
        any2node(visitRelationalExpr(context->relationalExpr(0))); // get the left hand side of the equality expr
    for (size_t i = 1; i < context->relationalExpr().size(); ++i) {
        BinaryDataOp op;
        string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "===") {
            op = BinaryDataOp::StrictEq;
        } else if (strOp == "!==") {
            op = BinaryDataOp::StrictNeq;
        } else if (strOp == "==") {
            op = BinaryDataOp::Eq;
        } else if (strOp == "!=") {
            op = BinaryDataOp::Neq;
        } else {
            throw std::runtime_error("Invalid equality operator: " + strOp);
        }
        node_ptr_t rhsNode = any2node(visitRelationalExpr(context->relationalExpr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        setNodeTokenRangeByContext(dataExprNode, context->relationalExpr(i));
        *dataExprNode << lhsNode << rhsNode;
        lhsNode = dataExprNode;
    }
    LEAVE("EqualityExpr");
    return lhsNode;
}

/*
relationalExpr
    : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
    ;
*/
any Constructor::visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) {
    ENTER("RelationalExpr");
    node_ptr_t lhsNode = any2node(visitAdditiveExpr(context->additiveExpr(0)));
    for (size_t i = 1; i < context->additiveExpr().size(); ++i) {
        BinaryDataOp op;
        string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "<") {
            op = BinaryDataOp::Less;
        } else if (strOp == ">") {
            op = BinaryDataOp::Greater;
        } else if (strOp == "<=") {
            op = BinaryDataOp::LessEq;
        } else if (strOp == ">=") {
            op = BinaryDataOp::GreaterEq;
        } else {
            throw std::runtime_error("Invalid relational operator: " + strOp);
        }
        node_ptr_t rhsNode = any2node(visitAdditiveExpr(context->additiveExpr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        setNodeTokenRangeByContext(dataExprNode, context->additiveExpr(i));
        *dataExprNode << lhsNode << rhsNode;
        lhsNode = dataExprNode;
    }
    LEAVE("RelationalExpr");
    return lhsNode;
}

/*
additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;
*/
any Constructor::visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) {
    ENTER("AdditiveExpr");
    node_ptr_t lhsNode = any2node(visitMultiplicativeExpr(context->multiplicativeExpr(0)));
    for (size_t i = 1; i < context->multiplicativeExpr().size(); i++) {
        BinaryDataOp op;
        string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "+") {
            op = BinaryDataOp::Add;
        } else if (strOp == "-") {
            op = BinaryDataOp::Sub;
        } else {
            throw std::runtime_error("Invalid additive operator: " + strOp);
        }
        node_ptr_t rhsNode = any2node(visitMultiplicativeExpr(context->multiplicativeExpr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        setNodeTokenRangeByContext(dataExprNode, context->multiplicativeExpr(i));
        *dataExprNode << lhsNode << rhsNode;
        lhsNode = dataExprNode;
    }
    LEAVE("AdditiveExpr");
    return lhsNode;
}

/*
multiplicativeExpr
    : nullableExpr (('*' | '/' | '^' | '@' | '%') nullableExpr)*
    ;
*/
any Constructor::visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) {
    ENTER("MultiplicativeExpr");
    BinaryDataOp op;
    node_ptr_t lhsNode = any2node(visitNullableExpr(context->nullableExpr(0)));
    for (size_t i = 1; i < context->nullableExpr().size(); i++) {
        string strOp = context->children[i * 2 - 1]->getText();
        if (strOp == "*") {
            op = BinaryDataOp::Mul;
        } else if (strOp == "/") {
            op = BinaryDataOp::Div;
        } else if (strOp == "^") {
            op = BinaryDataOp::Exp;
        } else if (strOp == "@") {
            op = BinaryDataOp::Mat;
        } else if (strOp == "%") {
            op = BinaryDataOp::Mod;
        } else {
            throw std::runtime_error("Invalid multiplicative operator: " + strOp);
        }
        node_ptr_t rhsNode = any2node(visitNullableExpr(context->nullableExpr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        setNodeTokenRangeByContext(dataExprNode, context->nullableExpr(i));
        *dataExprNode << lhsNode << rhsNode;
        lhsNode = dataExprNode;
    }
    LEAVE("MultiplicativeExpr");
    return lhsNode;
}

/*
nullableExpr
    : unaryExpr (('??' | '!!') dataExpr)?
    ;
*/
any Constructor::visitNullableExpr(OpenCMLParser::NullableExprContext *context) {
    ENTER("NullableExpr");
    node_ptr_t res = any2node(visitUnaryExpr(context->unaryExpr()));

    if (context->children.size() > 1) {
        string strOp = context->children[1]->getText();
        ReservedDataOp op;
        if (strOp == "??") {
            op = ReservedDataOp::NullThen;
        } else if (strOp == "!!") {
            op = ReservedDataOp::ErrorThen;
        } else {
            throw std::runtime_error("Invalid nullable operator: " + strOp);
        }

        if (context->dataExpr()) {
            node_ptr_t dataExprNode = createNodeAs<ReservedExprLoad>(op);
            setNodeTokenRangeByContext(dataExprNode, context->dataExpr());
            *dataExprNode << res << any2node(visitDataExpr(context->dataExpr()));
            res = dataExprNode;
        }
    }

    LEAVE("NullableExpr");
    return res;
}

/*
unaryExpr
    : linkExpr ((AS | IS) typeExpr)?
    | ('!' | '-' | '~') linkExpr
    ;
*/
any Constructor::visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) {
    ENTER("UnaryExpr");
    node_ptr_t res = nullptr;

    switch (context->getAltNumber()) {
    case 1: {
        if (context->children.size() == 1) {
            res = any2node(visitLinkExpr(context->linkExpr()));
        } else {
            ReservedDataOp op;
            if (context->AS()) {
                op = ReservedDataOp::As;
            } else if (context->IS()) {
                op = ReservedDataOp::Is;
            } else {
                throw std::runtime_error("Invalid unary operator: " + context->children[0]->getText());
            }
            node_ptr_t dataNode = any2node(visitLinkExpr(context->linkExpr()));
            node_ptr_t typeNode = any2node(visitTypeExpr(context->typeExpr()));
            res = createNodeAs<ReservedExprLoad>(op);
            setNodeTokenRangeByContext(res, context);
            *res << dataNode << typeNode;
        }
        break;
    }
    case 2: {
        UnaryDataOp op;
        string strOp = context->children[0]->getText();
        if (strOp == "!") {
            op = UnaryDataOp::Not;
        } else if (strOp == "-") {
            op = UnaryDataOp::Neg;
        } else if (strOp == "~") {
            op = UnaryDataOp::Inv;
        } else {
            throw std::runtime_error("Invalid unary operator: " + strOp);
        }
        node_ptr_t dataNode = any2node(visitLinkExpr(context->linkExpr()));
        res = createNodeAs<UnaryExprLoad>(op);
        setNodeTokenRangeByContext(res, context);
        *res << dataNode;
        break;
    }
    default:
        throw std::runtime_error("Invalid alternative number in UnaryExpr: " + std::to_string(context->getAltNumber()));
    }

    LEAVE("UnaryExpr");
    return res;
}

/*
linkExpr
    : bindExpr (('->' | '?->') bindExpr)*
    ;
*/
any Constructor::visitLinkExpr(OpenCMLParser::LinkExprContext *context) {
    ENTER("LinkExpr");
    node_ptr_t lhsNode = any2node(visitBindExpr(context->bindExpr(0)));
    for (size_t i = 1; i < context->bindExpr().size(); i++) {
        string strOp = context->children[i * 2 - 1]->getText();
        node_ptr_t linkNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
        node_ptr_t rhsNode = any2node(visitBindExpr(context->bindExpr(i)));
        if (strOp == "?->") {
            node_ptr_t notNullNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::NotNullThen);
            setNodeTokenRangeByContext(notNullNode, context->bindExpr(i));
            *notNullNode << lhsNode;
            lhsNode = notNullNode;
        }
        node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
        setNodeTokenRangeByContext(dataList, context->bindExpr(i));
        node_ptr_t namedDataList = createNodeAs<RepeatedLoad>("NamedData");
        setNodeTokenRangeByContext(namedDataList, context->bindExpr(i));
        *dataList << lhsNode;
        *linkNode << rhsNode << dataList << namedDataList;
        lhsNode = linkNode;
    }
    LEAVE("LinkExpr");
    return lhsNode;
}

/*
bindExpr
    : annoExpr (('..' | '?..') annoExpr)*
    ;
*/
any Constructor::visitBindExpr(OpenCMLParser::BindExprContext *context) {
    ENTER("BindExpr");
    node_ptr_t lhsNode = any2node(visitAnnoExpr(context->annoExpr(0)));
    for (size_t i = 1; i < context->annoExpr().size(); i++) {
        string strOp = context->children[i * 2 - 1]->getText();
        node_ptr_t dataNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Bind);
        setNodeTokenRangeByContext(dataNode, context->annoExpr(i));
        node_ptr_t rhsNode = any2node(visitAnnoExpr(context->annoExpr(i)));
        if (strOp == "?..") {
            node_ptr_t notNullNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::NotNullThen);
            setNodeTokenRangeByContext(notNullNode, context->annoExpr(i));
            *notNullNode << lhsNode;
            lhsNode = notNullNode;
        }
        *dataNode << lhsNode << rhsNode;
        lhsNode = dataNode;
    }
    LEAVE("BindExpr");
    return lhsNode;
}

/*
annoExpr
    : withExpr ({isAdjacent()}? (indices | parentArgues | angledValues | '!'))*
    ;
*/
any Constructor::visitAnnoExpr(OpenCMLParser::AnnoExprContext *context) {
    ENTER("AnnoExpr");
    node_ptr_t lhsNode = any2node(visitWithExpr(context->withExpr()));
    for (size_t i = 1; i < context->children.size(); i++) {
        auto child = context->children[i];
        if (antlr4::RuleContext::is(child)) {
            node_ptr_t exprNode = nullptr;
            if (tt::is_instance_of<OpenCMLParser::IndicesContext>(child)) {
                exprNode = createNodeAs<BinaryExprLoad>(BinaryDataOp::Index);
                setNodeTokenRangeByContext(exprNode, tt::as<OpenCMLParser::IndicesContext>(child));
                node_ptr_t rhsNode = any2node(visitIndices(tt::as<OpenCMLParser::IndicesContext>(child)));
                *exprNode << lhsNode << rhsNode;
            } else if (tt::is_instance_of<OpenCMLParser::ParentArguesContext>(child)) {
                exprNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
                setNodeTokenRangeByContext(exprNode, tt::as<OpenCMLParser::ParentArguesContext>(child));
                auto [dataList, namedDataList] = any_cast<std::pair<node_ptr_t, node_ptr_t>>(
                    visitParentArgues(tt::as<OpenCMLParser::ParentArguesContext>(child)));
                *exprNode << lhsNode << dataList << namedDataList;
            } else if (tt::is_instance_of<OpenCMLParser::AngledValuesContext>(child)) {
                exprNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::With);
                setNodeTokenRangeByContext(exprNode, tt::as<OpenCMLParser::AngledValuesContext>(child));
                auto [dataList, namedDataList] = any_cast<std::pair<node_ptr_t, node_ptr_t>>(
                    visitAngledValues(tt::as<OpenCMLParser::AngledValuesContext>(child)));
                *exprNode << lhsNode << dataList << namedDataList;
            }
            lhsNode = exprNode;
        } else {
            unwrapNodeAs<DataLoad>(lhsNode)->setNonNull(true);
        }
    }
    LEAVE("AnnoExpr");
    return lhsNode;
}

/*
withExpr
    : accessExpr (('.' | '?.') accessExpr)*
    ;
*/
any Constructor::visitWithExpr(OpenCMLParser::WithExprContext *context) {
    ENTER("WithExpr");
    node_ptr_t lhsNode = any2node(visitAccessExpr(context->accessExpr(0)));
    for (size_t i = 1; i < context->accessExpr().size(); i++) {
        string strOp = context->children[i * 2 - 1]->getText();
        node_ptr_t withNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::With);
        setNodeTokenRangeByContext(withNode, context->accessExpr(i));
        node_ptr_t rhsNode = any2node(visitAccessExpr(context->accessExpr(i)));
        if (strOp == "?.") {
            node_ptr_t notNullNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::NotNullThen);
            setNodeTokenRangeByContext(notNullNode, context->accessExpr(i));
            *notNullNode << lhsNode;
            lhsNode = notNullNode;
        }
        node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
        setNodeTokenRangeByContext(dataList, context->accessExpr(i));
        node_ptr_t namedDataList = createNodeAs<RepeatedLoad>("NamedData");
        setNodeTokenRangeByContext(namedDataList, context->accessExpr(i));
        *dataList << lhsNode;
        *withNode << rhsNode << dataList << namedDataList;
        lhsNode = withNode;
    }
    LEAVE("WithExpr");
    return lhsNode;
}

/*
accessExpr
    : primaryData ('.$' (IDENTIFIER | INTEGER))*
    ;
*/
any Constructor::visitAccessExpr(OpenCMLParser::AccessExprContext *context) {
    ENTER("AccessExpr");
    node_ptr_t lhsNode = any2node(visitPrimaryData(context->primaryData()));
    for (size_t i = 1; i < context->children.size(); i += 2) {
        string strOp = context->children[i]->getText();
        if (strOp == ".$") {
            node_ptr_t dataNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Access);
            setNodeTokenRangeByContext(dataNode, context);
            Reference ref(context->children[i + 1]->getText());
            node_ptr_t rhsNode = createNodeAs<RefDataLoad>(ref);
            *dataNode << lhsNode << rhsNode;
            lhsNode = dataNode;
        } else {
            throw std::runtime_error("Invalid access operator: " + strOp);
        }
    }
    LEAVE("AccessExpr");
    return lhsNode;
}

/*
dictData
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct of dict is immutable
    ;
*/
any Constructor::visitDictData(OpenCMLParser::DictDataContext *context) {
    ENTER("DictData");
    node_ptr_t dataNode = createNodeAs<DictDataLoad>();
    setNodeTokenRangeByContext(dataNode, context);
    if (context->pairedValues()) {
        *dataNode << any2node(visitPairedValues(context->pairedValues()));
    } else {
        *dataNode << createNodeAs<RepeatedLoad>("NamedData");
    }
    LEAVE("DictData");
    return dataNode;
}

/*
listData
    : '[' ((indexValues ','?) | dataExpr FOR identRef IN dataExpr (IF dataExpr)?)? ']'
    ;
*/
any Constructor::visitListData(OpenCMLParser::ListDataContext *context) {
    ENTER("ListData");
    node_ptr_t dataNode = createNodeAs<ListDataLoad>();
    setNodeTokenRangeByContext(dataNode, context);
    if (context->indexValues()) {
        *dataNode << any2node(visitIndexValues(context->indexValues()));
    } else {
        *dataNode << createNodeAs<RepeatedLoad>("Data");
        // TODO: Handle list comprehension
    }
    LEAVE("ListData");
    return dataNode;
}

/*
tupleData
    : '(' dataList? ','? ')'
    ;
*/
any Constructor::visitTupleData(OpenCMLParser::TupleDataContext *context) {
    ENTER("TupleData");
    node_ptr_t dataNode = createNodeAs<TupleDataLoad>();
    setNodeTokenRangeByContext(dataNode, context);
    if (context->dataList()) {
        *dataNode << any2node(visitDataList(context->dataList()));
    } else {
        *dataNode << createNodeAs<RepeatedLoad>("Data");
    }
    LEAVE("TupleData");
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
    ENTER("PrimaryData");
    node_ptr_t res;
    if (context->identRef()) {
        Reference ref(context->identRef()->getText());
        res = createNodeAs<RefDataLoad>(ref);
        setNodeTokenRangeByContext(res, context->identRef());
    } else if (context->literal()) {
        Literal literal = any_cast<Literal>(visitLiteral(context->literal()));
        res = createNodeAs<LiteralLoad>(literal);
        setNodeTokenRangeByContext(res, context->literal());
    } else if (context->dataExpr()) {
        res = any2node(visitDataExpr(context->dataExpr()));
    } else {
        res = any2node(visit(context->children[0]));
    }
    LEAVE("PrimaryData");
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
    ENTER("Literal: " + std::to_string(context->getAltNumber()));
    LiteralType type;
    string data = context->getText();
    if (context->INTEGER()) {
        type = LiteralType::Integer;
    } else if (context->REAL()) {
        type = LiteralType::Real;
    } else if (context->STRING()|| context->FSTRING()) {
        data = data.substr(1, data.size() - 2); // Remove quotes
        type = LiteralType::String;
    } else if (context->MULTI_STR()) {
        data = data.substr(3, data.size() - 6); // Remove triple quotes
        type = LiteralType::String;
    } else if (context->TRUE() || context->FALSE()) {
        type = LiteralType::Boolean;
    } else if (context->NULL_()) {
        type = LiteralType::Null;
    } else {
        throw std::runtime_error("Unknown literal type in visitLiteral");
    }
    Literal literal(type, data);
    LEAVE("Literal: " + std::to_string(context->getAltNumber()));
    return literal;
}

/*
typeExpr
    : unionType ('?' unionType?)?
    ;
*/
any Constructor::visitTypeExpr(OpenCMLParser::TypeExprContext *context) {
    ENTER("TypeExpr");
    node_ptr_t lhsNode = any2node(visitUnionType(context->unionType(0)));
    if (context->children.size() > 1) {
        if (context->unionType().size() > 1) {
            node_ptr_t typeExprNode = createNodeAs<TypeExprLoad>(TypeOp::ErrorThen);
            setNodeTokenRangeByContext(typeExprNode, context->unionType(1));
            *typeExprNode << any2node(visitUnionType(context->unionType(1)));
            lhsNode = typeExprNode;
        } else {
            node_ptr_t nullableTypeNode = createNodeAs<NullableTypeLoad>();
            setNodeTokenRangeByContext(nullableTypeNode, context);
            *nullableTypeNode << lhsNode;
            lhsNode = nullableTypeNode;
        }
    }
    LEAVE("TypeExpr");
    return lhsNode;
}

/*
unionType
    : interType ('|' interType)*
    ;
*/
any Constructor::visitUnionType(OpenCMLParser::UnionTypeContext *context) {
    ENTER("UnionType");
    node_ptr_t lhsNode = any2node(visitInterType(context->interType(0)));
    for (size_t i = 1; i < context->interType().size(); ++i) {
        node_ptr_t rhsNode = any2node(visitInterType(context->interType(i)));
        node_ptr_t typeExprNode = createNodeAs<TypeExprLoad>(TypeOp::Union);
        setNodeTokenRangeByContext(typeExprNode, context->interType(i));
        *typeExprNode << lhsNode << rhsNode;
        lhsNode = typeExprNode;
    }
    LEAVE("UnionType");
    return lhsNode;
}

/*
interType
    : diffType ('&' diffType)*
    ;
*/
any Constructor::visitInterType(OpenCMLParser::InterTypeContext *context) {
    ENTER("InterType");
    node_ptr_t lhsNode = any2node(visitDiffType(context->diffType(0)));
    for (size_t i = 1; i < context->diffType().size(); ++i) {
        node_ptr_t rhsNode = any2node(visitDiffType(context->diffType(i)));
        node_ptr_t typeExprNode = createNodeAs<TypeExprLoad>(TypeOp::Inter);
        setNodeTokenRangeByContext(typeExprNode, context->diffType(i));
        *typeExprNode << lhsNode << rhsNode;
        lhsNode = typeExprNode;
    }
    LEAVE("InterType");
    return lhsNode;
}

/*
diffType
    : keyUnionDiffType ('\\' keyUnionDiffType)*
    ;
*/
any Constructor::visitDiffType(OpenCMLParser::DiffTypeContext *context) {
    ENTER("DiffType");
    node_ptr_t lhsNode = any2node(visitKeyUnionDiffType(context->keyUnionDiffType(0)));
    for (size_t i = 1; i < context->keyUnionDiffType().size(); ++i) {
        node_ptr_t rhsNode = any2node(visitKeyUnionDiffType(context->keyUnionDiffType(i)));
        node_ptr_t typeExprNode = createNodeAs<TypeExprLoad>(TypeOp::Diff);
        setNodeTokenRangeByContext(typeExprNode, context->keyUnionDiffType(i));
        *typeExprNode << lhsNode << rhsNode;
        lhsNode = typeExprNode;
    }
    LEAVE("DiffType");
    return lhsNode;
}

/*
keyUnionDiffType
    : keyInterType (('+' | '-') keyInterType)*
    ;
*/
any Constructor::visitKeyUnionDiffType(OpenCMLParser::KeyUnionDiffTypeContext *context) {
    ENTER("KeyUnionDiffType");
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
        node_ptr_t typeExprNode = createNodeAs<TypeExprLoad>(op);
        setNodeTokenRangeByContext(typeExprNode, context->keyInterType(i));
        *typeExprNode << lhsNode << rhsNode;
        lhsNode = typeExprNode;
    }
    LEAVE("KeyUnionDiffType");
    return lhsNode;
}

/*
keyInterType
    : typeUnit ('^' typeUnit)*
    ;
*/
any Constructor::visitKeyInterType(OpenCMLParser::KeyInterTypeContext *context) {
    ENTER("KeyInterType");
    node_ptr_t lhsNode = any2node(visitTypeUnit(context->typeUnit(0)));
    for (size_t i = 1; i < context->typeUnit().size(); ++i) {
        node_ptr_t rhsNode = any2node(visitTypeUnit(context->typeUnit(i)));
        node_ptr_t typeExprNode = createNodeAs<TypeExprLoad>(TypeOp::KeyInter);
        setNodeTokenRangeByContext(typeExprNode, context->typeUnit(i));
        *typeExprNode << lhsNode << rhsNode;
        lhsNode = typeExprNode;
    }
    LEAVE("KeyInterType");
    return lhsNode;
}

/*
typeUnit : (identDef OF)? listType ;
*/
any Constructor::visitTypeUnit(OpenCMLParser::TypeUnitContext *context) {
    ENTER("TypeUnit");
    node_ptr_t res = nullptr;
    if (context->identDef()) {
        throw std::runtime_error("visitTypeUnit: identDef is not implemented yet");
    } else if (context->listType()) {
        res = any2node(visitListType(context->listType()));
    }
    LEAVE("TypeUnit");
    return res;
}

/*
listType
    : specType ('[' ']')*
    ;
*/
any Constructor::visitListType(OpenCMLParser::ListTypeContext *context) {
    ENTER("ListType");
    node_ptr_t res = any2node(visitSpecType(context->specType()));
    if (context->children.size() > 1) {
        size_t dims = (context->children.size() - 1) / 2;
        node_ptr_t listTypeNode = createNodeAs<ListTypeLoad>(dims);
        setNodeTokenRangeByContext(listTypeNode, context);
        *listTypeNode << res;
        res = listTypeNode;
    }
    LEAVE("ListType");
    return res;
}

/*
typeOrData : typeExpr | CONST dataExpr ;
*/
any Constructor::visitTypeOrData(OpenCMLParser::TypeOrDataContext *context) {
    ENTER("TypeOrData");
    node_ptr_t res = nullptr;
    if (context->typeExpr()) {
        res = any2node(visitTypeExpr(context->typeExpr()));
    } else if (context->dataExpr()) {
        res = any2node(visitDataExpr(context->dataExpr()));
    } else {
        throw std::runtime_error("Invalid TypeOrData context");
    }
    LEAVE("TypeOrData");
    return res;
}

/*
specType
    : primaryType ('<' typeOrData (',' typeOrData)* '>')?
    ;
*/
any Constructor::visitSpecType(OpenCMLParser::SpecTypeContext *context) {
    ENTER("SpecType");
    node_ptr_t res = any2node(visitPrimaryType(context->primaryType()));
    node_ptr_t specTypeNode = createNodeAs<TypeExprLoad>(TypeOp::Specialize);
    setNodeTokenRangeByContext(specTypeNode, context);
    if (context->typeOrData().size() > 0) {
        node_ptr_t repeatNode = createNodeAs<RepeatedLoad>("TypeOrData");
        setNodeTokenRangeByContexts(repeatNode, context->typeOrData());
        for (auto &typeOrData : context->typeOrData()) {
            *repeatNode << any2node(visitTypeOrData(typeOrData));
        }
        *specTypeNode << res << repeatNode;
        res = specTypeNode;
    }
    LEAVE("SpecType");
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
    ENTER("PrimaryType");
    node_ptr_t res = nullptr;
    if (context->INNER_ATOM_TYPE()) {
        Reference ref(context->INNER_ATOM_TYPE()->getText());
        res = createNodeAs<RefTypeLoad>(ref);
        setNodeTokenRangeByContext(res, context);
    } else if (context->dictType()) {
        res = any2node(visitDictType(context->dictType()));
    } else if (context->identRef()) {
        res = createNodeAs<RefTypeLoad>(any_cast<Reference>(visitIdentRef(context->identRef())));
        setNodeTokenRangeByContext(res, context->identRef());
    } else if (context->typeExpr()) {
        res = any2node(visitTypeExpr(context->typeExpr()));
    } else if (context->tupleType()) {
        res = any2node(visitTupleType(context->tupleType()));
    } else if (context->funcType()) {
        res = any2node(visitFuncType(context->funcType()));
    } else if (context->dataExpr()) {
        res = createNodeAs<TypeExprLoad>(TypeOp::TypeOf);
        setNodeTokenRangeByContext(res, context);
        *res << any2node(visitDataExpr(context->dataExpr()));
    } else if (context->identDef()) {
        res = createNodeAs<TypeExprLoad>(TypeOp::TypeAs);
        setNodeTokenRangeByContext(res, context);
        Reference ref(context->identDef()->getText());
        *res << createNodeAs<RefTypeLoad>(ref);
        setNodeTokenRangeByContext(res, context->identDef());
    }
    LEAVE("PrimaryType");
    return res;
}

/*
dictType
    : '{' (keyTypePair (',' keyTypePair)*)? ','? '}'
    ;
*/
any Constructor::visitDictType(OpenCMLParser::DictTypeContext *context) {
    ENTER("DictType");
    node_ptr_t repeatNode = createNodeAs<RepeatedLoad>("NamedPair");
    setNodeTokenRangeByContext(repeatNode, context);
    for (auto &pair : context->keyTypePair()) {
        node_ptr_t pairNode = any2node(visitKeyTypePair(pair));
        *repeatNode << pairNode;
    }
    LEAVE("DictType");
    return repeatNode;
}

/*
typeList
    : typeExpr (',' typeExpr)*
    ;
*/
any Constructor::visitTypeList(OpenCMLParser::TypeListContext *context) {
    ENTER("TypeList");
    node_ptr_t repeatNode = createNodeAs<RepeatedLoad>("Type");
    setNodeTokenRangeByContext(repeatNode, context);
    for (auto &typeExpr : context->typeExpr()) {
        node_ptr_t typeExprNode = any2node(visitTypeExpr(typeExpr));
        *repeatNode << typeExprNode;
    }
    LEAVE("TypeList");
    return repeatNode;
}

/*
tupleType
    : '(' typeList? ','? ')'
    ;
*/
any Constructor::visitTupleType(OpenCMLParser::TupleTypeContext *context) {
    ENTER("TupleType");
    node_ptr_t res = createNodeAs<TupleTypeLoad>();
    setNodeTokenRangeByContext(res, context);
    if (context->typeList()) {
        node_ptr_t repeatNode = any2node(visitTypeList(context->typeList()));
        *res << repeatNode;
    } else {
        *res << createNodeAs<RepeatedLoad>("Type");
    }
    LEAVE("TupleType");
    return res;
}

/*
funcType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;
*/
any Constructor::visitFuncType(OpenCMLParser::FuncTypeContext *context) {
    ENTER("FuncType");

    node_ptr_t funcTypeNode = createNodeAs<FuncTypeLoad>();
    setNodeTokenRangeByContext(funcTypeNode, context);
    auto funcType = unwrapNodeAs<FuncTypeLoad>(funcTypeNode);

    auto modifier = context->modifiers();
    ModifierSet modSet;
    if (modifier) {
        if (modifier->ATOMIC().size() > 0) {
            modSet.insert(Modifier::Atomic);
        }
        if (modifier->SHARED().size() > 0) {
            modSet.insert(Modifier::Shared);
        }
        if (modifier->SYNC().size() > 0) {
            modSet.insert(Modifier::Sync);
        }
        if (modifier->MACRO().size() > 0) {
            modSet.insert(Modifier::Macro);
        }
    }
    funcType->setModifiers(modSet);

    if (context->angledParams()) {
        node_ptr_t withParams = any2node(visitAngledParams(context->angledParams()));
        *funcTypeNode << withParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    if (context->parentParams()) {
        node_ptr_t normParams = any2node(visitParentParams(context->parentParams()));
        *funcTypeNode << normParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    node_ptr_t typeOptNode = createNodeAs<OptionalLoad>("Type");
    if (context->typeExpr()) {
        setNodeTokenRangeByContext(typeOptNode, context->typeExpr());
        setNodeTokenRangeByContext(typeOptNode, context->typeExpr());
        node_ptr_t typeNode = any2node(visitTypeExpr(context->typeExpr()));
        *typeOptNode << typeNode;
    }
    *funcTypeNode << typeOptNode;

    LEAVE("FuncType");
    return funcTypeNode;
}

/*
identDef : IDENTIFIER ;
*/
any Constructor::visitIdentDef(OpenCMLParser::IdentDefContext *context) {
    ENTER("IdentDef");
    Reference ref(context->IDENTIFIER()->getText());
    LEAVE("IdentDef");
    return ref;
}

/*
identRef : (IDENTIFIER '::')* IDENTIFIER ;
*/
any Constructor::visitIdentRef(OpenCMLParser::IdentRefContext *context) {
    ENTER("IdentRef");
    Reference ref(context->getText());
    LEAVE("IdentRef");
    return ref;
}
