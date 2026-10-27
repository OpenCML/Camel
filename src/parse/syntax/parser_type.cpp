// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The parser: type expressions.
 *
 *   typeExpr          T?  T!  T..  (marks, any number, read left to right)
 *   arrayType         T[]
 *   specType          T<arguments>
 *   primaryType       name, {struct}, (T), (tuple), function type, typeof e
 *
 * A type never continues with an operator. The type operators the grammar used to reserve here
 * (`A & B`, `A \ B`, `A + B`, `A ^ B`) are not in the language, so writing one where a type is
 * expected is an ordinary syntax error, and the operand of a cast ends at the type: `x as T + U`
 * is a cast to `T` and then the expression's `+ U`. While the operators were reserved the same
 * line read that the other way, keeping the operator for the type when a type could follow it.
 */

#include "parser_impl.h"

namespace camel::parse::syntax {

/// Whether a type can start at the `n`-th token from the current one. A modifier does not: `sync`
/// is a property of a block, and a function type has none (design principles 3.3).
bool ParserImpl::typeStartsAt(uint32_t n) const {
    const TK kind = peek(n);
    switch (kind) {
    case TK::Identifier:
    case TK::LParen:
    case TK::LBrace:
    case TK::Lt:
    case TK::KwTypeof:
    case TK::KwEnum:
        return true;
    default:
        return false;
    }
}

/// typeExpr : arrayType ('?' | '!' | '..')*
/// A mark is `?` (may be null), `!` (may have failed) or `..` (the type is a stream). The marks
/// are read left to right and each one wraps what is written before it, with no special case
/// `T?..` is `(T?)..`, the stream of a nullable `T`, and
/// `T..?` is the possibly absent stream. `T..` and `T[..]` are therefore two types.
///
/// A dimension written after a stream mark applies to the stream: `T..[3]` is an array of three
/// streams (the builder of the array reports that a stream is linear and cannot be an element —
/// SemanticDiag::StreamInContainer). Written without a stream mark nothing changes: `T?[3]` is
/// still a syntax error, and only the mark that was just read reopens the bracket.
///
/// `A of B` used to be read here and reported as not implemented; `of` is not a keyword of the
/// language, so the form is an ordinary syntax error. The type
/// operators `A & B`, `A \ B`, `A + B` and `A ^ B` left the grammar the same way.
void ParserImpl::parseType() {
    Depth depth(*this);
    if (depth.tooDeep()) {
        unexpected({});
        return;
    }
    const size_t m = mark();
    parseArrayType();
    bool any    = false;
    bool stream = false;
    while (!lineBreak() && !panic_) {
        if (cur_ == TK::Question || cur_ == TK::Bang) {
            bump();
            any = true;
            continue;
        }
        if (cur_ == TK::DotDot) {
            bump();
            any    = true;
            stream = true;
            continue;
        }
        if (stream && cur_ == TK::LBracket) {
            // The mark read above is a type of its own: close it, and let the dimensions be
            // read against it as the dimensions of an array type are read against one.
            if (any) {
                close(m, NK::TypeExpr);
                any = false;
            }
            bool anyDim = false;
            while (cur_ == TK::LBracket && !lineBreak() && !panic_) {
                parseArrayDim();
                anyDim = true;
            }
            if (anyDim) {
                close(m, NK::ArrayType);
            }
            stream = false;
            continue;
        }
        break;
    }
    if (any) {
        close(m, NK::TypeExpr);
    }
}

/// arrayType : specType arrayDim+
void ParserImpl::parseArrayType() {
    const size_t m = mark();
    parseSpecType();
    bool any = false;
    while (cur_ == TK::LBracket && !lineBreak() && !panic_) {
        parseArrayDim();
        any = true;
    }
    if (any) {
        close(m, NK::ArrayType);
    }
}

/// arrayDim     : '[' (']' | arrayDimItem (',' arrayDimItem)* ','?)? ']'
/// arrayDimItem : '?' | '..' identDef? | dataExpr
/// A dimension is an unknown extent (`?`, or the empty `[]`), an expression over integers and
/// dimension names, or the trailing pack (`..`, `..rest`). The parser keeps a pack that is not
/// last (and a second pack) so the tree still holds what was written; GCT reports it.
void ParserImpl::parseArrayDim() {
    const size_t m = mark();
    bump();
    {
        Scope scope(*this, false);
        if (cur_ != TK::RBracket && !panic_) {
            while (!panic_) {
                if (cur_ == TK::Question) {
                    bump();
                } else if (cur_ == TK::DotDot) {
                    bump();
                    if (cur_ == TK::Identifier) {
                        parseIdentDef();
                    }
                } else {
                    parseExpr();
                }
                if (!eat(TK::Comma) || cur_ == TK::RBracket) {
                    break;
                }
            }
        }
        if (!panic_ && cur_ != TK::RBracket) {
            unexpected({e(TK::Comma), e(TK::RBracket)});
        }
    }
    eat(TK::RBracket);
    close(m, NK::ArrayDim);
}

/// specType   : primaryType ('<' typeOrData (',' typeOrData)* '>')?
/// typeOrData : typeExpr | arrayDim | 'const' dataExpr
/// Rule 1: the `<` of type arguments touches the type (LAngle), as that of `with` arguments.
/// A shape argument (`f32[n, 784]`) is written without `const`; the `const` branch
/// stays for the other constant arguments a module type may take.
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
            } else if (cur_ == TK::LBracket) {
                parseArrayDim();
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
///             | 'typeof' dataExpr
/// tupleType   : '(' typeList? ','? ')'
/// A `(` opens a function type when its head is that of a parameter list (`(name:`, `(var`, or
/// an empty list followed by `=>`); otherwise `(T)` is the parenthesized type and anything
/// else a tuple type.
void ParserImpl::parsePrimaryType() {
    // After `fields` and after `match type` the parser is in a type position: the head `(` opens
    // a tuple or a parenthesised type, so `fields ` is the empty tuple type (rule 1).
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
        return parseFuncType();
    case TK::KwSync:
        // A function type has no modifiers: `sync` belongs to a block (design principles 3.3).
        return unexpected({SyntaxError::TypeExpression});
    case TK::KwTypeof: {
        // The operand stops before an assignment: `let v: typeof a = b` declares `v`.
        const size_t m = mark();
        bump();
        if (cur_ == TK::KwIf || cur_ == TK::KwMatch) {
            parseExpr();
        } else {
            parseLogicalOr();
        }
        return close(m, NK::TypeOfType);
    }
    case TK::LParen: {
        const TK t1 = peek(1);
        if (!typeHead &&
            (t1 == TK::KwVar || (t1 == TK::Identifier && peek(2) == TK::Colon) ||
             (t1 == TK::RParen && peek(2) == TK::FatArrow) ||
             (t1 == TK::DotDot && peek(2) == TK::Identifier && peek(3) == TK::RParen &&
              peek(4) == TK::FatArrow) ||
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

/// funcType : angledParams? parentParams '=>' typeExpr resourceClause?
void ParserImpl::parseFuncType() {
    const size_t m = mark();
    if (cur_ == TK::Lt || cur_ == TK::LAngle) {
        parseAngledParams(true);
    }
    if (cur_ != TK::LParen) {
        unexpected({e(TK::LParen)});
        return close(m, NK::FuncType);
    }
    parseParentParams(true);
    if (!panic_ && expect(TK::FatArrow)) {
        parseType();
    }
    if (atWord(0, "uses") || atWord(0, "pure")) {
        parseResourceClause();
    }
    close(m, NK::FuncType);
}

/// resourceClause : ('uses' identRef (',' identRef)* | 'pure')+
///
/// What a function type says about the resources a call touches (design effects plan 3.1):
/// `pure` says none; `uses` names parameters (the values a call touches) and resources
/// (ambient state, a module's resource, or a group of them). `uses` and `pure` are contextual.
/// A `pure` followed by a `uses` is read here and refused when the clause is compiled
/// (PureAndUses), so the error names the two words rather than the token after them.
void ParserImpl::parseResourceClause() {
    const size_t m = mark();
    while (!panic_ && (atWord(0, "pure") || atWord(0, "uses"))) {
        if (atWord(0, "pure")) {
            bump();
            continue;
        }
        bump(); // `uses`
        parseIdentRef(NK::IdentRef);
        while (cur_ == TK::Comma && peek(1) == TK::Identifier && !panic_) {
            bump();
            parseIdentRef(NK::IdentRef);
        }
    }
    close(m, NK::ResourceClause);
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
