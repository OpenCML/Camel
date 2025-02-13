/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Updated: Jan. 07, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "antlr/OpenCMLLexer.h"

#include "ast.h"
#include "common/token.h"
#include "utils/log.h"

#define DEBUG_LEVEL 0

using namespace std;
using namespace ast;

template <typename LoadType, typename... Args> node_ptr_t createNode(Args &&...args) {
    return std::make_shared<Node>(std::make_shared<LoadType>(std::forward<Args>(args)...));
}

template <typename DataType, typename... Args> node_ptr_t createDataNode(Args &&...args) {
    return createNode<DataLoad>(std::make_shared<DataType>(std::forward<Args>(args)...));
}

namespace AbstractSyntaxTree::InnerFuncDRefNodes {
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

} // namespace AbstractSyntaxTree::InnerFuncDRefNodes

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
program : SEP? ((decl | stmt) SEP?)* EOF;
*/
any Constructor::visitProgram(OpenCMLParser::ProgramContext *context) { return nullptr; }

/*
decl
    : moduleDecl
    | importDecl
    | exportDecl
    | funcDecl
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
    | exprStmt
    | blockStmt
    ;
*/
any Constructor::visitStmt(OpenCMLParser::StmtContext *context) { return nullptr; }

/*
stmtList : stmt (SEP? stmt)* SEP? ;
*/
any Constructor::visitStmtList(OpenCMLParser::StmtListContext *context) { return nullptr; }

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
blockExpr : stmtBlock | dataExpr ;
*/
any Constructor::visitBlockExpr(OpenCMLParser::BlockExprContext *context) { return nullptr; }

/*
blockStmt  : WAIT? stmtBlock ;
*/
any Constructor::visitBlockStmt(OpenCMLParser::BlockStmtContext *context) { return nullptr; }

/*
lambdaExpr : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr ;
*/
any Constructor::visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) { return nullptr; }

/*
funcDecl   : annotations? (WITH angledParams)? EXPORT? modifiers? FUNC identDef parentParams (':' typeExpr)? stmtBlock ;
*/
any Constructor::visitFuncDecl(OpenCMLParser::FuncDeclContext *context) { return nullptr; }

/*
parentIdents  : '(' identList? ','? ')' ;    // for tuple unpacking
*/
any Constructor::visitParentIdents(OpenCMLParser::ParentIdentsContext *context) { return nullptr; }

/*
bracedIdents  : '{' identList? ','? '}' ;    // for dict unpacking
*/
any Constructor::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) { return nullptr; }

/*
bracketIdents : '[' identList? ','? ']' ;    // for list unpacking
*/
any Constructor::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) { return nullptr; }

/*
carrier    : identDef | bracedIdents | bracketIdents ;
*/
any Constructor::visitCarrier(OpenCMLParser::CarrierContext *context) { return nullptr; }

/*
letDecl    : (LET | VAR) carrier (':' typeExpr)? '=' dataExpr ;
*/
any Constructor::visitLetDecl(OpenCMLParser::LetDeclContext *context) { return nullptr; }

/*
useDecl    : USE (identDef '=')? identRef ;
*/
any Constructor::visitUseDecl(OpenCMLParser::UseDeclContext *context) { return nullptr; }

/*
retStmt    : (RETURN | RAISE | THROW) dataExpr ;
*/
any Constructor::visitRetStmt(OpenCMLParser::RetStmtContext *context) { return nullptr; }

/*
typeDecl   : TYPE identDef '=' typeExpr ;
*/
any Constructor::visitTypeDecl(OpenCMLParser::TypeDeclContext *context) { return nullptr; }

/*
enumDecl   : ENUM identDef (OF typeExpr)? '=' '{' pairedValues ','? '}' ;
*/
any Constructor::visitEnumDecl(OpenCMLParser::EnumDeclContext *context) { return nullptr; }

/*
exprStmt   : annotations? dataExpr ;
*/
any Constructor::visitExprStmt(OpenCMLParser::ExprStmtContext *context) { return nullptr; }

/*
annotation  : '@' primaryData ;
*/
any Constructor::visitAnnotation(OpenCMLParser::AnnotationContext *context) { return nullptr; }

/*
annotations : annotation+ ;
*/
any Constructor::visitAnnotations(OpenCMLParser::AnnotationsContext *context) { return nullptr; }

/*
modifiers   : (INNER | OUTER | ATOMIC | SHARED | SYNC | MACRO)+ ;
*/
any Constructor::visitModifiers(OpenCMLParser::ModifiersContext *context) { return nullptr; }

/*
indexValue   : dataExpr | '...' dataExpr ;
*/
any Constructor::visitIndexValue(OpenCMLParser::IndexValueContext *context) { return nullptr; }

/*
keyTypePair  : identDef ':' typeExpr ;
*/
any Constructor::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) { return nullptr; }

/*
keyValuePair : identDef ':' dataExpr | '...' dataExpr ;
*/
any Constructor::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) { return nullptr; }

/*
keyParamPair : VAR? identDef annotation? ':' (typeExpr | TYPEAS identDef) ('=' dataExpr)? ;
*/
any Constructor::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) { return nullptr; }

/*
identList    : identDef (',' identDef)* ;
*/
any Constructor::visitIdentList(OpenCMLParser::IdentListContext *context) { return nullptr; }

/*
valueList    : dataExpr (',' dataExpr)* ;
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
memberAccess : '[' dataExpr (':' dataExpr (':' dataExpr)?)? ']' ;
*/
any Constructor::visitMemberAccess(OpenCMLParser::MemberAccessContext *context) { return nullptr; }

/*
parentParams : '(' pairedParams? ','? ')' ; // for functor parameters definition
*/
any Constructor::visitParentParams(OpenCMLParser::ParentParamsContext *context) { return nullptr; }

/*
parentArgues : '(' argumentList? ','? ')' ; // for functor arguments
*/
any Constructor::visitParentArgues(OpenCMLParser::ParentArguesContext *context) { return nullptr; }

/*
angledParams : '<' pairedParams? ','? '>' ; // for functor super parameters definition
*/
any Constructor::visitAngledParams(OpenCMLParser::AngledParamsContext *context) { return nullptr; }

/*
angledValues : '<' argumentList? ','? '>' ; // for functor super arguments
*/
any Constructor::visitAngledValues(OpenCMLParser::AngledValuesContext *context) { return nullptr; }

/*
dataExpr
    : WAIT? structExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '&=' | '|=') structExpr)?
    ;
*/
any Constructor::visitDataExpr(OpenCMLParser::DataExprContext *context) { return nullptr; }

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
structExpr
    : logicalOrExpr
    | IF logicalOrExpr THEN blockExpr ELSE blockExpr
    | MATCH identRef '{' matchCase+ '}'
    | TRY stmtBlock catchClause+ (FINALLY stmtBlock)?
    ;
*/
any Constructor::visitStructExpr(OpenCMLParser::StructExprContext *context) { return nullptr; }

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
    : nullableExpr (('^' | '*' | '/' | '%') nullableExpr)*
    ;
*/
any Constructor::visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) { return nullptr; }

/*
nullableExpr
    : unaryExpr (('??' | '!!') dataExpr)?
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
    : primaryData ({isAdjacent()}? (memberAccess | parentArgues | angledValues | '!') | annotation)*
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
    : '[' ((indexValues ','?) | dataExpr FOR identRef IN dataExpr (IF dataExpr)?)? ']'
    ;
*/
any Constructor::visitListExpr(OpenCMLParser::ListExprContext *context) { return nullptr; }

/*
primaryData
    : identRef
    | literal
    | listExpr
    | dictExpr
    | '(' dataExpr ')'        // if there is only one data, it will be recognized as a primary expression rather than a tuple
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
any Constructor::visitTypeOrData(OpenCMLParser::TypeOrDataContext *context) {
    return visit(context->children[0]);
}

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
    | TYPEOF dataExpr
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
tupleType
    : '(' (typeExpr (',' typeExpr)*)? ','? ')'
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
