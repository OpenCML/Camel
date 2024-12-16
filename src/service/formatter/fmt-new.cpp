#include "fmt-new.h"
#include <algorithm>

using namespace std;

string Formatter::formatStringLiteral(const string &input) {
    char quoteChar = (quotePrefer == QuotePreference::Single) ? '\'' : '"';
    const string slicedStr = input.substr(1, input.size() - 2);

    bool hasSingleQuote = (slicedStr.find('\'') != string::npos);
    bool hasDoubleQuote = (slicedStr.find('"') != string::npos);

    if (hasSingleQuote && !hasDoubleQuote) {
        quoteChar = '"';
    } else if (!hasSingleQuote && hasDoubleQuote) {
        quoteChar = '\'';
    } else {
        // escape all quotes
        string escapedStr;
        for (const char &c : slicedStr) {
            if (c == quoteChar) {
                escapedStr += '\\';
            }
            escapedStr += c;
        }
        return quoteChar + escapedStr + quoteChar;
    }

    return quoteChar + slicedStr + quoteChar;
}

inline bool isMultiLine(const antlr4::ParserRuleContext *context) {
    const size_t firstTokenLine = context->getStart()->getLine();
    size_t secondTokenLine = 0;
    // get second token
    const auto secondToken = context->children[1];
    if (antlr4::ParserRuleContext::is(secondToken)) {
        const antlr4::ParserRuleContext *secondTokenContext = dynamic_cast<antlr4::ParserRuleContext *>(secondToken);
        secondTokenLine = secondTokenContext->getStart()->getLine();
    } else {
        secondTokenLine = context->getStop()->getLine();
    }
    return firstTokenLine != secondTokenLine;
}

void Formatter::insertComment(antlr4::Token *comment, string &result) {
    const auto &commentText = comment->getText();
    result += commentText;
    if (comment->getChannel() == 4) {
        // block comment
        result += " ";
    }
};

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
