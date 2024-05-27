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
std::any ASTConstructor::visitProgram(OpenCMLParser::ProgramContext *context){};

/*
stmtList : stmt+ ;
*/
std::any ASTConstructor::visitStmtList(OpenCMLParser::StmtListContext *context){};

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
std::any ASTConstructor::visitStmt(OpenCMLParser::StmtContext *context){};

/*
letStmt : LET carrier (':' typeExpr)? ('='? entityExpr)?
        | carrier (':' typeExpr)? ':=' entityExpr ;
*/
std::any ASTConstructor::visitLetStmt(OpenCMLParser::LetStmtContext *context){};

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
std::any ASTConstructor::visitCarrier(OpenCMLParser::CarrierContext *context){};

/*
annotation  : '@' primEntity ;
*/
std::any ASTConstructor::visitAnnotation(OpenCMLParser::AnnotationContext *context){};

/*
annotations : (annotation SEP?)+ ;
*/
std::any ASTConstructor::visitAnnotations(OpenCMLParser::AnnotationsContext *context){};

/*
modifiers   : (INNER | OUTER | ATOMIC | STATIC | SYNC)+ ;
*/
std::any ASTConstructor::visitModifiers(OpenCMLParser::ModifiersContext *context){};

/*
keyTypePair  : identRef ':' typeExpr ;
*/
std::any ASTConstructor::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context){};

/*
keyValuePair : entityExpr ':' entityExpr ;
*/
std::any ASTConstructor::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context){};

/*
keyParamPair : identRef annotation? ':' typeExpr ('=' entityExpr)? ;
*/
std::any ASTConstructor::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context){};

/*
typeList     : typeExpr (',' typeExpr)* ;
*/
std::any ASTConstructor::visitTypeList(OpenCMLParser::TypeListContext *context){};

/*
identList    : identRef (',' identRef)* ;
*/
std::any ASTConstructor::visitIdentList(OpenCMLParser::IdentListContext *context){};

/*
valueList    : entityExpr (',' entityExpr)* ;
*/
std::any ASTConstructor::visitValueList(OpenCMLParser::ValueListContext *context){};

/*
pairedTypes  : keyTypePair (',' keyTypePair)* ;
*/
std::any ASTConstructor::visitPairedTypes(OpenCMLParser::PairedTypesContext *context){};

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
*/
std::any ASTConstructor::visitPairedValues(OpenCMLParser::PairedValuesContext *context){};

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
std::any ASTConstructor::visitPairedParams(OpenCMLParser::PairedParamsContext *context){};

/*
argumentList : valueList (',' pairedValues)? | pairedValues ;
*/
std::any ASTConstructor::visitArgumentList(OpenCMLParser::ArgumentListContext *context){};

/*
bracedPairedValues : '{' pairedValues? ','? '}' ;
*/
std::any ASTConstructor::visitBracedPairedValues(OpenCMLParser::BracedPairedValuesContext *context){};

/*
bracedIdents       : '{' identList? ','? '}' ;
*/
std::any ASTConstructor::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context){};

/*
bracedStmts        : '{' stmtList? '}' ;
*/
std::any ASTConstructor::visitBracedStmts(OpenCMLParser::BracedStmtsContext *context){};

/*
bracketIdents : '[' identList? ','? ']' ;
*/
std::any ASTConstructor::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context){};

/*
bracketValues : '[' valueList? ','? ']' ;
*/
std::any ASTConstructor::visitBracketValues(OpenCMLParser::BracketValuesContext *context){};

/*
parentParams : '(' pairedParams? ','? ')' ;
*/
std::any ASTConstructor::visitParentParams(OpenCMLParser::ParentParamsContext *context){};

/*
parentValues : '(' argumentList? ','? ')' ;
*/
std::any ASTConstructor::visitParentValues(OpenCMLParser::ParentValuesContext *context){};

/*
angledParams : '<' pairedParams? ','? '>' ;
*/
std::any ASTConstructor::visitAngledParams(OpenCMLParser::AngledParamsContext *context){};

/*
angledValues : '<' argumentList? ','? '>' ;
*/
std::any ASTConstructor::visitAngledValues(OpenCMLParser::AngledValuesContext *context){};

/*
primEntity
    : identRef
    | literal
    | bracketValues
    | bracedPairedValues
    | lambdaExpr
    | '(' entityExpr ')' ;
*/
std::any ASTConstructor::visitPrimEntity(OpenCMLParser::PrimEntityContext *context){};

/*
memberAccess : '[' entityExpr ']' ;
*/
std::any ASTConstructor::visitMemberAccess(OpenCMLParser::MemberAccessContext *context){};

/*
entity       : primEntity (memberAccess | angledValues | annotation | parentValues)* ;
*/
std::any ASTConstructor::visitEntity(OpenCMLParser::EntityContext *context){};

/*
entityChain  : entityLink+ ;
*/
std::any ASTConstructor::visitEntityChain(OpenCMLParser::EntityChainContext *context){};

/*
entityLink   : entityCall | entityLink '->' entityCall ;
*/
std::any ASTConstructor::visitEntityLink(OpenCMLParser::EntityLinkContext *context){};

/*
entityCall   : entity | entityCall '.' entity ;
*/
std::any ASTConstructor::visitEntityCall(OpenCMLParser::EntityCallContext *context){};

/*
entitySpread : '...' entity ;
*/
std::any ASTConstructor::visitEntitySpread(OpenCMLParser::EntitySpreadContext *context){};

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
std::any ASTConstructor::visitEntityExpr(OpenCMLParser::EntityExprContext *context){};

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
std::any ASTConstructor::visitRelaExpr(OpenCMLParser::RelaExprContext *context){};

/*
addExpr
    : multiExpr
    | addExpr '+' multiExpr
    | addExpr '-' multiExpr
    | addExpr '&' multiExpr
    | addExpr '|' multiExpr
    ;
*/
std::any ASTConstructor::visitAddExpr(OpenCMLParser::AddExprContext *context){};

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
        ast_ptr_t linkNode = std::make_shared<ASTNode>(std::make_shared<LinkNode>());

        ast_ptr_t execNode = std::make_shared<ASTNode>(std::make_shared<ExecuteNode>());
        auto [multiRefNode, multiEntity] =
            makeDanglingPair(std::any_cast<ast_ptr_t>(visitMultiExpr(context->multiExpr())));
        auto [unaryRefNode, unaryEntity] = makeDanglingPair(std::any_cast<ast_ptr_t>(unaryExpr));

        ast_ptr_t dataNode = std::any_cast<ast_ptr_t>(unaryExpr);
        ast_ptr_t funcNode = nullptr;
        const auto &op = context->children[1]->getText();
        if (op == "^") {
            funcNode = std::make_shared<ASTNode>(std::make_shared<DeRefNode>("__pow__"));
        } else if (op == "*") {
            funcNode = std::make_shared<ASTNode>(std::make_shared<DeRefNode>("__mul__"));
        } else if (op == "/") {
            funcNode = std::make_shared<ASTNode>(std::make_shared<DeRefNode>("__div__"));
        } else if (op == "%") {
            funcNode = std::make_shared<ASTNode>(std::make_shared<DeRefNode>("__mod__"));
        } else if (op == "as") {
            funcNode = std::make_shared<ASTNode>(std::make_shared<DeRefNode>("__as__"));
        } else if (op == "is") {
            funcNode = std::make_shared<ASTNode>(std::make_shared<DeRefNode>("__is__"));
        } else {
            throw std::runtime_error("Unknown operator: " + op);
        }
        *linkNode << dataNode << funcNode;
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
    }
        break;
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

    return std::make_shared<Entity>(type, value);
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
    : ('<' typeList? '>')? '(' typeList? ')' '=>' typeExpr
    ;
*/
std::any ASTConstructor::visitLambdaType(OpenCMLParser::LambdaTypeContext *context){};

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
std::any ASTConstructor::visitPrimType(OpenCMLParser::PrimTypeContext *context){};

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
std::any ASTConstructor::visitStructType(OpenCMLParser::StructTypeContext *context){};

/*
specialType
    : ANY_TYPE
    | VOID_TYPE
    | FUNCTOR_TYPE
    ;
*/
std::any ASTConstructor::visitSpecialType(OpenCMLParser::SpecialTypeContext *context){};

/*
identRef : IDENTIFIER ;
*/
std::any ASTConstructor::visitIdentRef(OpenCMLParser::IdentRefContext *context){};