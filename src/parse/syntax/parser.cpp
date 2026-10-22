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

#include <algorithm>
#include <array>

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
                (topLevel ? startsDecl(cur_) || atRuleDecl(0) || atAnalysisDecl(0) : startsStmt(cur_))) {
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
    case TK::KwSync:
    case TK::At:
    case TK::Hash:
        return true;
    case TK::Identifier:
        return atTagDecl(0) || atResourceDecl(0);
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
    case TK::KwReturn:
    case TK::KwThrow:
    case TK::At:
    case TK::Hash:
    case TK::KwStatic:
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
        } else if (startsDecl(cur_) || atRuleDecl(0) || atAnalysisDecl(0)) {
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
///      | tagDecl | resourceDecl | dataDecl | funcDecl | typeDecl | ruleDecl
/// Decided on the first token; after `export` on the next one (`export let`, `export type`, ...).
/// The word `tag` is not reserved: a declaration starts with it only when a tag name follows
/// (`atTagDecl`), so a value called `tag` still reads as a value.
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
    case TK::Hash:
        // A tagged function, type or block declaration (the tags come first).
        return parseAnnotatedStmt();
    case TK::KwExport: {
        const TK next = peek(1);
        if (atAnalysisDecl(1)) {
            return parseAnalysisDecl();
        }
        if (atRuleDecl(1)) {
            return parseRuleDecl();
        }
        if (atTagDecl(1)) {
            return parseTagDecl();
        }
        if (atResourceDecl(1)) {
            return parseResourceDecl();
        }
        if (next == TK::KwLet || next == TK::KwVar || next == TK::KwType || next == TK::LBrace) {
            return parseExportDecl();
        }
        return parseFuncDecl();
    }
    default:
        if (atTagDecl(0)) {
            return parseTagDecl();
        }
        if (atResourceDecl(0)) {
            return parseResourceDecl();
        }
        if (atAnalysisDecl(0)) {
            return parseAnalysisDecl();
        }
        if (atRuleDecl(0)) {
            return parseRuleDecl();
        }
        return parseFuncDecl();
    }
}

/// stmt : dataDecl | funcDecl | typeDecl | dataExpr | usingNamespaceDecl | retStmt
///      | blockStmt | staticForExpr
/// blockStmt : 'wait'? stmtBlock | funcAnno+ stmtBlock
/// Decided on the first token, except:
///   - modifiers (`sync`) are skipped to see what they modify: `func` (a declaration), `{`
///     after a lone `sync` (a block), anything else (a lambda expression);
///   - `{` is a struct literal when it is `{}`, `{ name: ...` or `{ ...`, a block otherwise;
///     the same after `wait`.
/// `static for` is a statement, not a value (H51 step 3): it parses here only, so writing it
/// where a value is needed is a syntax error rather than a silently dropped value.
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
    case TK::Hash:
        // `#tag func ...`, `#tag type ...` or `#tag { ... }`: the tags are read first, then
        // what they are written on (parseAnnotatedStmt).
        return parseAnnotatedStmt();
    case TK::KwSync: {
        const uint32_t n = afterModifiers();
        if (peek(n) == TK::KwFunc) {
            return parseFuncDecl();
        }
        if (n == 1 && peek(1) == TK::LBrace) {
            return parseStmtBlock();
        }
        return parseExpr();
    }
    case TK::KwStatic:
        // `static for` is a statement; `static loop` is an expression with a value, and is
        // read by parseExpr (`static` never starts anything else).
        return peek(1) == TK::KwLoop ? parseStaticLoop() : parseStaticFor();
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
/// The block is the first alternative: `{}` is the empty block here. `{ name: ...`, `{ ...` and
/// `{ static for ...` are expressions: a struct literal or a struct comprehension (H51 step 4).
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
/// `export resource` is not this: the word after `export` decides, and `parseDecl` reads it as a
/// resource declaration that carries its own `export` (see `resourceDecl`).
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
///               | typeDecl | dataDecl | resourceDecl
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
/// `@name` and `@name<args>` are decorators. `@name(a, b)` is the removed spelling of the
/// numeric-permission annotation (design principles 7.2 made permissions tags); it is parsed so
/// that the AST builder can report it where it is written, and grants nothing.
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

/// A statement that starts with annotations or tag uses: a function declaration, a type
/// declaration, a rule declaration (`#requires<...> rule f(...)`), or an annotated/tagged block
/// (blockStmt's second form; an `@name(args)` annotation, the removed spelling of the numeric
/// permission annotation; `#tag { ... }` on
/// a block, 6.2). They are read first; a block follows when the next token is '{' or `sync {`, a
/// type declaration when it is 'type', a rule when the tags are followed by `rule`, anything else
/// continues the function declaration. After a tag use '{' is always a block, never a struct
/// literal.
void ParserImpl::parseAnnotatedStmt() {
    const size_t m = mark();
    parseFrontMatter();
    if (cur_ == TK::KwExport && atRuleDecl(1)) {
        return parseRuleDeclRest(m);
    }
    if (atRuleDecl(0)) {
        return parseRuleDeclRest(m);
    }
    if (cur_ == TK::LBrace || (cur_ == TK::KwSync && peek(1) == TK::LBrace)) {
        parseStmtBlock();
        return close(m, NK::BlockStmt);
    }
    if (cur_ == TK::KwType) {
        return parseTypeDeclRest(m);
    }
    parseFuncDeclRest(m);
}

/// frontMatter : (funcAnno | tagUse)*
void ParserImpl::parseFrontMatter() {
    while (cur_ == TK::At || cur_ == TK::Hash) {
        if (cur_ == TK::At) {
            parseFuncAnno();
        } else {
            parseTagUse();
        }
    }
}

/// funcDecl : frontMatter ('with' angledParams)? 'export'? modifiers?
///            'func' (identRef | operatorName) typeParams? parentParams (':' typeExpr)?
///            resourceClause? stmtBlock?
///
/// Without a body the declaration stands for a C++ operator of the module the name is bound in
/// (design principles 6.4): it has no block, and what it declares is its signature. The
/// `uses`/`pure` clause after the result type says which resources a call touches (design
/// effects plan 3.1).
void ParserImpl::parseFuncDecl() {
    const size_t m = mark();
    parseFrontMatter();
    parseFuncDeclRest(m);
}

/// tagDecl : 'export'? 'tag' identDef angledParams? 'on' tagPositions ('read_by' tagReaders)?
///         | 'export'? 'tag' identDef '=' bracketIdents
///
/// A tag is a name in the namespace of functions and types (design principles 6.2): the
/// positions after `on` say where it may be written, `read_by` names the passes that read it,
/// and the second form makes it the group of the tags in the list (`tag finite_math = [no_nans,
/// no_infs]`). `tag`, `on` and `read_by` are contextual: they are not reserved words, and a
/// declaration starts with `tag` only when a name, `<` or `=` follows it.
void ParserImpl::parseTagDecl() {
    const size_t m = mark();
    eat(TK::KwExport);
    bump(); // `tag`
    expectName(NK::IdentDef);
    if (eat(TK::Assign)) {
        parseBracketedIdents(TK::RBracket, NK::BracketIdents);
        close(m, NK::TagDecl);
        return;
    }
    if (cur_ == TK::Lt || cur_ == TK::LAngle) {
        parseAngledParams();
    }
    if (!atWord(0, "on")) {
        unexpected({e(TK::Identifier)});
        return close(m, NK::TagDecl);
    }
    bump(); // `on`
    parseTagPositions();
    if (atWord(0, "read_by")) {
        bump();
        parseTagReaders();
    }
    close(m, NK::TagDecl);
}

/// Whether a tag declaration starts at offset `n`: `tag` followed by a name and then `<`, `=`,
/// or the contextual `on`. A `tag` followed by anything else is an ordinary name.
bool ParserImpl::atTagDecl(uint32_t n) const {
    if (!atWord(n, "tag") || peek(n + 1) != TK::Identifier) {
        return false;
    }
    const TK third = peek(n + 2);
    return third == TK::Lt || third == TK::LAngle || third == TK::Assign || atWord(n + 2, "on");
}

/// tagPositions : tagPositionName (',' tagPositionName)* ','?
void ParserImpl::parseTagPositions() {
    const size_t m = mark();
    parseTagPositionName();
    while (cur_ == TK::Comma && !atWord(1, "read_by") &&
           (peek(1) == TK::Identifier || isKeyword(peek(1))) && !panic_) {
        bump();
        parseTagPositionName();
    }
    eat(TK::Comma);
    close(m, NK::TagPositions);
}

/// tagPositionName : IDENTIFIER
/// The words a tag's `on` lists: `func`, `type`, `field`, `nodes`, `param` (design principles
/// 6.2). The first three are keywords of the language, the last two are not; after `on` all
/// five are read as names, and one that is none of them is reported by the AST builder.
void ParserImpl::parseTagPositionName() {
    static const std::array<std::string_view, 5> kPositions = {
        "func", "type", "field", "nodes", "param"};
    const bool name = cur_ == TK::Identifier &&
                      std::ranges::find(kPositions, tree_.tokens_.text(pos_)) !=
                          kPositions.end();
    const bool keyword = isKeyword(cur_) &&
                         std::ranges::find(kPositions, tree_.tokens_.text(pos_)) !=
                             kPositions.end();
    if (!name && !keyword) {
        unexpected({e(TK::Identifier)});
        return;
    }
    const size_t m = mark();
    bump();
    close(m, NK::IdentDef);
}

/// tagReaders : identRef (',' identRef)* ','?
void ParserImpl::parseTagReaders() {
    const size_t m = mark();
    parseIdentRef(NK::IdentRef);
    while (cur_ == TK::Comma && peek(1) == TK::Identifier && !panic_) {
        bump();
        parseIdentRef(NK::IdentRef);
    }
    eat(TK::Comma);
    close(m, NK::TagReaders);
}

/// tagUse : '#' (tagRef | '[' (tagRef (',' tagRef)* ','?)? ']')
/// The `#` forms of design principles 6.2: `#name`, `#name<args>` and the group
/// `#[a, b<v>]`. `#` used to start a comment; it is now always a tag (2.1).
void ParserImpl::parseTagUse() {
    const size_t m = mark();
    bump(); // `#`
    if (cur_ == TK::LBracket) {
        bump();
        {
            Scope scope(*this, false);
            if (cur_ != TK::RBracket && !panic_) {
                parseTagRef();
                while (cur_ == TK::Comma && peek(1) != TK::RBracket && !panic_) {
                    bump();
                    parseTagRef();
                }
            }
            eat(TK::Comma);
        }
        if (!panic_) {
            expect(TK::RBracket);
        }
    } else if (!panic_) {
        parseTagRef();
    }
    close(m, NK::TagUse);
}

/// tagRef : identRef angledValues?
void ParserImpl::parseTagRef() {
    const size_t m = mark();
    parseIdentRef(NK::IdentRef);
    if (cur_ == TK::LAngle) {
        parseAngledValues();
    }
    close(m, NK::TagRef);
}

/// The part of funcDecl after its annotations; `m` marks where the declaration began.
void ParserImpl::parseFuncDeclRest(size_t m) {
    if (eat(TK::KwWith)) {
        parseAngledParams();
    }
    eat(TK::KwExport);
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
    if (cur_ == TK::ColonColon && (peek(1) == TK::Lt || peek(1) == TK::LAngle)) {
        parseTypeParams();
    }
    parseParentParams(true, true);
    if (eat(TK::Colon)) {
        parseType();
    }
    if (atWord(0, "uses") || atWord(0, "pure")) {
        parseResourceClause();
    }
    // A function written without a body is the declaration of an operator of a C++ module: the
    // signature is the whole of it (design principles 6.4, handover H19 step 1). A block is
    // still what follows a signature in every other case, so the next token decides; `sync`
    // belongs to the block and is read with it (parseStmtBlock).
    const bool hasBlock =
        cur_ == TK::LBrace || (cur_ == TK::KwSync && peek(1) == TK::LBrace);
    if (!panic_ && hasBlock) {
        parseStmtBlock();
    }
    close(m, NK::FuncDecl);
}

/// typeParams : '::' '<' identDef (',' identDef)* ','? '>'
///
/// The type parameters of a generic declaration, written at its head right after the name
/// (design principles 5.5): `func first::<T>(xs: T[]): T`. The spelling is the one a call site
/// uses for explicit type arguments (`first::<int>(xs)`), so whether a declaration is generic is
/// visible in its head; without `::<...>` it is not generic. `rule` and `fact` declare theirs the
/// same way. A dimension name still binds where a shape first writes it (5.4) and may be listed
/// here too, which is why the list can name one.
void ParserImpl::parseTypeParams() {
    const size_t m = mark();
    bump(); // `::`
    if (!panic_ && cur_ != TK::Lt && cur_ != TK::LAngle) {
        unexpected({e(TK::Lt)});
        return close(m, NK::TypeParams);
    }
    bump(); // `<`
    {
        Scope scope(*this, false);
        parseIdentDef();
        while (!panic_ && eat(TK::Comma) && cur_ == TK::Identifier) {
            parseIdentDef();
        }
    }
    if (!panic_ && !eat(TK::Gt)) {
        unexpected({e(TK::Comma), e(TK::Gt)});
    }
    close(m, NK::TypeParams);
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

/// typeDecl : frontMatter? 'type' identRef
///            ('=' | '<' identDef (',' identDef)* ','? ('>' '=' | '>='))
///            (typeExpr | STRING)
void ParserImpl::parseTypeDecl() {
    const size_t m = mark();
    parseFrontMatter();
    parseTypeDeclRest(m);
}

/// The part of typeDecl after the tags that may precede it; `m` marks where it began.
void ParserImpl::parseTypeDeclRest(size_t m) {
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

/// resourceDecl : 'export'? 'resource' identDef ('=' bracketIdents | 'unordered')?
///
/// A resource declaration (design effects plan 3.1, design principles 3.3): `resource fs` names
/// an ambient resource of the module, `resource io = [stdout, fs]` a group of resources and other
/// groups, and `resource net unordered` says the accesses of a call may be reordered (step 8
/// gives that meaning). `resource` and `unordered` are contextual: they are not reserved words,
/// and a declaration starts with `resource` only when a name follows it.
void ParserImpl::parseResourceDecl() {
    const size_t m = mark();
    eat(TK::KwExport);
    bump(); // `resource`
    expectName(NK::IdentDef);
    if (eat(TK::Assign)) {
        parseBracketedIdents(TK::RBracket, NK::BracketIdents);
    } else if (atWord(0, "unordered")) {
        bump();
    }
    close(m, NK::ResourceDecl);
}

/// Whether a resource declaration starts at offset `n`: `resource` followed by a name.
bool ParserImpl::atResourceDecl(uint32_t n) const {
    return atWord(n, "resource") && peek(n + 1) == TK::Identifier;
}

/// retStmt : ('return' | 'throw') dataList
void ParserImpl::parseRetStmt() {
    const size_t m = mark();
    bump();
    parseDataList(TK::Eof);
    close(m, NK::RetStmt);
}

/// modifiers : 'sync'+
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

/// keyParamPair : ('var' | 'const')? identDef (':' typeExpr ('=' dataExpr)?)?
///              | '..' identDef (':' typeExpr)?
///              | 'constas' identDef (':' typeExpr)?
/// A rest parameter is written untyped and last in a function declaration's parameter list
/// (design principles 5.5). A `constas` parameter is written first in that list and is a
/// compile-time value, not a runtime parameter (design principles 5.4). A type written after a
/// rest parameter, or left out of a `constas` one, is parsed so that the semantic stage can
/// report it; the parameter lists of a lambda, a function type and a rule take neither.
void ParserImpl::parseKeyParamPair(bool allowRest, bool allowConstas) {
    const size_t m = mark();
    if (allowRest && cur_ == TK::DotDot) {
        bump();
        parseIdentDef();
        if (eat(TK::Colon)) {
            parseType();
        }
        close(m, NK::KeyParamPair);
        return;
    }
    if (allowConstas && cur_ == TK::KwConstas) {
        // `constas n: int` binds a compile-time value in a function declaration (design
        // principles 5.4). The type is required, but leaving it out is parsed as well, so that
        // the semantic stage reports it with a diagnostic instead of a bare syntax error.
        bump();
        parseIdentDef();
        if (eat(TK::Colon)) {
            parseType();
        }
        close(m, NK::KeyParamPair);
        return;
    }
    if (!eat(TK::KwVar)) {
        eat(TK::KwConst);
    }
    parseIdentDef();
    // Only a lambda may leave the type out (see lambdaParams_); a declaration and a function
    // type require it, and report the `:` that is missing.
    if (!panic_ && (lambdaParams_ ? eat(TK::Colon) : expect(TK::Colon))) {
        parseType();
        if (eat(TK::Assign)) {
            parseExpr();
        }
    }
    close(m, NK::KeyParamPair);
}

/// pairedParams : keyParamPair (',' keyParamPair)*
void ParserImpl::parsePairedParams(TK closer, bool allowRest, bool allowConstas) {
    const size_t m = mark();
    parseKeyParamPair(allowRest, allowConstas);
    while (cur_ == TK::Comma && peek(1) != closer && !panic_) {
        bump();
        parseKeyParamPair(allowRest, allowConstas);
    }
    close(m, NK::PairedParams);
}

/// parentParams : '(' pairedParams? ','? ')'
void ParserImpl::parseParentParams(bool allowRest, bool allowConstas) {
    const size_t m = mark();
    if (!expect(TK::LParen)) {
        return;
    }
    {
        Scope scope(*this, false);
        if (cur_ == TK::Identifier || cur_ == TK::KwVar || cur_ == TK::KwConst ||
            (allowRest && cur_ == TK::DotDot) || (allowConstas && cur_ == TK::KwConstas)) {
            parsePairedParams(TK::RParen, allowRest, allowConstas);
        } else if (cur_ != TK::RParen && cur_ != TK::Comma) {
            unexpected(
                {e(TK::Comma),
                 e(TK::RParen),
                 e(TK::KwVar),
                 e(TK::KwConstas),
                 e(TK::Identifier)});
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
            parsePairedParams(TK::Gt, false, false);
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
