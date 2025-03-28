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
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "antlr/OpenCMLLexer.h"

#include "common/token.h"
#include "gct.h"
#include "utils/log.h"

#define DEBUG_LEVEL 0

using namespace std;
using namespace GCT;

template <typename LoadType, typename... Args> node_ptr_t createNode(Args &&...args) {
    return std::make_shared<Node>(std::make_shared<LoadType>(std::forward<Args>(args)...));
}

template <typename DataType, typename... Args> node_ptr_t createDataNode(Args &&...args) {
    return createNode<DataLoad>(std::make_shared<DataType>(std::forward<Args>(args)...));
}

namespace GraphConstructTree::InnerFuncDRefNodes {
node_ptr_t __copy__ = nullptr;
node_ptr_t __cast__ = nullptr;
node_ptr_t __type__ = nullptr;
node_ptr_t __index__ = nullptr;

node_ptr_t __as__ = nullptr;
node_ptr_t __is__ = nullptr;

node_ptr_t __add__ = nullptr;
node_ptr_t __sub__ = nullptr;
node_ptr_t __mul__ = nullptr;
node_ptr_t __div__ = nullptr;
node_ptr_t __mod__ = nullptr;
node_ptr_t __pow__ = nullptr;
node_ptr_t __inter__ = nullptr;
node_ptr_t __union__ = nullptr;

node_ptr_t __assn__ = nullptr;
node_ptr_t __assn_add__ = nullptr;
node_ptr_t __assn_sub__ = nullptr;
node_ptr_t __assn_mul__ = nullptr;
node_ptr_t __assn_div__ = nullptr;
node_ptr_t __assn_mod__ = nullptr;
node_ptr_t __assn_pow__ = nullptr;
node_ptr_t __assn_inter__ = nullptr;
node_ptr_t __assn_union__ = nullptr;

node_ptr_t __lt__ = nullptr;
node_ptr_t __gt__ = nullptr;
node_ptr_t __le__ = nullptr;
node_ptr_t __ge__ = nullptr;
node_ptr_t __eq__ = nullptr;
node_ptr_t __ne__ = nullptr;
node_ptr_t __and__ = nullptr;
node_ptr_t __or__ = nullptr;

node_ptr_t __not__ = nullptr;
node_ptr_t __neg__ = nullptr;
node_ptr_t __rev__ = nullptr;

node_ptr_t __ifexpr__ = nullptr;

unordered_map<string, node_ptr_t> nodesMap;
unordered_map<string, node_ptr_t> opNodesMap;

void init() {
    if (__copy__)
        return;

    __copy__ = createNode<DRefLoad>("__copy__");
    nodesMap["__copy__"] = __copy__;
    __cast__ = createNode<DRefLoad>("__cast__");
    nodesMap["__cast__"] = __cast__;
    __type__ = createNode<DRefLoad>("__type__");
    nodesMap["__type__"] = __type__;
    __index__ = createNode<DRefLoad>("__index__");
    nodesMap["__index__"] = __index__;

    __as__ = createNode<DRefLoad>("__as__");
    nodesMap["__as__"] = __as__;
    opNodesMap["as"] = __as__;
    __is__ = createNode<DRefLoad>("__is__");
    nodesMap["__is__"] = __is__;
    opNodesMap["is"] = __is__;

    __add__ = createNode<DRefLoad>("__add__");
    nodesMap["__add__"] = __add__;
    opNodesMap["+"] = __add__;
    __sub__ = createNode<DRefLoad>("__sub__");
    nodesMap["__sub__"] = __sub__;
    opNodesMap["-"] = __sub__;
    __mul__ = createNode<DRefLoad>("__mul__");
    nodesMap["__mul__"] = __mul__;
    opNodesMap["*"] = __mul__;
    __div__ = createNode<DRefLoad>("__div__");
    nodesMap["__div__"] = __div__;
    opNodesMap["/"] = __div__;
    __mod__ = createNode<DRefLoad>("__mod__");
    nodesMap["__mod__"] = __mod__;
    opNodesMap["%"] = __mod__;
    __pow__ = createNode<DRefLoad>("__pow__");
    nodesMap["__pow__"] = __pow__;
    opNodesMap["^"] = __pow__;
    __inter__ = createNode<DRefLoad>("__inter__");
    nodesMap["__inter__"] = __inter__;
    opNodesMap["&"] = __inter__;
    __union__ = createNode<DRefLoad>("__union__");
    nodesMap["__union__"] = __union__;
    opNodesMap["|"] = __union__;

    __assn__ = createNode<DRefLoad>("__assn__");
    nodesMap["__assn__"] = __assn__;
    opNodesMap["="] = __assn__;
    __assn_add__ = createNode<DRefLoad>("__assn_add__");
    nodesMap["__assn_add__"] = __assn_add__;
    opNodesMap["+="] = __assn_add__;
    __assn_sub__ = createNode<DRefLoad>("__assn_sub__");
    nodesMap["__assn_sub__"] = __assn_sub__;
    opNodesMap["-="] = __assn_sub__;
    __assn_mul__ = createNode<DRefLoad>("__assn_mul__");
    nodesMap["__assn_mul__"] = __assn_mul__;
    opNodesMap["*="] = __assn_mul__;
    __assn_div__ = createNode<DRefLoad>("__assn_div__");
    nodesMap["__assn_div__"] = __assn_div__;
    opNodesMap["/="] = __assn_div__;
    __assn_mod__ = createNode<DRefLoad>("__assn_mod__");
    nodesMap["__assn_mod__"] = __assn_mod__;
    opNodesMap["%="] = __assn_mod__;
    __assn_pow__ = createNode<DRefLoad>("__assn_pow__");
    nodesMap["__assn_pow__"] = __assn_pow__;
    opNodesMap["^="] = __assn_pow__;
    __assn_inter__ = createNode<DRefLoad>("__assn_inter__");
    nodesMap["__assn_inter__"] = __assn_inter__;
    opNodesMap["&="] = __assn_inter__;
    __assn_union__ = createNode<DRefLoad>("__assn_union__");
    nodesMap["__assn_union__"] = __assn_union__;
    opNodesMap["|="] = __assn_union__;

    __lt__ = createNode<DRefLoad>("__lt__");
    nodesMap["__lt__"] = __lt__;
    opNodesMap["<"] = __lt__;
    __gt__ = createNode<DRefLoad>("__gt__");
    nodesMap["__gt__"] = __gt__;
    opNodesMap[">"] = __gt__;
    __le__ = createNode<DRefLoad>("__le__");
    nodesMap["__le__"] = __le__;
    opNodesMap["<="] = __le__;
    __ge__ = createNode<DRefLoad>("__ge__");
    nodesMap["__ge__"] = __ge__;
    opNodesMap[">="] = __ge__;
    __eq__ = createNode<DRefLoad>("__eq__");
    nodesMap["__eq__"] = __eq__;
    opNodesMap["=="] = __eq__;
    __ne__ = createNode<DRefLoad>("__ne__");
    nodesMap["__ne__"] = __ne__;
    opNodesMap["!="] = __ne__;
    __and__ = createNode<DRefLoad>("__and__");
    nodesMap["__and__"] = __and__;
    opNodesMap["&&"] = __and__;
    __or__ = createNode<DRefLoad>("__or__");
    nodesMap["__or__"] = __or__;
    opNodesMap["||"] = __or__;

    __not__ = createNode<DRefLoad>("__not__");
    nodesMap["__not__"] = __not__;
    opNodesMap["!"] = __not__;
    __neg__ = createNode<DRefLoad>("__neg__");
    nodesMap["__neg__"] = __neg__;
    opNodesMap["-"] = __neg__;
    __rev__ = createNode<DRefLoad>("__rev__");
    nodesMap["__rev__"] = __rev__;
    opNodesMap["~"] = __rev__;
}

} // namespace GraphConstructTree::InnerFuncDRefNodes

inline node_ptr_t reparent(node_ptr_t &node, node_ptr_t &parent) {
    *parent << node;
    return parent;
}

inline node_ptr_t linkFunc(node_ptr_t &argsNode, node_ptr_t &funcNode) {
    node_ptr_t linkNode = createNode<LinkLoad>();
    *linkNode << argsNode << funcNode;
    return linkNode;
}

data_ptr_t Constructor::extractStaticData(const node_ptr_t &node) {
    if (node->type() == NodeType::DATA) {
        const auto dataNode = dynamic_pointer_cast<DataLoad>(node->load());
        return dataNode->data();
    } else if (node->type() == NodeType::DREF) {
        const auto refNode = dynamic_pointer_cast<DRefLoad>(node->load());
        return make_shared<RefData>(refNode->ident());
    } else {
        return nullptr;
    }
}

pair<node_ptr_t, data_ptr_t> Constructor::makeRefData(const node_ptr_t &expr) {
    const string indent = to_string(indentIndex_++);
    node_ptr_t refNode = createNode<NRefLoad>(indent);
    *refNode << expr;
    data_ptr_t data = make_shared<RefData>(indent);
    return make_pair(refNode, data);
}

pair<data_ptr_t, bool> Constructor::extractData(const node_ptr_t &node, node_ptr_t &execNode) {
    const data_ptr_t data = extractStaticData(node);
    if (data) {
        return make_pair(data, false);
    } else {
        auto [refNode, refData] = makeRefData(node);
        *execNode << refNode;
        return make_pair(refData, true);
    }
}

pair<data_ptr_t, bool> Constructor::extractData(const node_ptr_t &node, node_ptr_t &execNode, bool &dangling) {
    auto [refData, dang] = extractData(node, execNode);
    if (dang) {
        dangling = true;
    }
    return make_pair(refData, dang);
}

/*
program : SEP? (decl SEP?)* EOF;
*/
any Constructor::visitProgram(OpenCMLParser::ProgramContext *context) {
    enter("Program");

    root_ = createNode<ExecLoad>();

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
any Constructor::visitDecl(OpenCMLParser::DeclContext *context) { return nullptr; }

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
    pushScope();

    node_ptr_t execNode = createNode<ExecLoad>();

    vector<OpenCMLParser::UseDeclContext *> froms;
    vector<OpenCMLParser::TypeDeclContext *> types;
    vector<OpenCMLParser::FuncDeclContext *> decls;
    vector<OpenCMLParser::StmtContext *> stmts;

    // 1. Classify each statement into the appropriate category. Sequence: Decl > Type > Def >
    for (const auto &stmt : context->stmt()) {
        if (stmt->useDecl()) {
            froms.push_back(stmt->useDecl());
        } else if (stmt->typeDecl()) {
            types.push_back(stmt->typeDecl());
        } else if (stmt->funcDecl()) {
            decls.push_back(stmt->funcDecl());
            stmts.push_back(stmt);
        } else {
            stmts.push_back(stmt);
        }
    }

    // 2. Process statements in priority order
    // 2.1 Handle imports first (may affect type resolution)
    for (const auto &stmt : froms) {
        *execNode << any_cast<node_ptr_t>(visitUseDecl(stmt));
    }

    // 2.2 Process type declarations (prerequisite for functions)
    for (const auto &stmt : types) {
        *execNode << any_cast<node_ptr_t>(visitTypeDecl(stmt));
    }

    // 2.3 Register function signatures (before their bodies are processed)
    for (const auto &decl : decls) {
        func_type_ptr_t funcType = any_cast<func_type_ptr_t>(visitFuncDecl(decl));
        node_ptr_t declNode = createNode<DeclLoad>(funcType); // Create declaration node
        *execNode << declNode;                                // Attach to execution block
    }

    // 2.4 Process general statements and function bodies
    for (const auto &stmt : stmts) {
        *execNode << any_cast<node_ptr_t>(visitStmt(stmt));
    }

    popScope();
    leave("StmtList");
    return execNode;
}

/*
moduleDecl : MODULE identDef ;
*/
any Constructor::visitModuleDecl(OpenCMLParser::ModuleDeclContext *context) { return nullptr; }

/*
importDecl : IMPORT (STRING | (identDef | bracedIdents) FROM STRING) ;
*/
any Constructor::visitImportDecl(OpenCMLParser::ImportDeclContext *context) { return nullptr; }

/*
exportDecl : EXPORT (letDecl | typeDecl | bracedIdents) ;
*/
any Constructor::visitExportDecl(OpenCMLParser::ExportDeclContext *context) { return nullptr; }

/*
stmtBlock  : SYNC? '{' stmtList? '}' ;
*/
any Constructor::visitStmtBlock(OpenCMLParser::StmtBlockContext *context) { return nullptr; }

/*
blockExpr  : stmtBlock | waitExpr ;
*/
any Constructor::visitBlockExpr(OpenCMLParser::BlockExprContext *context) { return nullptr; }

/*
waitStmt   : WAIT (stmtBlock | dataList) ;
*/
any Constructor::visitWaitStmt(OpenCMLParser::WaitStmtContext *context) { return nullptr; }

/*
lambdaExpr : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr ;
*/
any Constructor::visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) { return nullptr; }

/*
funcDecl   : (WITH angledParams)? EXPORT? implMark? modifiers? FUNC identDef parentParams (':' typeExpr)? stmtBlock ;
*/
any Constructor::visitFuncDecl(OpenCMLParser::FuncDeclContext *context) { return nullptr; }

/*
parentIdents  : '(' identList? ','? ')' ;
*/
any Constructor::visitParentIdents(OpenCMLParser::ParentIdentsContext *context) { return nullptr; }

/*
bracedIdents  : '{' identList? ','? '}' ;
*/
any Constructor::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) { return nullptr; }

/*
bracketIdents : '[' identList? ','? ']' ;
*/
any Constructor::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) { return nullptr; }

/*
carrier       : identList | parentIdents | bracedIdents | bracketIdents ;
*/
any Constructor::visitCarrier(OpenCMLParser::CarrierContext *context) { return nullptr; }

/*
letDecl    : (LET | VAR) carrier (':' typeList)? '=' valueList ;
*/
any Constructor::visitLetDecl(OpenCMLParser::LetDeclContext *context) { return nullptr; }

/*
useDecl    : USE (identDef '=')? identRef ;
*/
any Constructor::visitUseDecl(OpenCMLParser::UseDeclContext *context) { return nullptr; }

/*
retStmt    : (RETURN | RAISE | THROW) valueList ;
*/
any Constructor::visitRetStmt(OpenCMLParser::RetStmtContext *context) { return nullptr; }

/*
typeDecl   : implMark? TYPE identDef '=' (typeExpr | STRING) ;
*/
any Constructor::visitTypeDecl(OpenCMLParser::TypeDeclContext *context) { return nullptr; }

/*
enumDecl   : ENUM identDef (OF typeExpr)? '=' '{' pairedValues ','? '}' ;
*/
any Constructor::visitEnumDecl(OpenCMLParser::EnumDeclContext *context) { return nullptr; }

/*
implMark    : INNER | OUTER ;
*/
any Constructor::visitImplMark(OpenCMLParser::ImplMarkContext *context) { return nullptr; }

/*
modifiers   : (ATOMIC | SHARED | SYNC | MACRO)+ ;
*/
any Constructor::visitModifiers(OpenCMLParser::ModifiersContext *context) { return nullptr; }

/*
indexValue   : '...'? waitExpr ;
*/
any Constructor::visitIndexValue(OpenCMLParser::IndexValueContext *context) { return nullptr; }

/*
keyTypePair  : identDef ':' typeExpr ;
*/
any Constructor::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) { return nullptr; }

/*
keyValuePair : identDef ':' waitExpr | '...' waitExpr ;
*/
any Constructor::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) { return nullptr; }

/*
keyParamPair : VAR? identDef ':' typeExpr ('=' waitExpr)? ;
*/
any Constructor::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) { return nullptr; }

/*
dataList     : dataExpr (',' dataExpr)* ;
*/
any Constructor::visitDataList(OpenCMLParser::DataListContext *context) { return nullptr; }

/*
identList    : identDef (',' identDef)* ;
*/
any Constructor::visitIdentList(OpenCMLParser::IdentListContext *context) { return nullptr; }

/*
valueList    : waitExpr (',' waitExpr)* ;
*/
any Constructor::visitValueList(OpenCMLParser::ValueListContext *context) { return nullptr; }

/*
indexValues  : indexValue (',' indexValue)* ;
*/
any Constructor::visitIndexValues(OpenCMLParser::IndexValuesContext *context) { return nullptr; }

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
*/
any Constructor::visitPairedValues(OpenCMLParser::PairedValuesContext *context) { return nullptr; }

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
any Constructor::visitPairedParams(OpenCMLParser::PairedParamsContext *context) { return nullptr; }

/*
argumentList : indexValues (',' pairedValues)? | pairedValues ;
*/
any Constructor::visitArgumentList(OpenCMLParser::ArgumentListContext *context) { return nullptr; }

/*
memberAccess : '[' waitExpr (':' waitExpr (':' waitExpr)?)? ']' ;
*/
any Constructor::visitMemberAccess(OpenCMLParser::MemberAccessContext *context) { return nullptr; }

/*
parentParams : '(' pairedParams? ','? ')' ;
*/
any Constructor::visitParentParams(OpenCMLParser::ParentParamsContext *context) { return nullptr; }

/*
parentArgues : '(' argumentList? ','? ')' ;
*/
any Constructor::visitParentArgues(OpenCMLParser::ParentArguesContext *context) { return nullptr; }

/*
angledParams : '<' pairedParams? ','? '>' ;
*/
any Constructor::visitAngledParams(OpenCMLParser::AngledParamsContext *context) { return nullptr; }

/*
angledValues : '<' argumentList? ','? '>' ;
*/
any Constructor::visitAngledValues(OpenCMLParser::AngledValuesContext *context) { return nullptr; }

/*
waitExpr : WAIT? dataExpr ;
*/
any Constructor::visitWaitExpr(OpenCMLParser::WaitExprContext *context) { return nullptr; }

/*
pattern
    : identRef
    | literal
    | '(' (valueList | identList)? ','? ')'
    | '{' (pairedValues | identList)? ','? '}'
    | '_' // wildcard
    ;
*/
any Constructor::visitPattern(OpenCMLParser::PatternContext *context) { return nullptr; }

/*
matchCase
    : CASE pattern ('|' pattern)* '=>' blockExpr
    ;
*/
any Constructor::visitMatchCase(OpenCMLParser::MatchCaseContext *context) { return nullptr; }

/*
catchClause
    : CATCH identDef ':' typeExpr stmtBlock
    ;
*/
any Constructor::visitCatchClause(OpenCMLParser::CatchClauseContext *context) { return nullptr; }

/*
ctrlExpr
    : IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
    | MATCH identRef '{' matchCase+ '}'
    | TRY stmtBlock catchClause+ (FINALLY stmtBlock)?
    ;
*/
any Constructor::visitCtrlExpr(OpenCMLParser::CtrlExprContext *context) { return nullptr; }

/*
dataExpr
    : assignExpr
    | ctrlExpr
    ;
*/
any Constructor::visitDataExpr(OpenCMLParser::DataExprContext *context) { return nullptr; }

/*
assignExpr
    : logicalOrExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=') logicalOrExpr)?
    ;
*/
any Constructor::visitAssignExpr(OpenCMLParser::AssignExprContext *context) { return nullptr; }

/*
logicalOrExpr
    : logicalAndExpr ('||' logicalAndExpr)*
    ;
*/
any Constructor::visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) { return nullptr; }

/*
logicalAndExpr
    : equalityExpr ('&&' equalityExpr)*
    ;
*/
any Constructor::visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) { return nullptr; }

/*
equalityExpr
    : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
    ;
*/
any Constructor::visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) { return nullptr; }

/*
relationalExpr
    : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
    ;
*/
any Constructor::visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) { return nullptr; }

/*
additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;
*/
any Constructor::visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) { return nullptr; }

/*
multiplicativeExpr
    : nullableExpr (('*' | '/' | '^' | '@' | '%') nullableExpr)*
    ;
*/
any Constructor::visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) { return nullptr; }

/*
nullableExpr
    : unaryExpr (('??' | '!!') waitExpr)?
    ;
*/
any Constructor::visitNullableExpr(OpenCMLParser::NullableExprContext *context) { return nullptr; }

/*
unaryExpr
    : linkExpr ((AS | IS) typeExpr)?
    | ('!' | '-' | '~') linkExpr
    ;
*/
any Constructor::visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) { return nullptr; }

/*
linkExpr
    : bindExpr (('->' | '?->') bindExpr)*
    ;
*/
any Constructor::visitLinkExpr(OpenCMLParser::LinkExprContext *context) { return nullptr; }

/*
bindExpr
    : withExpr (('..' | '?..') withExpr)*
    ;
*/
any Constructor::visitBindExpr(OpenCMLParser::BindExprContext *context) { return nullptr; }

/*
withExpr
    : annoExpr (('.' | '?.') annoExpr)*
    ;
*/
any Constructor::visitWithExpr(OpenCMLParser::WithExprContext *context) { return nullptr; }

/*
annoExpr
    : primaryData ({isAdjacent()}? (memberAccess | parentArgues | angledValues | '!'))*
    ;
*/
any Constructor::visitAnnoExpr(OpenCMLParser::AnnoExprContext *context) { return nullptr; }

/*
dictExpr
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct of dict is immutable
    ;
*/
any Constructor::visitDictExpr(OpenCMLParser::DictExprContext *context) { return nullptr; }

/*
listExpr
    : '[' ((indexValues ','?) | waitExpr FOR identRef IN waitExpr (IF waitExpr)?)? ']'
    ;
*/
any Constructor::visitListExpr(OpenCMLParser::ListExprContext *context) { return nullptr; }

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
any Constructor::visitPrimaryData(OpenCMLParser::PrimaryDataContext *context) { return nullptr; }

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
any Constructor::visitLiteral(OpenCMLParser::LiteralContext *context) { return nullptr; }

/*
typeExpr
    : unionType ('?' unionType?)?
    ;
*/
any Constructor::visitTypeExpr(OpenCMLParser::TypeExprContext *context) { return nullptr; }

/*
unionType
    : unionUnit ('|' unionUnit)*
    ;
*/
any Constructor::visitUnionType(OpenCMLParser::UnionTypeContext *context) { return nullptr; }

/*
unionUnit : (identDef OF)? listType ;
*/
any Constructor::visitUnionUnit(OpenCMLParser::UnionUnitContext *context) { return nullptr; }

/*
listType
    : argsType ('[' ']')*
    ;
*/
any Constructor::visitListType(OpenCMLParser::ListTypeContext *context) { return nullptr; }

/*
typeOrData : typeExpr | primaryData ;
*/
any Constructor::visitTypeOrData(OpenCMLParser::TypeOrDataContext *context) { return visit(context->children[0]); }

/*
argsType
    : primaryType ('<' typeOrData (',' typeOrData)* '>')?
    ;
*/
any Constructor::visitArgsType(OpenCMLParser::ArgsTypeContext *context) { return nullptr; }

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
any Constructor::visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) { return nullptr; }

/*
dictExprType
    : dictType (('&' | '^') dictType)*
    ;
*/
any Constructor::visitDictExprType(OpenCMLParser::DictExprTypeContext *context) { return nullptr; }

/*
dictType
    : '{' (keyTypePair (',' keyTypePair)*)? ','? '}'
    ;
*/
any Constructor::visitDictType(OpenCMLParser::DictTypeContext *context) { return nullptr; }

/*
typeList
    : typeExpr (',' typeExpr)*
    ;
*/
any Constructor::visitTypeList(OpenCMLParser::TypeListContext *context) { return nullptr; }

/*
tupleType
    : '(' typeList? ','? ')'
    ;
*/
any Constructor::visitTupleType(OpenCMLParser::TupleTypeContext *context) { return nullptr; }

/*
lambdaType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;
*/
any Constructor::visitLambdaType(OpenCMLParser::LambdaTypeContext *context) { return nullptr; }

/*
identDef : IDENTIFIER ;
*/
any Constructor::visitIdentDef(OpenCMLParser::IdentDefContext *context) { return nullptr; }

/*
identRef : (IDENTIFIER '::')* IDENTIFIER ;
*/
any Constructor::visitIdentRef(OpenCMLParser::IdentRefContext *context) { return nullptr; }
