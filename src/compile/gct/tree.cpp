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
 * Created: Oct. 13, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The graph construction tree: its arena, the node factory, appending children, copying a
 * subtree and the text dump (camel/compile/gct/tree.h, node.h).
 */

#include "camel/compile/gct/tree.h"
#include "camel/core/operator_semantics.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <new>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace camel::compile::gct {

namespace {

/// Everything in the arena is made of pointers and 64-bit integers.
constexpr size_t kArenaAlign = alignof(uint64_t);

size_t alignUp(size_t bytes) { return (bytes + kArenaAlign - 1) & ~(kArenaAlign - 1); }

/// The capacity of a child array that holds `count` children: none, two, then doubling.
size_t childCapacity(size_t count) {
    return count == 0 ? 0 : std::max<size_t>(2, std::bit_ceil(count));
}

std::string pointerToHex(const void *ptr) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
       << reinterpret_cast<uintptr_t>(ptr);
    return ss.str();
}

} // namespace

// =============================================================================
// Arena
// =============================================================================

Tree::~Tree() {
    while (chunks_ != nullptr) {
        Chunk *older = chunks_->next;
        ::operator delete(chunks_, std::align_val_t(kChunkBytes));
        chunks_ = older;
    }
}

void Tree::addUsings(const Node *node, const std::vector<Reference> &usings) {
    if (!usings.empty()) {
        auto &known = usings_[node];
        known.insert(known.end(), usings.begin(), usings.end());
    }
}

void Tree::setNamespaceOf(const Node *node, const std::vector<std::string> &path) {
    if (!path.empty()) {
        namespaces_[node] = path;
    }
}

const std::vector<std::string> *Tree::namespaceOf(const Node *node) const {
    const auto it = namespaces_.find(node);
    return it == namespaces_.end() ? nullptr : &it->second;
}

const std::vector<Reference> *Tree::usingsOf(const Node *node) const {
    const auto it = usings_.find(node);
    return it == usings_.end() ? nullptr : &it->second;
}

void Tree::addTagUses(const Node *node, std::vector<TagUse> uses) {
    if (uses.empty()) {
        return;
    }
    auto &known = tagUses_[node];
    known.insert(
        known.end(), std::make_move_iterator(uses.begin()), std::make_move_iterator(uses.end()));
}

const std::vector<TagUse> *Tree::tagUsesOf(const Node *node) const {
    const auto it = tagUses_.find(node);
    return it == tagUses_.end() ? nullptr : &it->second;
}

void Tree::addFieldTagUses(const Node *decl, size_t field, std::vector<TagUse> uses) {
    if (uses.empty()) {
        return;
    }
    auto &known = fieldTagUses_[decl][field];
    known.insert(
        known.end(), std::make_move_iterator(uses.begin()), std::make_move_iterator(uses.end()));
}

const std::map<size_t, std::vector<TagUse>> *Tree::fieldTagUsesOf(const Node *decl) const {
    const auto it = fieldTagUses_.find(decl);
    return it == fieldTagUses_.end() ? nullptr : &it->second;
}

void Tree::setLiteralValue(const Node *node, literal::Value value) {
    literalValues_[node] = std::move(value);
}

const literal::Value *Tree::literalValueOf(const Node *node) const {
    const auto it = literalValues_.find(node);
    return it == literalValues_.end() ? nullptr : &it->second;
}

void Tree::setLiteralElements(const data_ptr_t &data, std::vector<LiteralElement> elements) {
    if (data != nullptr) {
        literalElements_[data.get()] = std::move(elements);
    }
}

const std::vector<Tree::LiteralElement> *Tree::literalElementsOf(const data_ptr_t &data) const {
    if (data == nullptr) {
        return nullptr;
    }
    const auto it = literalElements_.find(data.get());
    return it == literalElements_.end() ? nullptr : &it->second;
}

void Tree::setUnwrittenParams(const Node *func, std::vector<UnwrittenParam> params) {
    if (func != nullptr && !params.empty()) {
        unwrittenParams_[func] = std::move(params);
    }
}

const std::vector<Tree::UnwrittenParam> *Tree::unwrittenParamsOf(const Node *func) const {
    if (func == nullptr) {
        return nullptr;
    }
    const auto it = unwrittenParams_.find(func);
    return it == unwrittenParams_.end() ? nullptr : &it->second;
}

Tree *Tree::of(const Node *node) {
    const auto address = reinterpret_cast<uintptr_t>(node);
    return reinterpret_cast<const Chunk *>(address & ~(static_cast<uintptr_t>(kChunkBytes) - 1))
        ->owner;
}

void *Tree::allocate(size_t bytes) {
    bytes = alignUp(bytes);
    if (bytes > kLargeBytes) {
        largeBlocks_.push_back(std::make_unique<uint64_t[]>(bytes / sizeof(uint64_t)));
        arenaBytes_ += bytes;
        return largeBlocks_.back().get();
    }
    if (chunks_ == nullptr || chunks_->used + bytes > kChunkBytes) {
        auto *chunk =
            static_cast<Chunk *>(::operator new(kChunkBytes, std::align_val_t(kChunkBytes)));
        chunk->owner = this;
        chunk->next  = chunks_;
        chunk->used  = alignUp(sizeof(Chunk));
        chunks_      = chunk;
        arenaBytes_ += kChunkBytes;
    }
    void *memory = reinterpret_cast<std::byte *>(chunks_) + chunks_->used;
    chunks_->used += bytes;
    return memory;
}

Node *Tree::makeNode(NodeKind kind) {
    ++nodeCount_;
    return new (allocate(sizeof(Node))) Node(kind);
}

const std::string *Tree::intern(std::string_view name) {
    if (const auto known = names_.find(name); known != names_.end()) {
        return &*known;
    }
    return &*names_.emplace(name).first;
}

const std::string *Tree::internReference(std::string_view name) {
    // What Reference accepts: a last part, after any `::`, that is not empty.
    if (name.empty() || name.ends_with("::")) {
        throw std::runtime_error("Invalid reference name: " + std::string(name));
    }
    return intern(name);
}

void Tree::append(Node &parent, Node *child) {
    ASSERT((child->flags_ & Node::kAttached) == 0, "Node already has a parent");
    const size_t count = parent.childCount_;
    if (count == childCapacity(count)) {
        auto **grown = static_cast<Node **>(allocate(childCapacity(count + 1) * sizeof(Node *)));
        if (count != 0) {
            std::memcpy(grown, parent.children_, count * sizeof(Node *));
        }
        parent.children_ = grown;
    }
    parent.children_[count] = child;
    parent.childCount_      = static_cast<uint32_t>(count + 1);
    child->flags_ |= Node::kAttached;
}

Node &Node::operator<<(node_ptr_t child) {
    if (child != nullptr) {
        Tree::of(this)->append(*this, child);
    }
    return *this;
}

// =============================================================================
// Node factory
// =============================================================================

node_ptr_t Tree::makeData(const data_ptr_t &data) {
    ASSERT(data != nullptr, "A DATA node cannot be made of a null data pointer");
    Node *node          = makeNode(NodeKind::DATA);
    node->payload_.data = &constants_.emplace_back(data);
    return node;
}

node_ptr_t Tree::makeType(Type *type) {
    Node *node          = makeNode(NodeKind::TYPE);
    node->payload_.type = type;
    return node;
}

node_ptr_t Tree::makeDecl(const std::string &name, bool isFunc) {
    Node *node          = makeNode(NodeKind::DECL);
    node->payload_.name = internReference(name);
    node->flags_        = isFunc ? Node::kFlagA : 0;
    return node;
}

node_ptr_t Tree::makeFunc(const std::string &name) {
    Node *node          = makeNode(NodeKind::FUNC);
    node->payload_.name = intern(name);
    return node;
}

node_ptr_t Tree::makeNRef(const std::string &name) {
    Node *node          = makeNode(NodeKind::NREF);
    node->payload_.name = internReference(name);
    return node;
}

node_ptr_t Tree::makeDRef(const std::string &name) {
    Node *node          = makeNode(NodeKind::DREF);
    node->payload_.name = internReference(name);
    return node;
}

node_ptr_t Tree::makeDRef(OperId oper, const std::string &name) {
    Node *node          = makeDRef(name);
    operRefs_[node]     = oper;
    return node;
}

std::optional<OperId> Tree::operRefOf(const Node *node) const {
    const auto it = operRefs_.find(node);
    if (it == operRefs_.end()) {
        return std::nullopt;
    }
    return it->second;
}

node_ptr_t Tree::makeVari() { return makeNode(NodeKind::VARI); }

node_ptr_t Tree::makeWait() { return makeNode(NodeKind::WAIT); }

node_ptr_t Tree::makeLink(size_t argCount) {
    Node *node            = makeNode(NodeKind::LINK);
    node->payload_.number = argCount;
    return node;
}

node_ptr_t Tree::makeWith(size_t argCount) {
    Node *node            = makeNode(NodeKind::WITH);
    node->payload_.number = argCount;
    return node;
}

node_ptr_t Tree::makeTypeApplication() { return makeNode(NodeKind::TAPP); }

node_ptr_t Tree::makeAccs(size_t index) {
    Node *node            = makeNode(NodeKind::ACCS);
    node->payload_.number = index;
    node->flags_          = Node::kFlagA;
    return node;
}

node_ptr_t Tree::makeAccs(const std::string &key) {
    Node *node          = makeNode(NodeKind::ACCS);
    node->payload_.name = intern(key);
    return node;
}

node_ptr_t Tree::makeBrch(BranchKind kind) {
    Node *node = makeNode(NodeKind::BRCH);
    node->tag_ = static_cast<uint16_t>(kind);
    return node;
}

node_ptr_t Tree::makeCase(CaseType type, const std::string &binding) {
    Node *node          = makeNode(NodeKind::CASE);
    node->payload_.name = intern(binding);
    node->tag_          = static_cast<uint16_t>(type);
    return node;
}

node_ptr_t Tree::makeTypeMatch() { return makeNode(NodeKind::TMTCH); }

node_ptr_t Tree::makeTypeCase(uint16_t patterns) {
    Node *node = makeNode(NodeKind::TCASE);
    node->tag_ = patterns;
    return node;
}

node_ptr_t Tree::makeStaticFor(
    const std::string &valueName, const std::string &typeName, bool byIndices) {
    Node *node = makeNode(NodeKind::SFOR);
    auto *list = static_cast<Node::NameList *>(allocate(Node::NameList::bytesFor(2)));
    list->count    = 2;
    list->items[0] = intern(valueName);
    list->items[1] = intern(typeName);
    node->payload_.names = list;
    if (byIndices) {
        node->flags_ |= Node::kFlagA;
    }
    return node;
}

node_ptr_t Tree::makeStaticLoop(
    const std::vector<std::string> &state, const std::string &valueName,
    const std::string &typeName, bool byIndices) {
    Node *node = makeNode(NodeKind::SLOOP);
    const size_t count = state.size() + 2;
    auto *list = static_cast<Node::NameList *>(allocate(Node::NameList::bytesFor(count)));
    list->count = count;
    for (size_t i = 0; i < state.size(); ++i) {
        list->items[i] = intern(state[i]);
    }
    list->items[count - 2] = intern(valueName);
    list->items[count - 1] = intern(typeName);
    node->payload_.names = list;
    if (byIndices) {
        node->flags_ |= Node::kFlagA;
    }
    return node;
}

node_ptr_t Tree::makeStaticAccess() { return makeNode(NodeKind::SACC); }

node_ptr_t Tree::makeSpreadArg() { return makeNode(NodeKind::SPRD); }

node_ptr_t Tree::makeComprehension(bool isStruct) {
    Node *node = makeNode(NodeKind::SCOMP);
    node->tag_ = static_cast<uint16_t>(isStruct ? 0 : 1);
    return node;
}

node_ptr_t Tree::makeEnumCase(
    const std::string &label, const std::vector<std::string> &fields,
    const std::vector<std::string> &bindings, bool structShape) {
    ASSERT(
        fields.empty() || fields.size() == bindings.size(),
        "Every field of a struct pattern has a binding entry");
    Node *node   = makeNode(NodeKind::CASE);
    node->tag_   = static_cast<uint16_t>(CaseType::Enum);
    node->flags_ = static_cast<uint8_t>(structShape ? Node::kFlagA : 0);
    auto *data = static_cast<Node::EnumCase *>(allocate(Node::EnumCase::bytesFor(bindings.size())));
    auto **names   = reinterpret_cast<const std::string **>(data + 1);
    data->label    = intern(label);
    data->count    = bindings.size();
    data->fields   = names;
    data->bindings = names + bindings.size();
    for (size_t i = 0; i < bindings.size(); ++i) {
        names[i]                   = fields.empty() ? intern("") : intern(fields[i]);
        names[bindings.size() + i] = intern(bindings[i]);
    }
    node->payload_.enumCase = data;
    return node;
}

node_ptr_t Tree::makeCast(Type *targetType, bool annotation) {
    Node *node          = makeNode(NodeKind::CAST);
    node->payload_.type = targetType;
    node->flags_        = annotation ? Node::kFlagA : 0;
    return node;
}

node_ptr_t Tree::makeAnno() { return makeNode(NodeKind::ANNO); }

node_ptr_t Tree::makeExit(ExitType type) {
    Node *node = makeNode(NodeKind::EXIT);
    node->tag_ = static_cast<uint16_t>(type);
    return node;
}

node_ptr_t Tree::makeExec(bool synced, bool operands) {
    Node *node = makeNode(NodeKind::EXEC);
    node->flags_ =
        static_cast<uint8_t>((synced ? Node::kFlagA : 0) | (operands ? Node::kFlagB : 0));
    return node;
}

node_ptr_t Tree::makeExpt(const std::vector<Reference> &exports) {
    Node *node  = makeNode(NodeKind::EXPT);
    auto *list  = static_cast<Node::NameList *>(allocate(Node::NameList::bytesFor(exports.size())));
    list->count = exports.size();
    for (size_t i = 0; i < exports.size(); ++i) {
        list->items[i] = intern(exports[i].toString());
    }
    node->payload_.names = list;
    return node;
}

node_ptr_t Tree::makeLoop(
    camel::core::LoopKind kind, const std::vector<std::string> &state, const std::string &element) {
    const bool hasElement = camel::core::loopHasIterable(kind);
    const size_t count    = state.size() + (hasElement ? 1 : 0);
    Node *node            = makeNode(NodeKind::LOOP);
    node->tag_            = static_cast<uint16_t>(kind);
    auto *list  = static_cast<Node::NameList *>(allocate(Node::NameList::bytesFor(count)));
    list->count = count;
    for (size_t i = 0; i < state.size(); ++i) {
        list->items[i] = intern(state[i]);
    }
    if (hasElement) {
        list->items[count - 1] = intern(element);
    }
    node->payload_.names = list;
    return node;
}

node_ptr_t Tree::clone(const Node *node) {
    Node *copy    = makeNode(node->kind_);
    copy->flags_  = static_cast<uint8_t>(node->flags_ & ~Node::kAttached);
    copy->tag_    = node->tag_;
    copy->origin_ = node->origin_;
    // What the payload points to is taken into this tree, so that the copy does not depend
    // on the tree of the original for more than its own lifetime requires.
    switch (node->kind_) {
    case NodeKind::DATA:
        copy->payload_.data = &constants_.emplace_back(*node->payload_.data);
        // A literal keeps its exact value through a copy: the copy is the same literal.
        if (const literal::Value *exact = Tree::of(node)->literalValueOf(node)) {
            literalValues_[copy] = *exact;
        }
        // A composite literal keeps the exact value of its elements too (the data object is
        // shared with the original, so the entry is keyed by the same pointer).
        if (const auto *elements = Tree::of(node)->literalElementsOf(*node->payload_.data)) {
            literalElements_[copy->payload_.data->get()] = *elements;
        }
        break;
    case NodeKind::DECL:
    case NodeKind::FUNC:
    case NodeKind::NREF:
        copy->payload_.name = intern(*node->payload_.name);
        break;
    case NodeKind::DREF:
        copy->payload_.name = intern(*node->payload_.name);
        // A DREF the compiler made for an operator by id stands for the same operator in the
        // copy.
        if (const auto oper = Tree::of(node)->operRefOf(node)) {
            operRefs_[copy] = *oper;
        }
        break;
    case NodeKind::CASE:
        if (node->caseType() == CaseType::Enum) {
            const Node::EnumCase &from = node->enumCase();
            auto *data =
                static_cast<Node::EnumCase *>(allocate(Node::EnumCase::bytesFor(from.count)));
            auto **names   = reinterpret_cast<const std::string **>(data + 1);
            data->label    = intern(*from.label);
            data->count    = from.count;
            data->fields   = names;
            data->bindings = names + from.count;
            for (size_t i = 0; i < from.count; ++i) {
                names[i]              = intern(*from.fields[i]);
                names[from.count + i] = intern(*from.bindings[i]);
            }
            copy->payload_.enumCase = data;
        } else {
            copy->payload_.name = intern(*node->payload_.name);
        }
        break;
    case NodeKind::ACCS:
        if (node->accessByIndex()) {
            copy->payload_.number = node->payload_.number;
        } else {
            copy->payload_.name = intern(*node->payload_.name);
        }
        break;
    case NodeKind::EXPT:
    case NodeKind::LOOP:
    case NodeKind::SFOR:
    case NodeKind::SLOOP: {
        const Node::NameList *names = node->payload_.names;
        auto *list =
            static_cast<Node::NameList *>(allocate(Node::NameList::bytesFor(names->count)));
        list->count = names->count;
        for (size_t i = 0; i < names->count; ++i) {
            list->items[i] = intern(*names->items[i]);
        }
        copy->payload_.names = list;
        break;
    }
    default:
        copy->payload_ = node->payload_; // a type, a number or nothing
        break;
    }
    for (const Node *child : *node) {
        append(*copy, clone(child));
    }
    if (node->kind_ == NodeKind::FUNC) {
        // A lambda's unwritten parameters come with it: the copy is the same lambda.
        if (const auto *params = Tree::of(node)->unwrittenParamsOf(node)) {
            unwrittenParams_[copy] = *params;
        }
    }
    return copy;
}

// =============================================================================
// Text
// =============================================================================

std::string Node::toString() const {
    switch (kind_) {
    case NodeKind::DATA: {
        const data_ptr_t &value = data();
        std::string text        = "DATA: " + pointerToHex(value.get()) + ", ";
        text += value->type() ? value->type()->toString() : std::string("null");
        return text + ", " + value->toString();
    }
    case NodeKind::TYPE:
        return "TYPE: " + dataType()->toString();
    case NodeKind::DECL:
        return "DECL: " + std::string(isFunc() ? "func " : "type ") + name();
    case NodeKind::FUNC:
        return "FUNC: " + (name().empty()
                               ? std::string(decoratedBlock() ? "(decorated block)" : "(anonymous)")
                               : name());
    case NodeKind::NREF:
        return "NREF: " + name();
    case NodeKind::DREF:
        return "DREF: " + name();
    case NodeKind::LINK:
        return "LINK: argcnt=" + std::to_string(argCount());
    case NodeKind::WITH:
        return "WITH: argcnt=" + std::to_string(argCount());
    case NodeKind::TAPP:
        return "TAPP";
    case NodeKind::ACCS:
        return "ACCS: " + (accessByIndex() ? std::to_string(accessIndex()) : accessKey());
    case NodeKind::BRCH:
        switch (branchKind()) {
        case BranchKind::NullThen:
            return "BRCH: ??";
        case BranchKind::ErrorThen:
            return "BRCH: !!";
        case BranchKind::ErrorPass:
            return "BRCH: ?->";
        case BranchKind::LogicalAnd:
            return "BRCH: &&";
        case BranchKind::LogicalOr:
            return "BRCH: ||";
        default:
            return "BRCH";
        }
    case NodeKind::CASE:
        switch (caseType()) {
        case CaseType::Value:
            return "CASE: (value)";
        case CaseType::True:
            return "CASE: (true)";
        case CaseType::Else:
            return "CASE: (else)";
        case CaseType::Bind:
            return "CASE: (bind " + binding() + ")";
        case CaseType::Enum: {
            const EnumCase &pattern = enumCase();
            std::string text        = "CASE: (" + *pattern.label;
            text += enumStructPattern() ? " { " : "(";
            for (size_t i = 0; i < pattern.count; ++i) {
                if (i > 0) {
                    text += ", ";
                }
                if (enumStructPattern()) {
                    text += *pattern.fields[i];
                    if (!pattern.bindings[i]->empty() &&
                        *pattern.bindings[i] != *pattern.fields[i]) {
                        text += ": " + *pattern.bindings[i];
                    }
                } else {
                    text += pattern.bindings[i]->empty() ? "_" : *pattern.bindings[i];
                }
            }
            text += enumStructPattern() ? " }" : ")";
            return text + ")";
        }
        default:
            return "CASE: (error " + binding() + ")";
        }
    case NodeKind::TMTCH:
        return "TMTCH";
    case NodeKind::SFOR:
        return "SFOR: " + staticForValueName() + ", " + staticForTypeName();
    case NodeKind::SACC:
        return "SACC";
    case NodeKind::SPRD:
        return "SPRD";
    case NodeKind::SCOMP:
        return std::string("SCOMP: ") + (comprehensionIsStruct() ? "struct" : "tuple");
    case NodeKind::SLOOP: {
        std::string text = "SLOOP: ";
        for (size_t j = 0; j < staticLoopStateCount(); ++j) {
            text += (j == 0 ? "" : ", ") + staticLoopStateName(j);
        }
        return text + " for (" + staticLoopValueName() + ", " + staticLoopTypeName() + ")";
    }
    case NodeKind::TCASE: {
        std::string text = "TCASE: (";
        bool first       = true;
        for (const TypeCaseKind kind :
             {TypeCaseKind::Struct, TypeCaseKind::Tuple, TypeCaseKind::Leaf,
              TypeCaseKind::Wildcard}) {
            if ((typeCasePatterns() & typeCaseBit(kind)) == 0) {
                continue;
            }
            text += first ? "" : " | ";
            first = false;
            switch (kind) {
            case TypeCaseKind::Struct:
                text += "struct";
                break;
            case TypeCaseKind::Tuple:
                text += "tuple";
                break;
            case TypeCaseKind::Leaf:
                text += "#leaf";
                break;
            case TypeCaseKind::Wildcard:
                text += "_";
                break;
            }
        }
        return text + ")";
    }
    case NodeKind::CAST:
        return "CAST: " + targetType()->toString();
    case NodeKind::EXIT:
        return "EXIT: " + to_string(exitType());
    case NodeKind::EXEC:
        return synced() ? "SYNC" : "EXEC";
    case NodeKind::LOOP: {
        std::string text = std::string("LOOP: ") + camel::core::loopKindName(loopKind()) + " (";
        for (size_t j = 0; j < loopStateCount(); ++j) {
            text += (j == 0 ? "" : ", ") + loopStateName(j);
        }
        text += ")";
        if (camel::core::loopHasIterable(loopKind())) {
            text += " in " + loopElementName();
        }
        return text;
    }
    case NodeKind::EXPT: {
        std::string text = "EXPT: [";
        for (size_t i = 0; i < exportCount(); ++i) {
            text += (i == 0 ? "" : ", ") + exportName(i);
        }
        return text + "]";
    }
    default:
        return to_string(kind_); // VARI, WAIT, ANNO
    }
}

namespace {

/// One line per node, children below their parent. `ancestorsLast[i]` says whether the
/// ancestor at depth i + 1 is the last child of its parent: under such an ancestor the
/// column is blank, under any other it carries the line down to the next sibling.
void printNode(
    std::ostream &os, const Node &node, std::vector<bool> &ancestorsLast, bool isRoot,
    bool isLast) {
    if (!isRoot) {
        for (const bool last : ancestorsLast) {
            os << (last ? "   " : "|  ");
        }
        os << (isLast ? "\\-" : "|-");
    }
    os << node.toString() << std::endl;
    if (!isRoot) {
        ancestorsLast.push_back(isLast);
    }
    for (size_t i = 0; i < node.size(); ++i) {
        printNode(os, *node.at(i), ancestorsLast, false, i + 1 == node.size());
    }
    if (!isRoot) {
        ancestorsLast.pop_back();
    }
}

} // namespace

void Tree::print(std::ostream &os) const {
    if (root_ != nullptr) {
        std::vector<bool> ancestorsLast;
        printNode(os, *root_, ancestorsLast, true, true);
    }
}

} // namespace camel::compile::gct
