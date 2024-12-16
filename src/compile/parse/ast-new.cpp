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
 * Updated: Oct. 22, 2024
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
std::any visitProgram(OpenCMLParser::ProgramContext *context) {}

/*
decl
    : moduleDecl
    | importDecl
    | exportDecl
    | funcDecl
    ;
*/
std::any visitDecl(OpenCMLParser::DeclContext *context) {}

/*
stmt
    : letStmt
    | useStmt
    | typeStmt
    | enumStmt
    | exprStmt
    | retStmt
    | stmtBlock
    ;
*/
std::any visitStmt(OpenCMLParser::StmtContext *context) {}

/*
stmtList : stmt (SEP? stmt)* SEP? ;
*/
std::any visitStmtList(OpenCMLParser::StmtListContext *context) {}

/*
moduleDecl : MODULE identDef ;
*/
std::any visitModuleDecl(OpenCMLParser::ModuleDeclContext *context) {}

/*
importDecl : IMPORT (STRING | (identDef | bracedIdents) FROM STRING) ;
*/
std::any visitImportDecl(OpenCMLParser::ImportDeclContext *context) {}

/*
exportDecl : EXPORT (letStmt | typeStmt | bracedIdents) ;
*/
std::any visitExportDecl(OpenCMLParser::ExportDeclContext *context) {}

/*
funcDecl   : annotations? (WITH angledParams)? EXPORT? modifiers? FUNC identDef parentParams (':' typeExpr)? stmtBlock ;
*/
std::any visitFuncDecl(OpenCMLParser::FuncDeclContext *context) {}

/*
letStmt    : (LET | VAR) carrier (':' typeExpr)? '=' dataExpr ;
*/
std::any visitLetStmt(OpenCMLParser::LetStmtContext *context) {}

/*
useStmt    : USE (identDef '=')? identRef ;
*/
std::any visitUseStmt(OpenCMLParser::UseStmtContext *context) {}

/*
typeStmt   : TYPE identDef '=' typeExpr ;
*/
std::any visitTypeStmt(OpenCMLParser::TypeStmtContext *context) {}

/*
enumStmt   : ENUM identDef '=' bracedIdents ;
*/
std::any visitEnumStmt(OpenCMLParser::EnumStmtContext *context) {}

/*
exprStmt   : annotations? dataExpr ;
*/
std::any visitExprStmt(OpenCMLParser::ExprStmtContext *context) {}

/*
retStmt    : RETURN dataExpr ;
*/
std::any visitRetStmt(OpenCMLParser::RetStmtContext *context) {}

/*
stmtBlock  : SYNC? '{' stmtList? '}' ;
*/
std::any visitStmtBlock(OpenCMLParser::StmtBlockContext *context) {}

/*
lambdaExpr : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr ;
*/
std::any visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) {}

/*
carrier    : identDef | bracedIdents | bracketIdents ;
*/
std::any visitCarrier(OpenCMLParser::CarrierContext *context) {}

/*
annotation  : '@' primaryData ;
*/
std::any visitAnnotation(OpenCMLParser::AnnotationContext *context) {}

/*
annotations : annotation+ ;
*/
std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context) {}

/*
modifiers   : (INNER | OUTER | ATOMIC | SHARED | SYNC | MACRO)+ ;
*/
std::any visitModifiers(OpenCMLParser::ModifiersContext *context) {}

/*
indexValue   : dataExpr | '...' dataExpr ;
*/
std::any visitIndexValue(OpenCMLParser::IndexValueContext *context) {}

/*
keyTypePair  : identDef ':' typeExpr ;
*/
std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) {}

/*
keyValuePair : identDef ':' dataExpr | '...' dataExpr ;
*/
std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {}

/*
keyParamPair : VAR? identDef annotation? ':' (typeExpr | typeName) ('=' dataExpr)? ;
*/
std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {}

/*
typeList     : typeExpr (',' typeExpr)* ;
*/
std::any visitTypeList(OpenCMLParser::TypeListContext *context) {}

/*
identList    : identDef (',' identDef)* ;
*/
std::any visitIdentList(OpenCMLParser::IdentListContext *context) {}

/*
valueList    : dataExpr (',' dataExpr)* ;
*/
std::any visitValueList(OpenCMLParser::ValueListContext *context) {}

/*
indexValues  : indexValue (',' indexValue)* ;
*/
std::any visitIndexValues(OpenCMLParser::IndexValuesContext *context) {}

/*
pairedTypes  : keyTypePair (',' keyTypePair)* ;
*/
std::any visitPairedTypes(OpenCMLParser::PairedTypesContext *context) {}

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
*/
std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context) {}

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context) {}

/*
argumentList : indexValues (',' pairedValues)? | pairedValues ;
*/
std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context) {}

/*
bracedIdents       : '{' identList? ','? '}' ;    // for dict unpacking
*/
std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {}

/*
bracketIdents      : '[' identList? ','? ']' ;    // for list unpacking
*/
std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {}

/*
memberAccess       : '[' dataExpr (':' dataExpr (':' dataExpr)?)? ']' ;
*/
std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *context) {}

/*
parentParams       : '(' pairedParams? ','? ')' ; // for functor parameters definition
*/
std::any visitParentParams(OpenCMLParser::ParentParamsContext *context) {}

/*
parentArgues       : '(' argumentList? ','? ')' ; // for functor arguments
*/
std::any visitParentArgues(OpenCMLParser::ParentArguesContext *context) {}

/*
parentValues       : '(' valueList? ','? ')' ;    // for literal construction of tuple
*/
std::any visitParentValues(OpenCMLParser::ParentValuesContext *context) {}

/*
angledParams       : '<' pairedParams? ','? '>' ; // for functor super parameters definition
*/
std::any visitAngledParams(OpenCMLParser::AngledParamsContext *context) {}

/*
angledValues       : '<' argumentList? ','? '>' ; // for functor super arguments
*/
std::any visitAngledValues(OpenCMLParser::AngledValuesContext *context) {}

/*
blockExpr
    : stmtBlock | dataExpr
    ;
*/
std::any visitBlockExpr(OpenCMLParser::BlockExprContext *context) {}

/*
dataExpr
    : WAIT? structExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '&=' | '|=') structExpr)?
    ;
*/
std::any visitDataExpr(OpenCMLParser::DataExprContext *context) {}

/*
structExpr
    : logicalOrExpr
    | IF logicalOrExpr THEN blockExpr ELSE blockExpr
    | MATCH identRef '{' (CASE dataExpr '=>' blockExpr)+ '}'
    ;
*/
std::any visitStructExpr(OpenCMLParser::StructExprContext *context) {}

/*
logicalOrExpr
    : logicalAndExpr ('||' logicalAndExpr)*
    ;
*/
std::any visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) {}

/*
logicalAndExpr
    : equalityExpr ('&&' equalityExpr)*
    ;
*/
std::any visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) {}

/*
equalityExpr
    : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
    ;
*/
std::any visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) {}

/*
relationalExpr
    : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
    ;
*/
std::any visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) {}

/*
additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;
*/
std::any visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) {}

/*
multiplicativeExpr
    : nullableExpr (('^' | '*' | '/' | '%') nullableExpr)*
    ;
*/
std::any visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) {}

/*
nullableExpr
    : unaryExpr ('??' dataExpr)?
    ;
*/
std::any visitNullableExpr(OpenCMLParser::NullableExprContext *context) {}

/*
unaryExpr
    : linkExpr ((AS | IS) typeExpr)?
    | ('!' | '-' | '~') linkExpr
    ;
*/
std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) {}

/*
linkExpr
    : bindExpr (('->' | '?->') bindExpr)*
    ;
*/
std::any visitLinkExpr(OpenCMLParser::LinkExprContext *context) {}

/*
bindExpr
    : withExpr (('..' | '?..') withExpr)*
    ;
*/
std::any visitBindExpr(OpenCMLParser::BindExprContext *context) {}

/*
withExpr
    : annotatedExpr (('.' | '?.') annotatedExpr)*
    ;
*/
std::any visitWithExpr(OpenCMLParser::WithExprContext *context) {}

/*
annotatedExpr
    : primaryData ({isAdjacent()}? (memberAccess | parentArgues | angledValues) | annotation)*
    ;
*/
std::any visitAnnotatedExpr(OpenCMLParser::AnnotatedExprContext *context) {}

/*
dictExpr
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct of dict is immutable
    ;
*/
std::any visitDictExpr(OpenCMLParser::DictExprContext *context) {}

/*
listExpr
    : '[' ((indexValues ','?) | dataExpr FOR identRef IN dataExpr (IF dataExpr)?)? ']'
    ;
*/
std::any visitListExpr(OpenCMLParser::ListExprContext *context) {}

/*
primaryData
    : identRef
    | literal
    | listExpr
    | dictExpr
    | '(' dataExpr ')'      // if there is only one data, it will be recognized as a primary expression rather than a
tuple | parentValues          // for tuple | lambdaExpr
    ;
*/
std::any visitPrimaryData(OpenCMLParser::PrimaryDataContext *context) {}

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
std::any visitLiteral(OpenCMLParser::LiteralContext *context) {}

/*
typeName
    : TYPEAS identDef
    ;
*/
std::any visitTypeName(OpenCMLParser::TypeNameContext *context) {}

/*
typeExpr
    : unionType '?'?
    ;
*/
std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context) {}

/*
unionType
    : listType ('|' listType)*
    ;
*/
std::any visitUnionType(OpenCMLParser::UnionTypeContext *context) {}

/*
listType
    : argsType ('[' ']')*
    ;
*/
std::any visitListType(OpenCMLParser::ListTypeContext *context) {}

/*
argsType
    : primaryType ('<' typeList? '>')?
    ;
*/
std::any visitArgsType(OpenCMLParser::ArgsTypeContext *context) {}

/*
primaryType
    : INNER_ATOM_TYPE
    | dictExprType
    | identRef
    | '(' typeExpr ')'
    | lambdaType
    | TYPEOF dataExpr
    ;
*/
std::any visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) {}

/*
dictExprType
    : dictType (('&' | '^') dictType)*
    ;
*/
std::any visitDictExprType(OpenCMLParser::DictExprTypeContext *context) {}

/*
dictType
    : '{' pairedTypes? ','? '}'
    ;
*/
std::any visitDictType(OpenCMLParser::DictTypeContext *context) {}

/*
lambdaType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;
*/
std::any visitLambdaType(OpenCMLParser::LambdaTypeContext *context) {}

/*
identDef : IDENTIFIER ;
*/
std::any visitIdentDef(OpenCMLParser::IdentDefContext *context) {}

/*
identRef : (IDENTIFIER '::')* IDENTIFIER ;
*/
std::any visitIdentRef(OpenCMLParser::IdentRefContext *context) {}
