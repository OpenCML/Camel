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
 * Created: Oct. 12, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The parser: expressions.
 *
 * Precedence, loosest first (the levels of the grammar, unchanged):
 *
 *   dataExpr        if / match / try, or `wait`? assignExpr
 *   assignExpr      =  +=  -=  *=  /=  %=  ^=  @=  &=  |=      one, not associative
 *   logicalOrExpr   ||                                          left
 *   logicalAndExpr  &&                                          left
 *   orExpr          |                both sides evaluated        left
 *   andExpr         &                both sides evaluated        left
 *   equalityExpr    ===  !==  ==  !=                            left
 *   relationalExpr  <  >  <=  >=                                left
 *   additiveExpr    +  -                                        left
 *   multiplicative  *  /  @  %                                  left
 *   powerExpr       ^                                           right
 *   nullableExpr    ??  !!           right operand: a whole dataExpr
 *   unaryExpr       prefix !  ~      (operand: linkExpr);  postfix `as T`, `is T`
 *   linkExpr        ->  ?->                                     left
 *   negExpr         prefix -         tighter than `->`, looser than `^`:
 *                                    `-x->f` is `(-x)->f`, `-x ^ 2` is `-(x ^ 2)`
 *   compExpr        ..  ?..                                     left
 *   annoExpr        [index]  (arguments)  <arguments>  !  .member   postfix, touching
 *   accessExpr      .member
 *   primaryData     name, literal, [array], {struct}, (tuple), lambda
 *
 * Line breaks (rule 2). Where `nl_` is set, an operator that could also start a statement does
 * not continue an expression across a line break: `+`, `-` (a negation) and `@` (an
 * annotation). A symbol of two roles that starts a line has the role that starts something. Every
 * other operator of the table can only be binary and continues the expression wherever it stands. A
 * token the grammar requires (the operand after an operator, `then`, `=>`, a closing bracket) is
 * taken wherever it stands, too.
 */

#include "parser_impl.h"

namespace camel::parse::syntax {

bool ParserImpl::startsExpr(TK kind) const {
    switch (kind) {
    case TK::Identifier:
    case TK::Integer:
    case TK::Real:
    case TK::String:
    case TK::MultiStr:
    case TK::FString:
    case TK::KwTrue:
    case TK::KwFalse:
    case TK::KwNull:
    case TK::LParen:
    case TK::LBracket:
    case TK::LBrace:
    case TK::Lt:
    case TK::LAngle:
    case TK::Minus:
    case TK::Plus:
    case TK::Bang:
    case TK::Tilde:
    case TK::KwWait:
    case TK::KwIf:
    case TK::KwMatch:
    case TK::KwTry:
    case TK::KwLoop:
    case TK::KwSync:
    case TK::KwMacro:
    case TK::KwAtomic:
    case TK::KwShared:
        return true;
    default:
        return false;
    }
}

/// dataExpr : waitExpr | ctrlExpr | loopExpr
/// waitExpr : 'wait'? assignExpr
/// ctrlExpr : 'if' logicalOrExpr 'then' blockExpr ('else' blockExpr)?
///          | 'match' identRef '{' matchCase+ '}'
///          | 'try' stmtBlock catchClause+ ('finally' stmtBlock)?
void ParserImpl::parseExpr() {
    Depth depth(*this);
    if (depth.tooDeep()) {
        unexpected({});
        return;
    }
    switch (cur_) {
    case TK::KwIf:
        return parseIf();
    case TK::KwMatch:
        return parseMatch();
    case TK::KwTry:
        return parseTry();
    case TK::KwLoop:
        return parseLoop();
    case TK::KwWait: {
        const size_t m = mark();
        bump();
        parseAssign();
        return close(m, NK::WaitExpr);
    }
    default:
        return parseAssign();
    }
}

/// assignExpr : logicalOrExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&='
///              | '|=') logicalOrExpr)?
void ParserImpl::parseAssign() {
    const size_t m = mark();
    parseLogicalOr();
    switch (cur_) {
    case TK::Assign:
    case TK::PlusAssign:
    case TK::MinusAssign:
    case TK::StarAssign:
    case TK::SlashAssign:
    case TK::PercentAssign:
    case TK::CaretAssign:
    case TK::AtAssign:
    case TK::AmpAssign:
    case TK::PipeAssign:
        if (panic_) {
            return;
        }
        bump();
        parseLogicalOr();
        close(m, NK::AssignExpr);
        break;
    default:
        break;
    }
}

/// logicalOrExpr : logicalAndExpr ('||' logicalAndExpr)*
void ParserImpl::parseLogicalOr() {
    const size_t m = mark();
    parseLogicalAnd();
    if (cur_ != TK::PipePipe) {
        return;
    }
    while (cur_ == TK::PipePipe && !panic_) {
        bump();
        parseLogicalAnd();
    }
    close(m, NK::LogicalOrExpr);
}

/// logicalAndExpr : orExpr ('&&' orExpr)*
void ParserImpl::parseLogicalAnd() {
    const size_t m = mark();
    parseOr();
    if (cur_ != TK::AmpAmp) {
        return;
    }
    while (cur_ == TK::AmpAmp && !panic_) {
        bump();
        parseOr();
    }
    close(m, NK::LogicalAndExpr);
}

/// orExpr : andExpr ('|' andExpr)*
/// `|` and `&` are ordinary operators that evaluate both sides (and can be overloaded); `||`
/// and `&&` are reserved and evaluate their right side only when it decides the result.
void ParserImpl::parseOr() {
    const size_t m = mark();
    parseAnd();
    if (cur_ != TK::Pipe) {
        return;
    }
    while (cur_ == TK::Pipe && !panic_) {
        bump();
        parseAnd();
    }
    close(m, NK::OrExpr);
}

/// andExpr : equalityExpr ('&' equalityExpr)*
void ParserImpl::parseAnd() {
    const size_t m = mark();
    parseEquality();
    if (cur_ != TK::Amp) {
        return;
    }
    while (cur_ == TK::Amp && !panic_) {
        bump();
        parseEquality();
    }
    close(m, NK::AndExpr);
}

/// equalityExpr : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
void ParserImpl::parseEquality() {
    const size_t m = mark();
    parseRelational();
    bool any = false;
    while ((cur_ == TK::EqEq || cur_ == TK::NotEq || cur_ == TK::EqEqEq || cur_ == TK::NotEqEq) &&
           !panic_) {
        bump();
        parseRelational();
        any = true;
    }
    if (any) {
        close(m, NK::EqualityExpr);
    }
}

/// relationalExpr : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
/// Rule 1. `<` is the comparison only as the token Lt (the lexer made a `<` that touches its
/// left operand an LAngle, which the postfix loop of that operand takes). Directly inside an
/// angle-bracket list a `>` closes the list when it touches the token before it or starts its
/// line; after a space on the same line it is the comparison (`xs->filter<(x: int) => x > 1>`).
void ParserImpl::parseRelational() {
    const size_t m = mark();
    parseAdditive();
    bool any = false;
    while (!panic_) {
        if (cur_ == TK::Gt) {
            if (angle_ != kNone) {
                if (!tok().spaceBefore() || tok().newlineBefore()) {
                    break;
                }
                spacedGt_ = pos_;
            }
        } else if (cur_ != TK::Lt && cur_ != TK::LtEq && cur_ != TK::GtEq) {
            break;
        }
        bump();
        parseAdditive();
        any = true;
    }
    if (any) {
        close(m, NK::RelationalExpr);
    }
}

/// additiveExpr : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
/// Rule 2: a `+` or `-` that starts a line starts a statement.
void ParserImpl::parseAdditive() {
    const size_t m = mark();
    parseMultiplicative();
    bool any = false;
    while ((cur_ == TK::Plus || cur_ == TK::Minus) && !lineBreak() && !panic_) {
        bump();
        parseMultiplicative();
        any = true;
    }
    if (any) {
        close(m, NK::AdditiveExpr);
    }
}

/// multiplicativeExpr : powerExpr (('*' | '/' | '@' | '%') powerExpr)*
/// Rule 2: an `@` that starts a line starts an annotated function declaration.
void ParserImpl::parseMultiplicative() {
    const size_t m = mark();
    parsePower();
    bool any = false;
    while (!panic_) {
        if (cur_ == TK::At) {
            if (lineBreak()) {
                break;
            }
        } else if (cur_ != TK::Star && cur_ != TK::Slash && cur_ != TK::Percent) {
            break;
        }
        bump();
        parsePower();
        any = true;
    }
    if (any) {
        close(m, NK::MultiplicativeExpr);
    }
}

/// powerExpr : nullableExpr ('^' powerExpr)?
/// One level above the multiplicative operators and right-associative: `2 * 3 ^ 2` is 18,
/// `2 ^ 3 ^ 2` is `2 ^ (3 ^ 2)`.
void ParserImpl::parsePower() {
    const size_t m = mark();
    parseNullable();
    if (cur_ == TK::Caret && !panic_) {
        bump();
        parsePower();
        close(m, NK::PowerExpr);
    }
}

/// nullableExpr : unaryExpr (('??' | '!!') dataExpr)?
void ParserImpl::parseNullable() {
    const size_t m = mark();
    parseUnary();
    if ((cur_ == TK::QuestionQuestion || cur_ == TK::BangBang) && !panic_) {
        bump();
        parseExpr();
        close(m, NK::NullableExpr);
    }
}

/// unaryExpr : linkExpr (('as' | 'is') typeExpr)?
/// (The prefix operators `-`, `!` and `~` are taken by negExpr, under `->`.)
void ParserImpl::parseUnary() {
    const size_t m = mark();
    parseLink();
    if ((cur_ == TK::KwAs || cur_ == TK::KwIs) && !panic_) {
        bump();
        parseType();
        close(m, NK::CastExpr);
    }
}

/// linkExpr : negExpr (('->' | '?->') compExpr)*
void ParserImpl::parseLink() {
    const size_t m = mark();
    parseNeg(false);
    bool any = false;
    while ((cur_ == TK::Arrow || cur_ == TK::QuestionArrow) && !panic_) {
        bump();
        parseComp();
        any = true;
    }
    if (any) {
        close(m, NK::LinkExpr);
    }
}

/// negExpr : ('-' | '!' | '~') negPower | compExpr
/// negPower : ('-' | '!' | '~') negPower | compExpr ('^' negPower)?
/// The minus is always an operator, never part of a number, and reads the same before a
/// literal and before a name: it binds less tightly than `^` and more tightly than `->` and
/// every other binary operator. `-2 ^ 2` is `-(2 ^ 2)`, `-x->f` is `(-x)->f`, `-a * b` is
/// `(-a) * b`. `!` and `~` are at the same level (design principles 4.2): `!x->f` is
/// `(!x)->f`, `!a ^ b` is `!(a ^ b)`. Under a prefix operator the power is taken here;
/// elsewhere it is powerExpr, whose right operand reaches this function too, so `2 ^ -1` has
/// a signed exponent.
void ParserImpl::parseNeg(bool underMinus) {
    const size_t m = mark();
    if (cur_ == TK::Minus || cur_ == TK::Bang || cur_ == TK::Tilde) {
        Depth depth(*this);
        bump();
        parseNeg(true);
        return close(m, NK::UnaryExpr);
    }
    parseComp();
    if (underMinus && cur_ == TK::Caret && !panic_) {
        bump();
        parseNeg(true);
        close(m, NK::PowerExpr);
    }
}

/// compExpr : annoExpr (('..' | '?..') annoExpr)*
void ParserImpl::parseComp() {
    const size_t m = mark();
    parseAnno();
    bool any = false;
    while ((cur_ == TK::DotDot || cur_ == TK::QuestionDotDot) && !panic_) {
        bump();
        parseAnno();
        any = true;
    }
    if (any) {
        close(m, NK::CompExpr);
    }
}

/// annoExpr : accessExpr (indices | parentArgues | structData | angledValues | typeArguments | '!'
/// | memberAccess)*
/// memberAccess : '.' (IDENTIFIER | INTEGER | REAL)
/// Rule 1: an index, a call, an
/// angle-bracket argument list and the postfix `!` touch the expression before them. For `<` the
/// lexer has decided already (LAngle). `Shape::Rect { w: 2 }` is a struct variant construction:
/// a struct literal follows a variant path, never a name that could be a value.
void ParserImpl::parseAnno() {
    const size_t m = mark();
    parseAccess();
    bool any = false;
    while (!panic_) {
        if (adjacent()) {
            if (cur_ == TK::ColonColon && (peek(1) == TK::Lt || peek(1) == TK::LAngle)) {
                parseTypeArguments();
                any = true;
                continue;
            }
            // `xs[]` is not an index: the empty brackets are an array literal of their own, as
            // they always were (two statements on a line).
            if (cur_ == TK::LBracket && peek(1) != TK::RBracket) {
                parseIndices();
                any = true;
                continue;
            }
            if (cur_ == TK::LParen) {
                parseParentArgues();
                any = true;
                continue;
            }
            if (cur_ == TK::LAngle) {
                parseAngledValues();
                any = true;
                continue;
            }
            if (cur_ == TK::Bang) {
                bump();
                any = true;
                continue;
            }
        }
        if (cur_ == TK::LBrace && braceIsStruct(0) && lastRefIsPath()) {
            parseStructData();
            any = true;
            continue;
        }
        if (cur_ == TK::Dot) {
            const TK member = peek(1);
            if (member == TK::Identifier || member == TK::Integer || member == TK::Real) {
                const size_t mm = mark();
                bump();
                bump();
                close(mm, NK::MemberAccess);
                any = true;
                continue;
            }
        }
        break;
    }
    if (any) {
        close(m, NK::AnnoExpr);
    }
}

/// accessExpr : primaryData ('.' (IDENTIFIER | INTEGER | REAL))*
/// `t.0.1` arrives as `t`, `.`, and the REAL `0.1`: the AST builder splits it.
void ParserImpl::parseAccess() {
    const size_t m = mark();
    parsePrimary();
    bool any = false;
    while (cur_ == TK::Dot && !panic_) {
        const TK member = peek(1);
        if (member != TK::Identifier && member != TK::Integer && member != TK::Real) {
            bump();
            if (isKeyword(cur_)) {
                error(SyntaxError::Kind::KeywordAsName, {e(TK::Identifier)}, pos_);
            } else {
                unexpected({e(TK::Identifier), e(TK::Integer)});
            }
            any = true;
            break;
        }
        bump();
        bump();
        any = true;
    }
    if (any) {
        close(m, NK::AccessExpr);
    }
}

/// Whether the `(` at the current token opens the parameters of a lambda and not a tuple or a
/// parenthesized expression. Parameters are `name: type` pairs, so the head tells: `(name:`,
/// `(var`, or an empty list followed by `=>` or by the `:` of a return type. At most four
/// tokens are read.
bool ParserImpl::lambdaAhead() const {
    const TK t1 = peek(1);
    if (t1 == TK::KwVar) {
        return true;
    }
    if (t1 == TK::Identifier) {
        return peek(2) == TK::Colon;
    }
    if (t1 == TK::RParen) {
        const TK t2 = peek(2);
        return t2 == TK::FatArrow || t2 == TK::Colon;
    }
    if (t1 == TK::Comma && peek(2) == TK::RParen) {
        const TK t3 = peek(3);
        return t3 == TK::FatArrow || t3 == TK::Colon;
    }
    return false;
}

/// primaryData : identRef | constasData | literal | arrayData | structData | tupleData
///             | funcData
void ParserImpl::parsePrimary() {
    switch (cur_) {
    case TK::Identifier:
        return parseIdentRef(NK::RefData);
    case TK::KwConstas:
        // A named dimension is only meaningful in a `const` type argument (`const [constas n]`).
        if (constData_) {
            return parseConstasData();
        }
        break;
    case TK::Integer:
    case TK::Real:
    case TK::String:
    case TK::MultiStr:
    case TK::FString:
    case TK::KwTrue:
    case TK::KwFalse:
    case TK::KwNull:
        return parseLiteral();
    case TK::Plus:
        if (literalSignAhead()) {
            return parseLiteral();
        }
        if (lineBreak()) {
            // Rule 2: the line was meant to continue the one before it.
            error(SyntaxError::Kind::LineStartsWithSign, {SyntaxError::Expression}, pos_);
            return;
        }
        break;
    case TK::LBracket:
        return parseArrayData();
    case TK::LBrace:
        return parseStructData();
    case TK::LParen:
        return lambdaAhead() ? parseFuncData() : parseTupleData();
    case TK::Lt:
    case TK::LAngle:
    case TK::KwSync:
    case TK::KwMacro:
    case TK::KwAtomic:
    case TK::KwShared:
        return parseFuncData();
    default:
        break;
    }
    unexpected({SyntaxError::Expression});
}

/// constasData : 'constas' identDef
/// A named dimension (design principles 5.4): in a parameter type, `constas n` binds `n` the
/// first time it appears; later occurrences write `n` directly.
void ParserImpl::parseConstasData() {
    const size_t m = mark();
    bump();
    parseIdentDef();
    close(m, NK::ConstasData);
}

/// literal : ('+' | '-')? INTEGER | ('+' | '-')? REAL | STRING | MULTI_STR | FSTRING
///         | 'true' | 'false' | 'null'
/// The `-` is taken here in a pattern only (`case -1 =>`); in an expression a minus is the
/// operator of negExpr. A `+` before a number is part of the literal in both.
void ParserImpl::parseLiteral() {
    const size_t m = mark();
    if (cur_ == TK::Plus || cur_ == TK::Minus) {
        bump();
    }
    bump();
    close(m, NK::Literal);
}

/// indexValue : '...'? dataExpr
void ParserImpl::parseIndexValue() {
    if (cur_ == TK::Ellipsis) {
        const size_t m = mark();
        bump();
        parseExpr();
        return close(m, NK::SpreadValue);
    }
    parseExpr();
}

/// keyValuePair : identDef ':' dataExpr | '...' dataExpr
void ParserImpl::parseKeyValuePair() {
    const size_t m = mark();
    if (cur_ == TK::Ellipsis) {
        bump();
        parseExpr();
    } else {
        parseIdentDef();
        if (!panic_ && expect(TK::Colon)) {
            parseExpr();
        }
    }
    close(m, NK::KeyValuePair);
}

/// pairedValues : keyValuePair (',' keyValuePair)*
void ParserImpl::parsePairedValues(TK closer) {
    const size_t m = mark();
    parseKeyValuePair();
    while (cur_ == TK::Comma && peek(1) != closer && !panic_) {
        bump();
        parseKeyValuePair();
    }
    close(m, NK::PairedValues);
}

/// dataList : dataExpr (',' dataExpr)*
/// Inside brackets a comma before the closing bracket is the bracket's trailing comma.
void ParserImpl::parseDataList(TK closer) {
    const size_t m = mark();
    parseExpr();
    while (cur_ == TK::Comma && peek(1) != closer && !panic_) {
        bump();
        parseExpr();
    }
    close(m, NK::DataList);
}

/// argumentList : indexValues (',' pairedValues)? | pairedValues
/// indexValues  : indexValue (',' indexValue)*
/// Positional arguments come first; the first `name:` starts the named ones. A `...` before
/// that is a positional spread, after it a named one.
void ParserImpl::parseArguments(TK closer) {
    if (cur_ == closer) {
        return;
    }
    if (cur_ == TK::Comma && peek(1) == closer) {
        bump();
        return;
    }
    const size_t m = mark();
    bool named     = pairStartsAt(0);
    if (!named) {
        const size_t im = mark();
        parseIndexValue();
        while (cur_ == TK::Comma && peek(1) != closer && !pairStartsAt(1) && !panic_) {
            bump();
            parseIndexValue();
        }
        close(im, NK::IndexValues);
        if (cur_ == TK::Comma && pairStartsAt(1) && !panic_) {
            bump();
            named = true;
        }
    }
    if (named) {
        parsePairedValues(closer);
    }
    close(m, NK::ArgumentList);
    eat(TK::Comma);
}

/// indices : '[' dataExpr (':' dataExpr (':' dataExpr)?)? ']'
void ParserImpl::parseIndices() {
    const size_t m = mark();
    bump();
    {
        Scope scope(*this, false);
        parseExpr();
        if (eat(TK::Colon)) {
            parseExpr();
            if (eat(TK::Colon)) {
                parseExpr();
            }
        }
    }
    if (!panic_) {
        expect(TK::RBracket);
    }
    close(m, NK::Indices);
}

/// parentArgues : '(' argumentList? ','? ')'
void ParserImpl::parseParentArgues() {
    const size_t m = mark();
    bump();
    {
        Scope scope(*this, false);
        parseArguments(TK::RParen);
    }
    if (!panic_) {
        expectListEnd(TK::RParen);
    }
    close(m, NK::ParentArgues);
}

/// angledValues : '<' argumentList? ','? '>'
void ParserImpl::parseAngledValues() {
    const size_t m      = mark();
    const uint32_t open = pos_;
    bump();
    {
        Scope scope(*this, false, open);
        parseArguments(TK::Gt);
        if (!panic_ && cur_ != TK::Gt) {
            unexpected({e(TK::Comma), e(TK::Gt)});
        }
    }
    eat(TK::Gt);
    close(m, NK::AngledValues);
}

/// typeArguments : '::' '<' (typeList | namedTypeArgument (',' namedTypeArgument)*) ','? '>'
/// namedTypeArgument : IDENTIFIER '=' typeExpr
/// The arguments are all positional or all named (design principles 5.9): `f::<int, string>`
/// or `f::<A = int, B = string>`.
void ParserImpl::parseTypeArguments() {
    const size_t m = mark();
    bump(); // ::
    const uint32_t open = pos_;
    bump(); // < (not an expression's with-argument list)
    {
        Scope scope(*this, false, open);
        if (cur_ == TK::Identifier && peek(1) == TK::Assign) {
            while (!panic_) {
                const size_t named = mark();
                eat(TK::Identifier);
                eat(TK::Assign);
                parseType();
                close(named, NK::NamedTypeArgument);
                if (cur_ != TK::Comma || peek(1) == TK::Gt)
                    break;
                bump();
            }
        } else {
            parseTypeList(TK::Gt);
        }
        eat(TK::Comma);
        expect(TK::Gt);
    }
    close(m, NK::TypeArguments);
}

/// arrayData : '[' ((indexValues ','?) | dataExpr 'for' identRef 'in' dataExpr ('if' dataExpr)?)?
///             ']'
void ParserImpl::parseArrayData() {
    const size_t m = mark();
    bump();
    {
        Scope scope(*this, false);
        if (cur_ != TK::RBracket) {
            const size_t im = mark();
            parseIndexValue();
            if (cur_ == TK::KwFor && !panic_) {
                bump();
                parseIdentRef(NK::IdentRef);
                if (!panic_ && expect(TK::KwIn)) {
                    parseExpr();
                    if (eat(TK::KwIf)) {
                        parseExpr();
                    }
                }
            } else {
                while (cur_ == TK::Comma && peek(1) != TK::RBracket && !panic_) {
                    bump();
                    parseIndexValue();
                }
                close(im, NK::IndexValues);
                eat(TK::Comma);
            }
        }
    }
    if (!panic_) {
        expectListEnd(TK::RBracket);
    }
    close(m, NK::ArrayData);
}

/// structData : '{' (pairedValues ','?)? '}'
void ParserImpl::parseStructData() {
    const size_t m = mark();
    bump();
    {
        Scope scope(*this, false);
        if (cur_ != TK::RBrace) {
            parsePairedValues(TK::RBrace);
            eat(TK::Comma);
        }
    }
    if (!panic_) {
        expectListEnd(TK::RBrace);
    }
    close(m, NK::StructData);
}

/// tupleData : '(' dataList? ','? ')'
/// Also the parenthesized expression: one element and no comma.
void ParserImpl::parseTupleData() {
    const size_t m = mark();
    bump();
    {
        Scope scope(*this, false);
        if (cur_ != TK::RParen && cur_ != TK::Comma) {
            parseDataList(TK::RParen);
        }
        eat(TK::Comma);
    }
    if (!panic_) {
        expectListEnd(TK::RParen);
    }
    close(m, NK::TupleData);
}

/// funcData : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr
void ParserImpl::parseFuncData() {
    const size_t m = mark();
    if (isModifier(cur_)) {
        parseModifiers();
    }
    if (cur_ == TK::Lt || cur_ == TK::LAngle) {
        parseAngledParams();
    }
    if (cur_ != TK::LParen) {
        unexpected({e(TK::LParen)});
        return close(m, NK::FuncData);
    }
    parseParentParams();
    if (eat(TK::Colon)) {
        parseType();
    }
    if (!panic_ && expect(TK::FatArrow)) {
        parseBlockExpr();
    }
    close(m, NK::FuncData);
}

/// The `if` of ctrlExpr.
/// An `else` belongs to the nearest `if`, on whatever line it stands.
void ParserImpl::parseIf() {
    const size_t m = mark();
    bump();
    parseLogicalOr();
    if (!eat(TK::KwThen)) {
        if (cur_ == TK::Assign) {
            error(SyntaxError::Kind::AssignmentInCondition, {e(TK::KwThen)}, pos_);
        } else {
            error(SyntaxError::Kind::MissingThen, {e(TK::KwThen)}, pos_);
        }
        return close(m, NK::IfExpr);
    }
    parseBlockExpr();
    if (cur_ == TK::KwElse && !panic_) {
        bump();
        parseBlockExpr();
    }
    close(m, NK::IfExpr);
}

/// The `match` of ctrlExpr: `match` logicalOrExpr '{' matchCase+ '}'.
/// The subject is any expression; the `{` after it opens the cases (a struct literal after a
/// name is read only when the name is a variant path, `Shape::Rect { ... }`).
void ParserImpl::parseMatch() {
    const size_t m = mark();
    bump();
    parseLogicalOr();
    if (panic_ || !expect(TK::LBrace)) {
        return close(m, NK::MatchExpr);
    }
    {
        Scope scope(*this, true);
        if (cur_ != TK::KwCase) {
            unexpected({e(TK::KwCase)});
        }
        while (cur_ == TK::KwCase && !panic_) {
            parseMatchCase();
        }
        if (!panic_ && cur_ != TK::RBrace) {
            unexpected({e(TK::KwCase), e(TK::RBrace)});
        }
    }
    eat(TK::RBrace);
    close(m, NK::MatchExpr);
}

/// matchCase : 'case' pattern ('|' pattern)* '=>' blockExpr
void ParserImpl::parseMatchCase() {
    const size_t m = mark();
    bump();
    parsePattern();
    while (cur_ == TK::Pipe && !panic_) {
        bump();
        parsePattern();
    }
    if (!panic_ && expect(TK::FatArrow)) {
        parseBlockExpr();
    }
    close(m, NK::MatchCase);
}

/// pattern : identRef (identDef | enumTuplePattern | enumStructPattern)? | literal
///         | '(' (dataList | identList)? ','? ')' | '{' (pairedValues | identList)? ','? '}' | '_'
/// A parenthesized pattern is read as a dataList (the first alternative, which every identList
/// also is); a braced one as pairedValues when it starts with `name:` or `...`. A path with a
/// `::` followed by `(` or `{` is an enum pattern: the variant and the fields it binds.
void ParserImpl::parsePattern() {
    const size_t m = mark();
    switch (cur_) {
    case TK::Identifier:
        parseIdentRef(NK::IdentRef);
        if (cur_ == TK::LParen) {
            parseEnumTuplePattern();
        } else if (cur_ == TK::LBrace) {
            parseEnumStructPattern();
        } else if (cur_ == TK::Identifier) {
            parseIdentDef();
        }
        break;
    case TK::Underscore:
        bump();
        break;
    case TK::LParen: {
        bump();
        {
            Scope scope(*this, false);
            if (cur_ != TK::RParen && cur_ != TK::Comma) {
                parseDataList(TK::RParen);
            }
            eat(TK::Comma);
        }
        if (!panic_) {
            expectListEnd(TK::RParen);
        }
        break;
    }
    case TK::LBrace: {
        bump();
        {
            Scope scope(*this, false);
            if (cur_ == TK::Ellipsis || pairStartsAt(0)) {
                parsePairedValues(TK::RBrace);
            } else if (cur_ == TK::Identifier) {
                const size_t lm = mark();
                parseIdentDef();
                while (cur_ == TK::Comma && peek(1) != TK::RBrace && !panic_) {
                    bump();
                    parseIdentDef();
                }
                close(lm, NK::IdentList);
            }
            eat(TK::Comma);
        }
        if (!panic_) {
            expectListEnd(TK::RBrace);
        }
        break;
    }
    default:
        if (cur_ == TK::Integer || cur_ == TK::Real || cur_ == TK::String || cur_ == TK::MultiStr ||
            cur_ == TK::FString || cur_ == TK::KwTrue || cur_ == TK::KwFalse ||
            cur_ == TK::KwNull || literalSignAhead()) {
            parseLiteral();
        } else {
            unexpected({SyntaxError::PatternItem});
            return;
        }
    }
    close(m, NK::Pattern);
}

/// enumTuplePattern : '(' (pattern (',' pattern)*)? ','? ')'
/// A field the branch does not need is `_`. Nesting is read, so that the compiler can say it is
/// not supported yet instead of the parser refusing the program.
void ParserImpl::parseEnumTuplePattern() {
    const size_t m = mark();
    bump();
    {
        Scope scope(*this, false);
        if (cur_ != TK::RParen && cur_ != TK::Comma) {
            parsePattern();
            while (cur_ == TK::Comma && peek(1) != TK::RParen && !panic_) {
                bump();
                parsePattern();
            }
        }
        eat(TK::Comma);
    }
    if (!panic_) {
        expectListEnd(TK::RParen);
    }
    close(m, NK::EnumTuplePattern);
}

/// enumStructPattern : '{' (IDENTIFIER (':' pattern)? ','?)* '}'
/// `{ w, h }` binds the fields of those names, `{ w: width }` binds the field `w` to `width`,
/// and `{ w: _ }` binds nothing.
void ParserImpl::parseEnumStructPattern() {
    const size_t m = mark();
    bump();
    {
        Scope scope(*this, false);
        while (cur_ == TK::Identifier && !panic_) {
            bump();
            if (eat(TK::Colon)) {
                parsePattern();
            }
            if (cur_ != TK::Comma) {
                break;
            }
            bump();
        }
    }
    if (!panic_) {
        expectListEnd(TK::RBrace);
    }
    close(m, NK::EnumStructPattern);
}

/// The `try` of ctrlExpr (reserved: the AST builder reports it as not part of the language).
void ParserImpl::parseTry() {
    const size_t m = mark();
    bump();
    parseStmtBlock();
    if (!panic_ && cur_ != TK::KwCatch) {
        unexpected({e(TK::KwCatch)});
    }
    while (cur_ == TK::KwCatch && !panic_) {
        parseCatchClause();
    }
    if (cur_ == TK::KwFinally && !panic_) {
        bump();
        parseStmtBlock();
    }
    close(m, NK::TryExpr);
}

/// loopExpr : 'loop' loopState (',' loopState)* loopFor? loopWhile? 'do' blockExpr
/// loopState : identDef '=' logicalOrExpr
/// loopFor : 'for' identDef 'in' logicalOrExpr
/// loopWhile : 'while' logicalOrExpr
/// A loop is an expression with a state (design principles 3.10): each
/// name of the state is declared once in the head with its first value, several separated by
/// commas; the expression after `do` is evaluated once per round and its value is the state of
/// the next round (a tuple when the state has several names); the value of the loop is the last
/// state. `for` takes the elements of a sequence one per round, `while` goes on as long as its
/// condition holds, and with both the loop ends when either does.
/// The first values, the sequence and the condition are operator expressions, not `if`, `match`
/// or an assignment (write those in parentheses), so each ends at the comma or keyword that
/// follows. A comma after a first value starts another name of the state only when a name and
/// `=` follow; otherwise it belongs to what the loop is written in (`f(loop a = 0 for ..., 5)`).
/// What follows `do` is read as what follows `then`: `{ x: 1 }` is a struct, `{ 5 }` a block.
/// A loop needs `for` or `while` (one that has neither could not end; a question to the
/// language author, the narrow reading meanwhile).
void ParserImpl::parseLoop() {
    const size_t m = mark();
    bump();
    for (;;) {
        const size_t sm = mark();
        parseIdentDef();
        if (panic_ || !expect(TK::Assign)) {
            close(sm, NK::LoopState);
            return close(m, NK::LoopExpr);
        }
        parseLogicalOr();
        close(sm, NK::LoopState);
        if (cur_ == TK::Comma && peek(1) == TK::Identifier && peek(2) == TK::Assign && !panic_) {
            bump();
            continue;
        }
        break;
    }
    bool clause = false;
    if (cur_ == TK::KwFor && !panic_) {
        const size_t fm = mark();
        bump();
        parseIdentDef();
        if (!panic_ && expect(TK::KwIn)) {
            parseLogicalOr();
        }
        close(fm, NK::LoopFor);
        clause = true;
    }
    if (cur_ == TK::KwWhile && !panic_) {
        const size_t wm = mark();
        bump();
        parseLogicalOr();
        close(wm, NK::LoopWhile);
        clause = true;
    }
    if (!clause && !panic_) {
        unexpected({e(TK::KwFor), e(TK::KwWhile)});
    }
    if (!panic_ && expect(TK::KwDo)) {
        parseBlockExpr();
    }
    close(m, NK::LoopExpr);
}

/// catchClause : 'catch' identDef ':' typeExpr stmtBlock
void ParserImpl::parseCatchClause() {
    const size_t m = mark();
    bump();
    parseIdentDef();
    if (!panic_ && expect(TK::Colon)) {
        parseType();
        if (!panic_) {
            parseStmtBlock();
        }
    }
    close(m, NK::CatchClause);
}

} // namespace camel::parse::syntax
