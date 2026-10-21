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
 * The parser: infrastructure, program, declarations and statements.
 * Expressions are in parser_expr.cpp, types in parser_type.cpp.
 */

#include "parser_impl.h"

#include "camel/parse/syntax/lexer.h"

namespace camel::parse::syntax {

Tree Parser::parse(std::string source) { return parse(Lexer::tokenize(std::move(source))); }

Tree Parser::parse(TokenBuffer tokens) { return ParserImpl(std::move(tokens)).run(); }

Tree Parser::parseExpression(TokenBuffer tokens) {
    return ParserImpl(std::move(tokens)).runExpression();
}

// ---- Node access (cst.h) ----

std::string Node::text() const {
    std::string out;
    const NodeData &d      = tree_->data(id_);
    const TokenBuffer &buf = tree_->tokens();
    if ((d.flags & 1) != 0) {
        return out;
    }
    for (uint32_t i = d.firstToken; i <= d.lastToken; ++i) {
        if (!isTrivia(buf[i].kind)) {
            out.append(buf.text(i));
        }
    }
    return out;
}

Node Node::nodeBy(int filter, size_t nth) const {
    if (tree_ == nullptr) {
        return Node();
    }
    const NodeData &d = tree_->data(id_);
    for (uint32_t i = 0; i < d.childCount; ++i) {
        const uint32_t word = tree_->childWord(d.firstChild + i);
        if ((word & Tree::kTokenBit) == 0 && passes(tree_->data(word).kind, filter)) {
            if (nth == 0) {
                return Node(tree_, word);
            }
            --nth;
        }
    }
    return Node();
}

size_t Node::countBy(int filter) const {
    if (tree_ == nullptr) {
        return 0;
    }
    size_t count      = 0;
    const NodeData &d = tree_->data(id_);
    for (uint32_t i = 0; i < d.childCount; ++i) {
        const uint32_t word = tree_->childWord(d.firstChild + i);
        if ((word & Tree::kTokenBit) == 0 && passes(tree_->data(word).kind, filter)) {
            ++count;
        }
    }
    return count;
}

TokenRef Node::tok(TokenKind k, size_t nth) const {
    if (tree_ == nullptr) {
        return TokenRef();
    }
    const NodeData &d = tree_->data(id_);
    for (uint32_t i = 0; i < d.childCount; ++i) {
        const uint32_t word = tree_->childWord(d.firstChild + i);
        if ((word & Tree::kTokenBit) != 0 && tree_->tokens()[word & ~Tree::kTokenBit].kind == k) {
            if (nth == 0) {
                return TokenRef(tree_, word & ~Tree::kTokenBit);
            }
            --nth;
        }
    }
    return TokenRef();
}

size_t Node::tokCount(TokenKind k) const {
    size_t count = 0;
    if (tree_ == nullptr) {
        return 0;
    }
    const NodeData &d = tree_->data(id_);
    for (uint32_t i = 0; i < d.childCount; ++i) {
        const uint32_t word = tree_->childWord(d.firstChild + i);
        if ((word & Tree::kTokenBit) != 0 && tree_->tokens()[word & ~Tree::kTokenBit].kind == k) {
            ++count;
        }
    }
    return count;
}

// ---- Infrastructure ----

ParserImpl::ParserImpl(TokenBuffer tokens) : tree_(std::move(tokens)) {
    toks_  = tree_.tokens_.tokens().data();
    count_ = static_cast<uint32_t>(tree_.tokens_.size());
    // About one node per two tokens and one child slot per token and per node.
    tree_.nodes_.reserve(count_ / 2 + 16);
    tree_.children_.reserve(count_ + count_ / 2 + 16);
    stack_.reserve(256);
    pos_ = 0;
    while (isTrivia(toks_[pos_].kind)) {
        ++pos_;
    }
    prev_ = pos_;
    cur_  = toks_[pos_].kind;
}

Tree ParserImpl::run() {
    parseProgram();
    tree_.root_ = stack_.empty() ? Tree::kNoNode : stack_.back();
    return std::move(tree_);
}

Tree ParserImpl::runExpression() {
    parseExpr();
    // One expression is the whole input: anything after it is not part of the interpolation.
    if (!panic_ && cur_ != TK::Eof) {
        unexpected({});
    }
    tree_.root_ = stack_.empty() ? Tree::kNoNode : stack_.back();
    return std::move(tree_);
}

void ParserImpl::close(size_t mark, NK kind) {
    NodeData node;
    node.kind         = kind;
    node.firstChild   = static_cast<uint32_t>(tree_.children_.size());
    node.childCount   = static_cast<uint32_t>(stack_.size() - mark);
    const uint32_t id = static_cast<uint32_t>(tree_.nodes_.size());
    // The node spans from its first to its last token. A child without tokens (a node the
    // parser closed empty in a wrong text) does not count.
    bool any = false;
    for (size_t i = mark; i < stack_.size(); ++i) {
        const uint32_t word = stack_[i];
        uint32_t first, last;
        if ((word & Tree::kTokenBit) != 0) {
            first = last = word & ~Tree::kTokenBit;
        } else {
            NodeData &child = tree_.nodes_[word];
            child.parent    = id;
            if ((child.flags & kEmptyNode) != 0) {
                tree_.children_.push_back(word);
                continue;
            }
            first = child.firstToken;
            last  = child.lastToken;
        }
        if (!any) {
            node.firstToken = first;
            any             = true;
        }
        node.lastToken = last;
        tree_.children_.push_back(word);
    }
    if (!any) {
        node.firstToken = node.lastToken = pos_;
        node.flags |= kEmptyNode;
    }
    stack_.resize(mark);
    tree_.nodes_.push_back(node);
    stack_.push_back(id);
}

void ParserImpl::error(
    SyntaxError::Kind kind, std::initializer_list<uint8_t> expected, uint32_t at) {
    if (panic_) {
        return;
    }
    panic_ = true;
    SyntaxError err;
    err.kind     = kind;
    err.token    = at;
    err.expected = expected;
    // Rule 1. The parse failed directly inside an angle-bracket list. If the list was opened by
    // a `<` touching the token before it, the `<` may have been meant as a comparison (`i<n`);
    // if a `>` after a space was read as a comparison in it, that `>` may have been meant to
    // close it (`f<a >`). The plain error is kept for the classifier to describe.
    if ((kind == SyntaxError::Kind::Unexpected || kind == SyntaxError::Kind::Missing) &&
        angle_ != kNone) {
        if (spacedGt_ != kNone) {
            err.kind = SyntaxError::Kind::AngleCloseAfterSpace;
            err.aux  = spacedGt_;
        } else if (toks_[angle_].kind == TK::LAngle) {
            err.kind = SyntaxError::Kind::ComparisonNeedsSpace;
            err.aux  = angle_;
        }
    }
    tree_.errors_.push_back(std::move(err));
}

bool ParserImpl::expect(TK kind) {
    if (cur_ == kind) {
        bump();
        return true;
    }
    error(SyntaxError::Kind::Missing, {e(kind)}, pos_);
    return false;
}

bool ParserImpl::expectListEnd(TK closer) {
    if (cur_ == closer) {
        bump();
        return true;
    }
    unexpected({e(TK::Comma), e(closer)});
    return false;
}

bool ParserImpl::expectName(NK wrapAs) {
    if (cur_ == TK::Identifier) {
        const size_t m = mark();
        bump();
        close(m, wrapAs);
        return true;
    }
    if (isKeyword(cur_)) {
        error(SyntaxError::Kind::KeywordAsName, {e(TK::Identifier)}, pos_);
    } else {
        unexpected({e(TK::Identifier)});
    }
    return false;
}

/// Skips to the next place a statement can start: a `;`, the `}` that closes the block the
/// parser is in, or a line that begins with a token a statement (at top level: a declaration)
/// starts with. Brackets opened on the way are skipped with their contents. What is skipped
/// becomes an Error node.
void ParserImpl::recover(bool inBlock, bool topLevel) {
    const size_t m       = mark();
    const uint32_t start = pos_;
    int nesting          = 0;
    while (cur_ != TK::Eof) {
        if (nesting == 0) {
            if (cur_ == TK::Semi || (cur_ == TK::RBrace && inBlock)) {
                break;
            }
            if (pos_ != start && tok().newlineBefore() &&
                (topLevel ? startsDecl(cur_) || atRuleDecl(0) : startsStmt(cur_))) {
                break;
            }
            // A declaration keyword starts a declaration wherever it stands.
            if (pos_ != start &&
                (cur_ == TK::KwFunc || cur_ == TK::KwLet || cur_ == TK::KwVar ||
                 cur_ == TK::KwImport || cur_ == TK::KwModule || cur_ == TK::KwNamespace)) {
                break;
            }
        }
        if (cur_ == TK::LParen || cur_ == TK::LBracket || cur_ == TK::LBrace) {
            ++nesting;
        } else if (cur_ == TK::RParen || cur_ == TK::RBracket || cur_ == TK::RBrace) {
            if (nesting > 0) {
                --nesting;
            }
        }
        stack_.push_back(pos_ | Tree::kTokenBit);
        advance();
    }
    if (stack_.size() > m) {
        close(m, NK::Error);
    }
    panic_ = false;
}

// ---- Program ----

bool ParserImpl::startsDecl(TK kind) const {
    switch (kind) {
    case TK::KwModule:
    case TK::KwImport:
    case TK::KwExport:
    case TK::KwNamespace:
    case TK::KwUsing:
    case TK::KwLet:
    case TK::KwVar:
    case TK::KwFunc:
    case TK::KwType:
    case TK::KwWith:
    case TK::KwInner:
    case TK::KwOuter:
    case TK::KwAtomic:
    case TK::KwShared:
    case TK::KwSync:
    case TK::KwMacro:
    case TK::At:
        return true;
    default:
        return false;
    }
}

bool ParserImpl::startsStmt(TK kind) const {
    switch (kind) {
    case TK::KwLet:
    case TK::KwVar:
    case TK::KwFunc:
    case TK::KwType:
    case TK::KwUsing:
    case TK::KwWith:
    case TK::KwExport:
    case TK::KwInner:
    case TK::KwOuter:
    case TK::KwReturn:
    case TK::KwThrow:
    case TK::At:
        return true;
    default:
        return startsExpr(kind);
    }
}

/// program : SEP? (decl SEP?)* EOF
void ParserImpl::parseProgram() {
    const size_t m = mark();
    eat(TK::Semi);
    while (cur_ != TK::Eof) {
        const uint32_t before = pos_;
        if (cur_ == TK::Semi) {
            // One separator between declarations was taken already.
            error(SyntaxError::Kind::ExtraSemicolon, {}, pos_);
        } else if (startsDecl(cur_) || atRuleDecl(0)) {
            parseDecl();
            if (!panic_) {
                eat(TK::Semi);
                continue;
            }
        } else {
            unexpected({SyntaxError::Declaration, e(TK::Eof)});
        }
        recover(false, true);
        if (pos_ == before && cur_ != TK::Eof) {
            // Nothing was consumed and nothing skipped: step over the token.
            const size_t em = mark();
            stack_.push_back(pos_ | Tree::kTokenBit);
            advance();
            close(em, NK::Error);
        }
        eat(TK::Semi);
    }
    stack_.push_back(pos_ | Tree::kTokenBit); // EOF
    close(m, NK::Program);
}

/// The number of tokens the modifiers at the current token take (0 if there is none).
uint32_t ParserImpl::afterModifiers() const {
    uint32_t n = 0;
    while (isModifier(peek(n))) {
        ++n;
    }
    return n;
}

/// decl : moduleDecl | importDecl | exportDecl | namespaceDecl | usingNamespaceDecl
///      | dataDecl | funcDecl | typeDecl | ruleDecl
/// Decided on the first token; after `export`, `inner` and `outer` on the next one or two
/// (`export inner type` is an exported type, `export inner func` a function).
void ParserImpl::parseDecl() {
    switch (cur_) {
    case TK::KwModule:
        return parseModuleDecl();
    case TK::KwImport:
        return parseImportDecl();
    case TK::KwNamespace:
        return parseNamespaceDecl();
    case TK::KwUsing:
        return parseUsingNamespaceDecl();
    case TK::KwLet:
    case TK::KwVar:
        return parseDataDecl();
    case TK::KwType:
        return parseTypeDecl();
    case TK::KwExport: {
        const TK next = peek(1);
        if (atRuleDecl(1)) {
            return parseRuleDecl();
        }
        if (next == TK::KwLet || next == TK::KwVar || next == TK::KwType || next == TK::LBrace ||
            ((next == TK::KwInner || next == TK::KwOuter) && peek(2) == TK::KwType)) {
            return parseExportDecl();
        }
        return parseFuncDecl();
    }
    case TK::KwInner:
    case TK::KwOuter:
        return peek(1) == TK::KwType ? parseTypeDecl() : parseFuncDecl();
    default:
        if (atRuleDecl(0)) {
            return parseRuleDecl();
        }
        return parseFuncDecl();
    }
}

/// stmt : dataDecl | funcDecl | typeDecl | dataExpr | usingNamespaceDecl | retStmt
///      | blockStmt
/// blockStmt : 'wait'? stmtBlock | funcAnno+ stmtBlock
/// Decided on the first token, except:
///   - modifiers (`sync`, `macro`, ...) are skipped to see what they modify: `func` (a
///     declaration), `{` after a lone `sync` (a block), anything else (a lambda expression);
///   - `{` is a struct literal when it is `{}`, `{ name: ...` or `{ ...`, a block otherwise;
///     the same after `wait`.
void ParserImpl::parseStmt() {
    Depth depth(*this);
    if (depth.tooDeep()) {
        unexpected({});
        return;
    }
    switch (cur_) {
    case TK::KwLet:
    case TK::KwVar:
        return parseDataDecl();
    case TK::KwType:
        return parseTypeDecl();
    case TK::KwUsing:
        return parseUsingNamespaceDecl();
    case TK::KwReturn:
    case TK::KwThrow:
        return parseRetStmt();
    case TK::KwFunc:
    case TK::KwWith:
    case TK::KwExport:
        return parseFuncDecl();
    case TK::At:
        return parseAnnotatedStmt();
    case TK::KwInner:
    case TK::KwOuter:
        return peek(1) == TK::KwType ? parseTypeDecl() : parseFuncDecl();
    case TK::KwAtomic:
    case TK::KwShared:
    case TK::KwSync:
    case TK::KwMacro: {
        const uint32_t n = afterModifiers();
        if (peek(n) == TK::KwFunc) {
            return parseFuncDecl();
        }
        if (cur_ == TK::KwSync && n == 1 && peek(1) == TK::LBrace) {
            return parseStmtBlock();
        }
        return parseExpr();
    }
    case TK::KwWait: {
        // blockStmt : 'wait' stmtBlock, or the expression `wait {struct}`.
        const uint32_t brace = peek(1) == TK::KwSync ? 2 : 1;
        if (peek(brace) == TK::LBrace &&
            (brace == 2 || !(peek(2) == TK::RBrace || braceIsStruct(1)))) {
            const size_t m = mark();
            bump();
            parseStmtBlock();
            close(m, NK::BlockStmt);
            return;
        }
        return parseExpr();
    }
    case TK::LBrace:
        // The expression is the first alternative: `{}` alone is the empty struct.
        if (peek(1) == TK::RBrace || braceIsStruct(0)) {
            return parseExpr();
        }
        return parseStmtBlock();
    default:
        return parseExpr();
    }
}

/// stmtList : stmt (SEP? stmt)* SEP?
/// Two statements need nothing between them: the first ends where its expression cannot go on,
/// which a line break makes sure of (rule 2).
void ParserImpl::parseStmtList() {
    const size_t m = mark();
    while (cur_ != TK::RBrace && cur_ != TK::Eof) {
        const uint32_t before = pos_;
        if (cur_ == TK::Semi) {
            if (toks_[prev_].kind == TK::Semi) {
                error(SyntaxError::Kind::ExtraSemicolon, {}, pos_);
            } else {
                unexpected({e(TK::RBrace)});
            }
        } else if (startsStmt(cur_)) {
            parseStmt();
            if (!panic_) {
                eat(TK::Semi);
                continue;
            }
        } else {
            unexpected({e(TK::RBrace)});
        }
        recover(true, false);
        if (pos_ == before && cur_ != TK::Eof && cur_ != TK::RBrace) {
            const size_t em = mark();
            stack_.push_back(pos_ | Tree::kTokenBit);
            advance();
            close(em, NK::Error);
        }
        eat(TK::Semi);
    }
    if (stack_.size() > m) {
        close(m, NK::StmtList);
    }
}

/// stmtBlock : 'sync'? '{' stmtList? '}'
void ParserImpl::parseStmtBlock() {
    Depth depth(*this);
    const size_t m = mark();
    eat(TK::KwSync);
    if (!eat(TK::LBrace)) {
        unexpected({e(TK::LBrace), e(TK::KwSync)});
        return close(m, NK::StmtBlock);
    }
    if (depth.tooDeep()) {
        unexpected({});
        return close(m, NK::StmtBlock);
    }
    {
        Scope scope(*this, true);
        parseStmtList();
    }
    expect(TK::RBrace);
    close(m, NK::StmtBlock);
}

/// blockExpr : stmtBlock | dataExpr
/// The block is the first alternative: `{}` is the empty block here. `{ name: ...` and `{ ...`
/// are a struct literal, that is an expression.
void ParserImpl::parseBlockExpr() {
    if ((cur_ == TK::KwSync && peek(1) == TK::LBrace) ||
        (cur_ == TK::LBrace && !braceIsStruct(0))) {
        return parseStmtBlock();
    }
    parseExpr();
}

// ---- Declarations ----

/// moduleName : ('.' | '..' | '...')? IDENTIFIER ('.' IDENTIFIER)* ('::' IDENTIFIER)*
/// A path after `::` names a namespace of the module (`import { f } from m::N`).
void ParserImpl::parseModuleName() {
    const size_t m = mark();
    if (cur_ == TK::Dot || cur_ == TK::DotDot || cur_ == TK::Ellipsis) {
        bump();
    }
    if (cur_ == TK::Identifier) {
        bump();
        while (cur_ == TK::Dot && peek(1) == TK::Identifier) {
            bump();
            bump();
        }
        while (cur_ == TK::ColonColon && peek(1) == TK::Identifier) {
            bump();
            bump();
        }
    } else if (isKeyword(cur_)) {
        error(SyntaxError::Kind::KeywordAsName, {e(TK::Identifier)}, pos_);
    } else {
        unexpected({e(TK::Identifier)});
    }
    close(m, NK::ModuleName);
}

/// moduleDecl : 'module' identDef
void ParserImpl::parseModuleDecl() {
    const size_t m = mark();
    bump();
    expectName(NK::IdentDef);
    close(m, NK::ModuleDecl);
}

/// importDecl : 'import' (moduleName ('as' identDef)? | (importItem | bracedImports) 'from'
/// moduleName) A name followed by `from` is the one name imported (design principles 10.2: `import
/// a from m` is `import { a } from m`), otherwise the start of the module name; `import m as x`
/// gives the module another name.
void ParserImpl::parseImportDecl() {
    const size_t m = mark();
    bump();
    if (cur_ == TK::LBrace) {
        parseBracedImports();
        expect(TK::KwFrom);
        parseModuleName();
    } else if (
        cur_ == TK::Identifier && (peek(1) == TK::KwFrom || peek(1) == TK::KwAs) &&
        (peek(1) == TK::KwFrom || peek(3) == TK::KwFrom)) {
        parseImportItem();
        expect(TK::KwFrom);
        parseModuleName();
    } else {
        parseModuleName();
        if (cur_ == TK::KwAs && !panic_) {
            bump();
            parseIdentDef();
        }
    }
    close(m, NK::ImportDecl);
}

/// bracedImports : '{' (importItem (',' importItem)*)? ','? '}'
void ParserImpl::parseBracedImports() {
    const size_t m = mark();
    bump();
    {
        Scope scope(*this, false);
        if (cur_ != TK::RBrace && cur_ != TK::Comma) {
            parseImportItem();
            while (cur_ == TK::Comma && peek(1) != TK::RBrace && !panic_) {
                bump();
                parseImportItem();
            }
        }
        eat(TK::Comma);
    }
    if (!panic_) {
        expectListEnd(TK::RBrace);
    }
    close(m, NK::BracedImports);
}

/// importItem : identDef ('as' identDef)?
/// `find as str_find`: the name in the module, then the name it has in this file.
void ParserImpl::parseImportItem() {
    const size_t m = mark();
    parseIdentDef();
    if (cur_ == TK::KwAs && !panic_) {
        bump();
        parseIdentDef();
    }
    close(m, NK::ImportItem);
}

/// exportDecl : 'export' (dataDecl | typeDecl | bracedIdents)
void ParserImpl::parseExportDecl() {
    const size_t m = mark();
    bump();
    if (cur_ == TK::LBrace) {
        parseBracketedIdents(TK::RBrace, NK::BracedIdents);
    } else if (cur_ == TK::KwLet || cur_ == TK::KwVar) {
        parseDataDecl();
    } else {
        parseTypeDecl();
    }
    close(m, NK::ExportDecl);
}

/// usingNamespaceDecl : 'using' 'namespace' identRef
void ParserImpl::parseUsingNamespaceDecl() {
    const size_t m = mark();
    bump();
    expect(TK::KwNamespace);
    parseIdentRef(NK::IdentRef);
    close(m, NK::UsingNamespaceDecl);
}

/// namespaceDecl : 'namespace' identRef '{' SEP? (namespaceItem SEP?)* '}'
/// namespaceItem : namespaceDecl | importDecl | exportDecl | usingNamespaceDecl | funcDecl
///               | typeDecl | dataDecl
void ParserImpl::parseNamespaceDecl() {
    Depth depth(*this);
    const size_t m = mark();
    bump();
    parseIdentRef(NK::IdentRef);
    if (!expect(TK::LBrace) || depth.tooDeep()) {
        return close(m, NK::NamespaceDecl);
    }
    {
        Scope scope(*this, true);
        eat(TK::Semi);
        while (cur_ != TK::RBrace && cur_ != TK::Eof) {
            const uint32_t before = pos_;
            if (cur_ == TK::Semi) {
                error(SyntaxError::Kind::ExtraSemicolon, {}, pos_);
            } else if (startsDecl(cur_) && cur_ != TK::KwModule) {
                parseDecl();
                if (!panic_) {
                    eat(TK::Semi);
                    continue;
                }
            } else {
                unexpected({SyntaxError::Declaration, e(TK::RBrace)});
            }
            recover(true, true);
            if (pos_ == before && cur_ != TK::Eof && cur_ != TK::RBrace) {
                const size_t em = mark();
                stack_.push_back(pos_ | Tree::kTokenBit);
                advance();
                close(em, NK::Error);
            }
            eat(TK::Semi);
        }
    }
    expect(TK::RBrace);
    close(m, NK::NamespaceDecl);
}

/// funcAnno : '@' identRef (angledValues | parentIdents)?
/// `@name` and `@name<args>` are decorators; `@name(a, b)` is an annotation whose arguments are
/// names, `@numeric(reassociate, contract)` (design principles 7.5). Which annotations exist,
/// and where each may be written, is checked when the tree is built (AST builder).
void ParserImpl::parseFuncAnno() {
    const size_t m = mark();
    bump();
    parseIdentRef(NK::IdentRef);
    if (cur_ == TK::LAngle) {
        parseAngledValues();
    } else if (cur_ == TK::LParen) {
        parseBracketedIdents(TK::RParen, NK::ParentIdents);
    }
    close(m, NK::FuncAnno);
}

/// A statement that starts with annotations: a function declaration, or an annotated block
/// (blockStmt's second form; `@numeric(...)` on a block, design principles 7.5). The annotations
/// are read first; a block follows when the next token is '{' or `sync {`, anything else continues
/// the function declaration. After an annotation '{' is always a block, never a struct literal.
void ParserImpl::parseAnnotatedStmt() {
    const size_t m = mark();
    while (cur_ == TK::At) {
        parseFuncAnno();
    }
    if (cur_ == TK::LBrace || (cur_ == TK::KwSync && peek(1) == TK::LBrace)) {
        parseStmtBlock();
        return close(m, NK::BlockStmt);
    }
    parseFuncDeclRest(m);
}

/// funcDecl : funcAnno* ('with' angledParams)? 'export'? implMark? modifiers?
///            'func' (identRef | operatorName) parentParams (':' typeExpr)? stmtBlock
void ParserImpl::parseFuncDecl() {
    const size_t m = mark();
    while (cur_ == TK::At) {
        parseFuncAnno();
    }
    parseFuncDeclRest(m);
}

/// The part of funcDecl after its annotations; `m` marks where the declaration began.
void ParserImpl::parseFuncDeclRest(size_t m) {
    if (eat(TK::KwWith)) {
        parseAngledParams();
    }
    eat(TK::KwExport);
    if (cur_ == TK::KwInner || cur_ == TK::KwOuter) {
        parseImplMark();
    }
    if (isModifier(cur_)) {
        parseModifiers();
    }
    if (!eat(TK::KwFunc)) {
        unexpected({e(TK::KwFunc)});
        return close(m, NK::FuncDecl);
    }
    if (atOperatorName()) {
        parseOperatorName();
    } else {
        parseIdentRef(NK::IdentRef);
    }
    parseParentParams();
    if (eat(TK::Colon)) {
        parseType();
    }
    if (!panic_) {
        parseStmtBlock();
    }
    close(m, NK::FuncDecl);
}

/// operatorName : IDENTIFIER (OPERATOR | '[' ']')
///
/// The IDENTIFIER is the name `operator`; OPERATOR is any operator token. An operator overload
/// (design principles 4.3): `func operator +(a: V, b: V): V`. The notation is provisional, and
/// this production is the whole of it in the parser: `operator` is not a keyword (a function may
/// still be called `operator`), it starts this form only right after `func` and before an operator
/// token other than `(`. Every operator token is taken here; which of them can be overloaded, and
/// with how many parameters, is decided when the declaration is compiled
/// (camel/core/operator_names.h), so that the error can say why.
bool ParserImpl::atOperatorName() const {
    return cur_ == TK::Identifier && tree_.tokens_.text(pos_) == "operator" &&
           isPunctuation(peek(1)) && peek(1) != TK::LParen;
}

void ParserImpl::parseOperatorName() {
    const size_t m = mark();
    bump(); // `operator`
    if (cur_ == TK::LBracket) {
        // Indexing is written with its two brackets: `func operator [](m: M, i: int): T`.
        bump();
        expect(TK::RBracket);
    } else {
        bump();
    }
    close(m, NK::OperatorName);
}

/// dataDecl : ('let' | 'var') carrier (':' typeList)? '=' dataList
///          | 'let' '_' (':' typeList)? '=' dataList
/// carrier  : identList | parentIdents | bracedIdents | bracketIdents
void ParserImpl::parseDataDecl() {
    const size_t m   = mark();
    const bool isLet = cur_ == TK::KwLet;
    bump();
    switch (cur_) {
    case TK::Underscore: {
        // `let _ = e`: the value of `e` is dropped on purpose (design principles 3.9). `_`
        // is not a name; it is taken in this one place, alone, and stands where the single
        // name of a declaration does, so the tree has the shape of `let x = e`.
        if (!isLet) {
            unexpected({e(TK::Identifier)});
            return close(m, NK::DataDecl);
        }
        const size_t list = mark();
        const size_t name = mark();
        bump();
        close(name, NK::IdentDef);
        close(list, NK::IdentList);
    } break;
    case TK::LParen:
        parseBracketedIdents(TK::RParen, NK::ParentIdents);
        break;
    case TK::LBrace:
        parseBracketedIdents(TK::RBrace, NK::BracedIdents);
        break;
    case TK::LBracket:
        parseBracketedIdents(TK::RBracket, NK::BracketIdents);
        break;
    case TK::Identifier:
        parseIdentList();
        break;
    default:
        if (isKeyword(cur_)) {
            error(SyntaxError::Kind::KeywordAsName, {e(TK::Identifier)}, pos_);
        } else {
            unexpected({e(TK::Identifier)});
        }
        return close(m, NK::DataDecl);
    }
    bool typed = false;
    if (eat(TK::Colon)) {
        typed = true;
        parseTypeList(TK::Eof);
    }
    if (!eat(TK::Assign)) {
        if (!typed && !panic_) {
            error(SyntaxError::Kind::MissingInitializer, {e(TK::Colon), e(TK::Assign)}, prev_);
        } else {
            unexpected({e(TK::Assign)});
        }
        return close(m, NK::DataDecl);
    }
    parseDataList(TK::Eof);
    close(m, NK::DataDecl);
}

/// typeDecl : implMark? 'type' identRef
///            ('=' | '<' identDef (',' identDef)* ','? ('>' '=' | '>='))
///            (typeExpr | STRING)
void ParserImpl::parseTypeDecl() {
    const size_t m = mark();
    if (cur_ == TK::KwInner || cur_ == TK::KwOuter) {
        parseImplMark();
    }
    if (!expect(TK::KwType)) {
        return close(m, NK::TypeDecl);
    }
    parseIdentRef(NK::IdentRef);
    if (cur_ == TK::LAngle || cur_ == TK::Lt) {
        bump();
        Scope scope(*this, false);
        parseIdentDef();
        while (cur_ == TK::Comma && peek(1) != TK::Gt && peek(1) != TK::GtEq) {
            bump();
            parseIdentDef();
        }
        eat(TK::Comma);
        if (!eat(TK::GtEq)) {
            if (cur_ == TK::Gt) {
                bump();
                expect(TK::Assign);
            } else {
                unexpected({e(TK::Comma), e(TK::Gt)});
            }
        }
    } else if (!eat(TK::Assign)) {
        unexpected({e(TK::Assign), e(TK::Lt)});
    }
    if (panic_) {
        return close(m, NK::TypeDecl);
    }
    if (cur_ == TK::String) {
        bump();
    } else {
        parseType();
    }
    close(m, NK::TypeDecl);
}

/// retStmt : ('return' | 'throw') dataList
void ParserImpl::parseRetStmt() {
    const size_t m = mark();
    bump();
    parseDataList(TK::Eof);
    close(m, NK::RetStmt);
}

/// implMark : 'inner' | 'outer'
void ParserImpl::parseImplMark() {
    const size_t m = mark();
    bump();
    close(m, NK::ImplMark);
}

/// modifiers : ('atomic' | 'shared' | 'sync' | 'macro')+
void ParserImpl::parseModifiers() {
    const size_t m = mark();
    while (isModifier(cur_)) {
        bump();
    }
    close(m, NK::Modifiers);
}

/// identDef : IDENTIFIER
void ParserImpl::parseIdentDef() { expectName(NK::IdentDef); }

/// identRef : (IDENTIFIER '::')* IDENTIFIER
/// `kind` is IdentRef where a name is required, RefData for an operand, RefType for a type.
void ParserImpl::parseIdentRef(NK kind) {
    if (cur_ != TK::Identifier) {
        if (isKeyword(cur_)) {
            error(SyntaxError::Kind::KeywordAsName, {e(TK::Identifier)}, pos_);
        } else {
            unexpected({e(TK::Identifier)});
        }
        return;
    }
    const size_t m = mark();
    bump();
    while (cur_ == TK::ColonColon && peek(1) != TK::Lt && peek(1) != TK::LAngle) {
        bump();
        if (cur_ != TK::Identifier) {
            if (isKeyword(cur_)) {
                error(SyntaxError::Kind::KeywordAsName, {e(TK::Identifier)}, pos_);
            } else {
                unexpected({e(TK::Identifier)});
            }
            break;
        }
        bump();
    }
    close(m, kind);
}

/// identList : identDef (',' identDef)*
/// Unbracketed (`let a, b = ...`): every comma is followed by a name.
void ParserImpl::parseIdentList() {
    const size_t m = mark();
    parseIdentDef();
    while (cur_ == TK::Comma) {
        bump();
        parseIdentDef();
    }
    close(m, NK::IdentList);
}

/// parentIdents : '(' identList? ','? ')'
/// bracedIdents : '{' identList? ','? '}'
/// bracketIdents : '[' identList? ','? ']'
void ParserImpl::parseBracketedIdents(TK closer, NK kind) {
    const size_t m = mark();
    bump();
    {
        Scope scope(*this, false);
        if (cur_ != closer && cur_ != TK::Comma) {
            const size_t lm = mark();
            parseIdentDef();
            while (cur_ == TK::Comma && peek(1) != closer) {
                bump();
                parseIdentDef();
            }
            close(lm, NK::IdentList);
        }
        eat(TK::Comma);
    }
    expectListEnd(closer);
    close(m, kind);
}

/// keyParamPair : ('var' | 'const')? identDef ':' typeExpr ('=' dataExpr)?
void ParserImpl::parseKeyParamPair() {
    const size_t m = mark();
    if (!eat(TK::KwVar)) {
        eat(TK::KwConst);
    }
    parseIdentDef();
    if (!panic_ && expect(TK::Colon)) {
        parseType();
        if (eat(TK::Assign)) {
            parseExpr();
        }
    }
    close(m, NK::KeyParamPair);
}

/// pairedParams : keyParamPair (',' keyParamPair)*
void ParserImpl::parsePairedParams(TK closer) {
    const size_t m = mark();
    parseKeyParamPair();
    while (cur_ == TK::Comma && peek(1) != closer && !panic_) {
        bump();
        parseKeyParamPair();
    }
    close(m, NK::PairedParams);
}

/// parentParams : '(' pairedParams? ','? ')'
void ParserImpl::parseParentParams() {
    const size_t m = mark();
    if (!expect(TK::LParen)) {
        return;
    }
    {
        Scope scope(*this, false);
        if (cur_ == TK::Identifier || cur_ == TK::KwVar || cur_ == TK::KwConst) {
            parsePairedParams(TK::RParen);
        } else if (cur_ != TK::RParen && cur_ != TK::Comma) {
            unexpected({e(TK::Comma), e(TK::RParen), e(TK::KwVar), e(TK::Identifier)});
        }
        eat(TK::Comma);
    }
    if (!panic_) {
        expectListEnd(TK::RParen);
    }
    close(m, NK::ParentParams);
}

/// angledParams : '<' pairedParams? ','? '>'
/// The `<` of a parameter list need not touch anything (`with <T: type>`).
void ParserImpl::parseAngledParams() {
    if (cur_ != TK::Lt && cur_ != TK::LAngle) {
        unexpected({e(TK::Lt)});
        return;
    }
    const size_t m      = mark();
    const uint32_t open = pos_;
    bump();
    {
        Scope scope(*this, false, open);
        if (cur_ != TK::Gt && cur_ != TK::Comma) {
            parsePairedParams(TK::Gt);
        }
        eat(TK::Comma);
        if (!panic_ && cur_ != TK::Gt) {
            unexpected({e(TK::Comma), e(TK::Gt)});
        }
    }
    eat(TK::Gt);
    close(m, NK::AngledParams);
}

} // namespace camel::parse::syntax
