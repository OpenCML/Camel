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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The parser: type expressions.
 *
 *   typeExpr          T?  T!  (marks, any number)
 *   interType         &
 *   diffType          \
 *   keyUnionDiffType  +  -
 *   keyInterType      ^
 *   typeUnit          name of T
 *   arrayType         T[]
 *   specType          T<arguments>
 *   primaryType       name, {struct}, (T), (tuple), function type, typeof e, typeas name
 *
 * A type often stands where an expression goes on after it (`x as T + 1`, `x as T ^ 2`), and
 * `+`, `-`, `^` are operators of both. The type keeps the operator when a type can follow it,
 * as the ANTLR grammar did: `x as T + U` is a cast to the type `T + U`.
 */

#include "parser_impl.h"

namespace camel::parse::syntax {

/// Whether a type can start at the `n`-th token from the current one. A modifier starts one
/// only as the head of a function type (`sync (x: int) => int`).
bool ParserImpl::typeStartsAt(uint32_t n) const {
    TK kind = peek(n);
    while (isModifier(kind)) {
        kind = peek(++n);
        if (kind != TK::LParen && kind != TK::Lt && !isModifier(kind)) {
            return false;
        }
    }
    switch (kind) {
    case TK::Identifier:
    case TK::LParen:
    case TK::LBrace:
    case TK::Lt:
    case TK::KwTypeof:
    case TK::KwTypeas:
    case TK::KwEnum:
        return true;
    default:
        return false;
    }
}

/// typeExpr : interType ('?' | '!')*
/// `T?` may be null, `T!` may have failed; the marks combine. A `?` is only the nullable mark:
/// `T ? U` is no type at all, and what follows the mark is read as the next statement.
void ParserImpl::parseType() {
    Depth depth(*this);
    if (depth.tooDeep()) {
        unexpected({});
        return;
    }
    const size_t m = mark();
    parseInterType();
    if ((cur_ != TK::Question && cur_ != TK::Bang) || lineBreak() || panic_) {
        return;
    }
    while ((cur_ == TK::Question || cur_ == TK::Bang) && !lineBreak()) {
        bump();
    }
    close(m, NK::TypeExpr);
}

/// interType : diffType ('&' diffType)*
void ParserImpl::parseInterType() {
    const size_t m = mark();
    parseDiffType();
    bool any = false;
    while (cur_ == TK::Amp && !panic_) {
        bump();
        parseDiffType();
        any = true;
    }
    if (any) {
        close(m, NK::InterType);
    }
}

/// diffType : keyUnionDiffType ('\\' keyUnionDiffType)*
void ParserImpl::parseDiffType() {
    const size_t m = mark();
    parseKeyUnionDiffType();
    bool any = false;
    while (cur_ == TK::Backslash && !panic_) {
        bump();
        parseKeyUnionDiffType();
        any = true;
    }
    if (any) {
        close(m, NK::DiffType);
    }
}

/// keyUnionDiffType : keyInterType (('+' | '-') keyInterType)*
void ParserImpl::parseKeyUnionDiffType() {
    const size_t m = mark();
    parseKeyInterType();
    bool any = false;
    while ((cur_ == TK::Plus || cur_ == TK::Minus) && typeStartsAt(1) && !lineBreak() && !panic_) {
        bump();
        parseKeyInterType();
        any = true;
    }
    if (any) {
        close(m, NK::KeyUnionDiffType);
    }
}

/// keyInterType : typeUnit ('^' typeUnit)*
void ParserImpl::parseKeyInterType() {
    const size_t m = mark();
    parseTypeUnit();
    bool any = false;
    while (cur_ == TK::Caret && typeStartsAt(1) && !panic_) {
        bump();
        parseTypeUnit();
        any = true;
    }
    if (any) {
        close(m, NK::KeyInterType);
    }
}

/// typeUnit : (identDef 'of')? arrayType
void ParserImpl::parseTypeUnit() {
    if (cur_ == TK::Identifier && peek(1) == TK::KwOf) {
        const size_t m = mark();
        parseIdentDef();
        bump();
        parseArrayType();
        return close(m, NK::TypeUnit);
    }
    parseArrayType();
}

/// arrayType : specType ('[' ']')*
void ParserImpl::parseArrayType() {
    const size_t m = mark();
    parseSpecType();
    bool any = false;
    while (cur_ == TK::LBracket && peek(1) == TK::RBracket && !lineBreak() && !panic_) {
        bump();
        bump();
        any = true;
    }
    if (any) {
        close(m, NK::ArrayType);
    }
}

/// specType   : primaryType ('<' typeOrData (',' typeOrData)* '>')?
/// typeOrData : typeExpr | 'const' dataExpr
/// Rule 1: the `<` of type arguments touches the type (LAngle), as that of `with` arguments.
void ParserImpl::parseSpecType() {
    const size_t m = mark();
    parsePrimaryType();
    if (cur_ != TK::LAngle || panic_) {
        return;
    }
    const uint32_t open = pos_;
    bump();
    {
        Scope scope(*this, false, open);
        do {
            if (cur_ == TK::KwConst) {
                const size_t cm = mark();
                bump();
                {
                    // `constas` is written only in the expression of a const type argument.
                    ConstDataScope constData(*this, true);
                    parseExpr();
                }
                close(cm, NK::ConstData);
            } else {
                parseType();
            }
        } while (!panic_ && eat(TK::Comma));
        if (!panic_ && cur_ != TK::Gt) {
            unexpected({e(TK::Comma), e(TK::Gt)});
        }
    }
    eat(TK::Gt);
    close(m, NK::SpecType);
}

/// primaryType : structType | enumType | identRef | '(' typeExpr ')' | tupleType | funcType
///             | 'typeof' dataExpr | 'typeas' identDef
/// tupleType   : '(' typeList? ','? ')'
/// A `(` opens a function type when its head is that of a parameter list (`(name:`, `(var`, or
/// an empty list followed by `=>`); otherwise `(T)` is the parenthesized type and anything
/// else a tuple type.
void ParserImpl::parsePrimaryType() {
    // After `fields` and after `match type` the parser is in a type position: the head `(` opens
    // a tuple or a parenthesised type, so `fields ()` is the empty tuple type (H79 ruling 1).
    // The flag belongs to the head only: a type nested in the parentheses keeps the
    // function-type reading, and a function type at that head is written between one more pair
    // of parentheses (`fields ((x: int) => int)`).
    const bool typeHead = typeHead_;
    typeHead_           = false;
    switch (cur_) {
    case TK::Identifier:
        return parseIdentRef(NK::RefType);
    case TK::LBrace:
        return parseStructType();
    case TK::KwEnum:
        return parseEnumType();
    case TK::Lt:
    case TK::KwSync:
        return parseFuncType();
    case TK::KwTypeof: {
        // The operand stops before an assignment: `let v: typeof a = b` declares `v`.
        const size_t m = mark();
        bump();
        if (cur_ == TK::KwIf || cur_ == TK::KwMatch || cur_ == TK::KwTry || cur_ == TK::KwWait) {
            parseExpr();
        } else {
            parseLogicalOr();
        }
        return close(m, NK::TypeOfType);
    }
    case TK::KwTypeas: {
        const size_t m = mark();
        bump();
        parseIdentDef();
        return close(m, NK::TypeAsType);
    }
    case TK::LParen: {
        const TK t1 = peek(1);
        if (!typeHead &&
            (t1 == TK::KwVar || (t1 == TK::Identifier && peek(2) == TK::Colon) ||
             (t1 == TK::RParen && peek(2) == TK::FatArrow) ||
             (t1 == TK::Comma && peek(2) == TK::RParen && peek(3) == TK::FatArrow))) {
            return parseFuncType();
        }
        const size_t m = mark();
        bump();
        bool parenthesized = false;
        {
            Scope scope(*this, false);
            if (cur_ != TK::RParen && cur_ != TK::Comma) {
                const size_t lm = mark();
                parseType();
                if (cur_ == TK::RParen) {
                    parenthesized = true;
                } else {
                    while (cur_ == TK::Comma && peek(1) != TK::RParen && !panic_) {
                        bump();
                        parseType();
                    }
                    close(lm, NK::TypeList);
                }
            }
            eat(TK::Comma);
        }
        if (!panic_) {
            expectListEnd(TK::RParen);
        }
        return close(m, parenthesized ? NK::ParenType : NK::TupleType);
    }
    default:
        break;
    }
    if (isKeyword(cur_) && cur_ != TK::KwConst) {
        error(SyntaxError::Kind::KeywordAsName, {SyntaxError::TypeExpression}, pos_);
    } else {
        unexpected({SyntaxError::TypeExpression});
    }
}

/// structType  : '{' (keyTypePair (',' keyTypePair)*)? ','? '}'
/// keyTypePair : tagUse* identDef ':' typeExpr
/// A field may be tagged (`#tag x: int`, design principles 6.2).
void ParserImpl::parseStructType() {
    const size_t m = mark();
    bump();
    {
        Scope scope(*this, false);
        while ((cur_ == TK::Identifier || cur_ == TK::Hash) && !panic_) {
            const size_t pm = mark();
            while (cur_ == TK::Hash) {
                parseTagUse();
            }
            parseIdentDef();
            if (expect(TK::Colon)) {
                parseType();
            }
            close(pm, NK::KeyTypePair);
            if (cur_ != TK::Comma || (peek(1) != TK::Identifier && peek(1) != TK::Hash)) {
                break;
            }
            bump();
        }
        eat(TK::Comma);
    }
    if (!panic_) {
        expectListEnd(TK::RBrace);
    }
    close(m, NK::StructType);
}

/// funcType : modifiers? angledParams? parentParams '=>' typeExpr
void ParserImpl::parseFuncType() {
    const size_t m = mark();
    if (isModifier(cur_)) {
        parseModifiers();
    }
    if (cur_ == TK::Lt || cur_ == TK::LAngle) {
        parseAngledParams();
    }
    if (cur_ != TK::LParen) {
        unexpected({e(TK::LParen)});
        return close(m, NK::FuncType);
    }
    parseParentParams();
    if (!panic_ && expect(TK::FatArrow)) {
        parseType();
    }
    close(m, NK::FuncType);
}

/// typeList : typeExpr (',' typeExpr)*
void ParserImpl::parseTypeList(TK closer) {
    const size_t m = mark();
    parseType();
    while (cur_ == TK::Comma && peek(1) != closer && !panic_) {
        bump();
        parseType();
    }
    close(m, NK::TypeList);
}

/// enumType : 'enum' '{' (enumVariant (',' enumVariant)*)? ','? '}'
/// A variant without a field list is a unit variant; `Name(T, ...)` carries a tuple and
/// `Name { field: T, ... }` a struct. The names of the fields of a struct variant are part of
/// its declaration, so a variant is written in one of those two shapes only.
void ParserImpl::parseEnumType() {
    const size_t m = mark();
    bump();
    if (!expect(TK::LBrace)) {
        return;
    }
    {
        Scope scope(*this, false);
        if (cur_ == TK::Identifier) {
            parseEnumVariant();
            while (cur_ == TK::Comma && peek(1) == TK::Identifier && !panic_) {
                bump();
                parseEnumVariant();
            }
        }
        eat(TK::Comma);
    }
    if (!panic_) {
        expectListEnd(TK::RBrace);
    }
    close(m, NK::EnumType);
}

/// enumVariant : identDef ('(' typeList? ','? ')' | '{' (enumField (',' enumField)*)? ','? '}')?
void ParserImpl::parseEnumVariant() {
    const size_t m = mark();
    parseIdentDef();
    if (!panic_ && cur_ == TK::LParen) {
        bump();
        {
            Scope scope(*this, false);
            if (cur_ != TK::RParen && cur_ != TK::Comma) {
                parseTypeList(TK::RParen);
            }
            eat(TK::Comma);
        }
        if (!panic_) {
            expectListEnd(TK::RParen);
        }
    } else if (!panic_ && cur_ == TK::LBrace) {
        bump();
        {
            Scope scope(*this, false);
            if (cur_ == TK::Identifier) {
                parseEnumField();
                while (cur_ == TK::Comma && peek(1) == TK::Identifier && !panic_) {
                    bump();
                    parseEnumField();
                }
            }
            eat(TK::Comma);
        }
        if (!panic_) {
            expectListEnd(TK::RBrace);
        }
    }
    close(m, NK::EnumVariant);
}

/// enumField : identDef ':' typeExpr
void ParserImpl::parseEnumField() {
    const size_t m = mark();
    parseIdentDef();
    if (!panic_ && expect(TK::Colon)) {
        parseType();
    }
    close(m, NK::EnumField);
}

} // namespace camel::parse::syntax
