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
 * Created: May. 05, 2024
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "data.h"
#include "entity.h"
namespace AbstractSyntaxTree {

class Node;
using node_ptr_t = std::shared_ptr<Node>;
// Node of AST

class Load;
using load_ptr_t = std::shared_ptr<Load>;
// Information of AST node

enum class NodeType {
    // Basic node types
    EXPR, // Expression node
    STMT, // Statement node
    DECL, // Declaration node

    // Data related
    DATA, // Data node
    VARI, // Variable node
    TYPE, // Type node

    // Function related
    FUNC, // Function definition
    CALL, // Function call

    // Reference related
    NREF, // Name reference
    DREF, // Data reference

    // Control flow
    IF,    // Conditional statement
    LOOP,  // Loop statement
    MATCH, // Match statement
    TRY,   // Exception handling

    // Module related
    MODULE, // Module definition
    IMPORT, // Import statement
    EXPORT, // Export statement

    // Others
    WAIT, // Wait
    ANNO, // Annotation
    LINK, // Link
    WITH, // With statement
    RETN, // Return
    EXEC, // Execution
    FROM  // Source
};

class Node {
  protected:
    node_ptr_t parent_;                // Parent node of the current node
    std::vector<node_ptr_t> children_; // List of child nodes
    load_ptr_t load_;                  // Load information associated with the node

  public:
    Node() = default;
    virtual ~Node() = default;

    // Set the parent node
    void setParent(node_ptr_t parent) { parent_ = parent; }

    // Get the parent node
    node_ptr_t parent() const { return parent_; }

    // Add a child node
    void addChild(node_ptr_t child) {
        children_.push_back(child);
        child->setParent(shared_from_this());
    }

    // Get the list of child nodes
    const std::vector<node_ptr_t> &children() const { return children_; }

    // Set the load information
    void setLoad(load_ptr_t load) { load_ = load; }

    // Get the load information
    load_ptr_t load() const { return load_; }
};

// Expression node base class
class ExprNode : public Node {
  protected:
    NodeType exprType_; // Type of the expression
    data_ptr_t value_;  // Value of the expression

  public:
    ExprNode(NodeType type) : exprType_(type) {}
    virtual ~ExprNode() = default;

    // Get the type of the expression
    NodeType exprType() const { return exprType_; }

    // Set the value of the expression
    void setValue(data_ptr_t value) { value_ = value; }

    // Get the value of the expression
    data_ptr_t value() const { return value_; }

    // Clone the node
    node_ptr_t clone() const override {
        auto cloned = std::make_shared<ExprNode>(exprType_);
        cloned->setValue(value_);
        return cloned;
    }
};

inline std::shared_ptr<ExprNode> expr_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DataLoad>(ExprNode);
}

// Statement node base class
class StmtNode : public Node {
  protected:
    NodeType stmtType_; // Type of the statement

  public:
    StmtNode(NodeType type) : stmtType_(type) {}
    virtual ~StmtNode() = default;

    // Get the type of the statement
    NodeType stmtType() const { return stmtType_; }

    // Clone the node
    node_ptr_t clone() const override { return std::make_shared<StmtNode>(stmtType_); }
};

inline std::shared_ptr<StmtNode> stmt_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<StmtNode>(ptr);
}

// Declaration node base class
class DeclNode : public Node {
  protected:
    NodeType declType_; // Type of the declaration

  public:
    DeclNode(NodeType type) : declType_(type) {}
    virtual ~DeclNode() = default;

    // Get the type of the declaration
    NodeType declType() const { return declType_; }

    // Clone the node
    node_ptr_t clone() const override { return std::make_shared<DeclNode>(declType_); }
};

inline std::shared_ptr<DeclNode> decl_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DeclNode>(ptr);
}

// Module-related nodes
class ModuleNode : public Node {
  protected:
    std::string name_;                // Module name
    std::string path_;                // Module path
    std::vector<node_ptr_t> imports_; // Import statements
    std::vector<node_ptr_t> exports_; // Export statements

  public:
    ModuleNode(const std::string &name, const std::string &path) : name_(name), path_(path) {}

    // Get module name
    const std::string &name() const { return name_; }

    // Get module path
    const std::string &path() const { return path_; }

    // Add an import statement
    void addImport(node_ptr_t import) { imports_.push_back(import); }

    // Get list of import statements
    const std::vector<node_ptr_t> &imports() const { return imports_; }

    // Add an export statement
    void addExport(node_ptr_t exportStmt) { exports_.push_back(exportStmt); }

    // Get list of export statements
    const std::vector<node_ptr_t> &exports() const { return exports_; }

    // Clone the node
    node_ptr_t clone() const override {
        auto cloned = std::make_shared<ModuleNode>(name_, path_);
        for (auto import : imports_) {
            cloned->addImport(import->clone());
        }
        for (auto exportStmt : exports_) {
            cloned->addExport(exportStmt->clone());
        }
        return cloned;
    }
};

inline std::shared_ptr<ModuleNode> module_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ModuleNode>(ptr);
}

class ImportNode : public Node {
  protected:
    std::string path_;                // Path of the imported module
    std::vector<std::string> idents_; // List of imported identifiers (if empty, import all)

  public:
    ImportNode(const std::string &path, const std::vector<std::string> &idents = {}) : path_(path), idents_(idents) {}

    // Get the path of the imported module
    const std::string &path() const { return path_; }

    // Get the list of imported identifiers
    const std::vector<std::string> &idents() const { return idents_; }

    // Clone the node
    node_ptr_t clone() const override { return std::make_shared<ImportNode>(path_, idents_); }
};

inline std::shared_ptr<ImportNode> import_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ImportNode>(ptr);
}

class ExportNode : public Node {
  protected:
    std::vector<std::string> idents_; // List of exported identifiers

  public:
    ExportNode(const std::vector<std::string> &idents = {}) : idents_(idents) {}

    // Get the list of exported identifiers
    const std::vector<std::string> &idents() const { return idents_; }

    // Add an exported identifier
    void addIdent(const std::string &ident) { idents_.push_back(ident); }

    // Clone the node
    node_ptr_t clone() const override { return std::make_shared<ExportNode>(idents_); }
};

inline std::shared_ptr<ExportNode> export_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ExportNode>(ptr);
}

class IfNode : public Node {
  protected:
    node_ptr_t cond_; // Condition expression
    node_ptr_t then_; // Then branch
    node_ptr_t else_; // Else branch (optional)

  public:
    IfNode(node_ptr_t cond, node_ptr_t then, node_ptr_t else_ = nullptr) : cond_(cond), then_(then), else_(else_) {}

    // Get the condition expression
    node_ptr_t cond() const { return cond_; }

    // Get the then branch
    node_ptr_t then() const { return then_; }

    // Get the else branch
    node_ptr_t elseBranch() const { return else_; }

    // Clone the node
    node_ptr_t clone() const override {
        return std::make_shared<IfNode>(cond_->clone(), then_->clone(), else_ ? else_->clone() : nullptr);
    }
};

inline std::shared_ptr<IfNode> if_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<IfNode>(ptr);
}

class LoopNode : public Node {
  protected:
    node_ptr_t cond_; // Loop condition
    node_ptr_t body_; // Loop body

  public:
    LoopNode(node_ptr_t cond, node_ptr_t body) : cond_(cond), body_(body) {}

    // Get the loop condition
    node_ptr_t cond() const { return cond_; }

    // Get the loop body
    node_ptr_t body() const { return body_; }

    // Clone the node
    node_ptr_t clone() const override { return std::make_shared<LoopNode>(cond_->clone(), body_->clone()); }
};

inline std::shared_ptr<LoopNode> loop_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<LoopNode>(ptr);
}

class MatchNode : public Node {
  protected:
    node_ptr_t expr_;               // Match expression
    std::vector<node_ptr_t> cases_; // Match cases
    node_ptr_t defaultCase_;        // Default case (optional)

  public:
    MatchNode(node_ptr_t expr, const std::vector<node_ptr_t> &cases, node_ptr_t defaultCase = nullptr)
        : expr_(expr), cases_(cases), defaultCase_(defaultCase) {}

    // Get the match expression
    node_ptr_t expr() const { return expr_; }

    // Get the match cases
    const std::vector<node_ptr_t> &cases() const { return cases_; }

    // Get the default case
    node_ptr_t defaultCase() const { return defaultCase_; }

    // Add a match case
    void addCase(node_ptr_t caseNode) { cases_.push_back(caseNode); }

    // Set the default case
    void setDefaultCase(node_ptr_t defaultCase) { defaultCase_ = defaultCase; }

    // Clone the node
    node_ptr_t clone() const override {
        std::vector<node_ptr_t> clonedCases;
        for (auto caseNode : cases_) {
            clonedCases.push_back(caseNode->clone());
        }
        return std::make_shared<MatchNode>(expr_->clone(), clonedCases, defaultCase_ ? defaultCase_->clone() : nullptr);
    }
};

inline std::shared_ptr<MatchNode> match_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<MatchNode>(ptr);
}

class TryNode : public Node {
  protected:
    node_ptr_t tryBlock_;                 // Try block
    std::vector<node_ptr_t> catchBlocks_; // Catch blocks
    node_ptr_t finallyBlock_;             // Finally block (optional)

  public:
    TryNode(node_ptr_t tryBlock, const std::vector<node_ptr_t> &catchBlocks, node_ptr_t finallyBlock = nullptr)
        : tryBlock_(tryBlock), catchBlocks_(catchBlocks), finallyBlock_(finallyBlock) {}

    // Get the try block
    node_ptr_t tryBlock() const { return tryBlock_; }

    // Get the catch blocks
    const std::vector<node_ptr_t> &catchBlocks() const { return catchBlocks_; }

    // Get the finally block
    node_ptr_t finallyBlock() const { return finallyBlock_; }

    // Add a catch block
    void addCatchBlock(node_ptr_t catchBlock) { catchBlocks_.push_back(catchBlock); }

    // Set the finally block
    void setFinallyBlock(node_ptr_t finallyBlock) { finallyBlock_ = finallyBlock; }

    // Clone the node
    node_ptr_t clone() const override {
        std::vector<node_ptr_t> clonedCatchBlocks;
        for (auto catchBlock : catchBlocks_) {
            clonedCatchBlocks.push_back(catchBlock->clone());
        }
        return std::make_shared<TryNode>(tryBlock_->clone(), clonedCatchBlocks,
                                         finallyBlock_ ? finallyBlock_->clone() : nullptr);
    }
};

inline std::shared_ptr<TryNode> try_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<TryNode>(ptr);
}

class FuncNode : public Node {
  protected:
    std::string name_;               // Function name
    std::vector<node_ptr_t> params_; // Parameter list
    node_ptr_t body_;                // Function body

  public:
    FuncNode(const std::string &name, const std::vector<node_ptr_t> &params, node_ptr_t body)
        : name_(name), params_(params), body_(body) {}

    // Get function name
    const std::string &name() const { return name_; }

    // Get parameter list
    const std::vector<node_ptr_t> &params() const { return params_; }

    // Get function body
    node_ptr_t body() const { return body_; }

    // Clone the node
    node_ptr_t clone() const override {
        std::vector<node_ptr_t> clonedParams;
        for (auto param : params_) {
            clonedParams.push_back(param->clone());
        }
        return std::make_shared<FuncNode>(name_, clonedParams, body_->clone());
    }
};

inline std::shared_ptr<FuncNode> func_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<FuncNode>(ptr);
}

class CallNode : public Node {
  protected:
    node_ptr_t callee_;            // Callee function
    std::vector<node_ptr_t> args_; // Argument list

  public:
    CallNode(node_ptr_t callee, const std::vector<node_ptr_t> &args) : callee_(callee), args_(args) {}

    // Get callee function
    node_ptr_t callee() const { return callee_; }

    // Get argument list
    const std::vector<node_ptr_t> &args() const { return args_; }

    // Clone the node
    node_ptr_t clone() const override {
        std::vector<node_ptr_t> clonedArgs;
        for (auto arg : args_) {
            clonedArgs.push_back(arg->clone());
        }
        return std::make_shared<CallNode>(callee_->clone(), clonedArgs);
    }
};

inline std::shared_ptr<CallNode> call_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<CallNode>(ptr);
}

class TypeNode : public Node {
  protected:
    std::string name_;               // Type name
    std::vector<node_ptr_t> fields_; // List of fields

  public:
    TypeNode(const std::string &name, const std::vector<node_ptr_t> &fields) : name_(name), fields_(fields) {}

    // Get type name
    const std::string &name() const { return name_; }

    // Get list of fields
    const std::vector<node_ptr_t> &fields() const { return fields_; }

    // Clone the node
    node_ptr_t clone() const override {
        std::vector<node_ptr_t> clonedFields;
        for (auto field : fields_) {
            clonedFields.push_back(field->clone());
        }
        return std::make_shared<TypeNode>(name_, clonedFields);
    }
};

inline std::shared_ptr<TypeNode> type_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<TypeNode>(ptr);
}

class TypeAliasNode : public Node {
  protected:
    std::string alias_;       // Alias name
    node_ptr_t originalType_; // Original type

  public:
    TypeAliasNode(const std::string &alias, node_ptr_t originalType) : alias_(alias), originalType_(originalType) {}

    // Get alias name
    const std::string &alias() const { return alias_; }

    // Get original type
    node_ptr_t originalType() const { return originalType_; }

    // Clone the node
    node_ptr_t clone() const override { return std::make_shared<TypeAliasNode>(alias_, originalType_->clone()); }
};

inline std::shared_ptr<TypeAliasNode> type_alias_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<TypeAliasNode>(ptr);
}

class WaitNode : public Node {
  protected:
    node_ptr_t condition_; // Wait condition
    node_ptr_t body_;      // Wait body

  public:
    WaitNode(node_ptr_t condition, node_ptr_t body) : condition_(condition), body_(body) {}

    // Get wait condition
    node_ptr_t condition() const { return condition_; }

    // Get wait body
    node_ptr_t body() const { return body_; }

    // Clone the node
    node_ptr_t clone() const override { return std::make_shared<WaitNode>(condition_->clone(), body_->clone()); }
};

inline std::shared_ptr<WaitNode> wait_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<WaitNode>(ptr);
}

class AnnoNode : public Node {
  protected:
    std::string annotation_; // Annotation content
    node_ptr_t target_;      // Annotation target

  public:
    AnnoNode(const std::string &annotation, node_ptr_t target) : annotation_(annotation), target_(target) {}

    // Get annotation content
    const std::string &annotation() const { return annotation_; }

    // Get annotation target
    node_ptr_t target() const { return target_; }

    // Clone the node
    node_ptr_t clone() const override { return std::make_shared<AnnoNode>(annotation_, target_->clone()); }
};

inline std::shared_ptr<AnnoNode> anno_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<AnnoNode>(ptr);
}

class LinkNode : public Node {
  protected:
    node_ptr_t source_; // Source node
    node_ptr_t target_; // Target node

  public:
    LinkNode(node_ptr_t source, node_ptr_t target) : source_(source), target_(target) {}

    // Get source node
    node_ptr_t source() const { return source_; }

    // Get target node
    node_ptr_t target() const { return target_; }

    // Clone the node
    node_ptr_t clone() const override { return std::make_shared<LinkNode>(source_->clone(), target_->clone()); }
};

inline std::shared_ptr<LinkNode> link_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<LinkNode>(ptr);
}

class WithNode : public Node {
  protected:
    node_ptr_t resource_; // Resource
    node_ptr_t body_;     // Body

  public:
    WithNode(node_ptr_t resource, node_ptr_t body) : resource_(resource), body_(body) {}

    // Get resource
    node_ptr_t resource() const { return resource_; }

    // Get body
    node_ptr_t body() const { return body_; }

    // Clone the node
    node_ptr_t clone() const override { return std::make_shared<WithNode>(resource_->clone(), body_->clone()); }
};

inline std::shared_ptr<WithNode> with_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<WithNode>(ptr);
}

class RetnNode : public Node {
  protected:
    node_ptr_t value_; // Return value

  public:
    RetnNode(node_ptr_t value) : value_(value) {}

    // Get return value
    node_ptr_t value() const { return value_; }

    // Clone the node
    node_ptr_t clone() const override { return std::make_shared<RetnNode>(value_->clone()); }
};

inline std::shared_ptr<RetnNode> retn_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<RetnNode>(ptr);
}

class ExecNode : public Node {
  protected:
    node_ptr_t expr_; // Execution expression

  public:
    ExecNode(node_ptr_t expr) : expr_(expr) {}

    // Get execution expression
    node_ptr_t expr() const { return expr_; }

    // Clone the node
    node_ptr_t clone() const override { return std::make_shared<ExecNode>(expr_->clone()); }
};

inline std::shared_ptr<ExecNode> exec_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ExecNode>(ptr);
}

class Load {
  protected:
    NodeType type_;
    size_t tokenStart_;
    size_t tokenEnd_;

  public:
    Load(NodeType type) : type_(type) {}
    virtual ~Load() = default;

    void setToken(size_t start, size_t end) {
        tokenStart_ = start;
        tokenEnd_ = end;
    }

    NodeType type() const { return type_; }
    std::pair<size_t, size_t> range() const { return {tokenStart_, tokenEnd_}; }
    const std::string typeStr() const;

    virtual const std::string toString() const { return typeStr(); }
    virtual void visit() { throw std::runtime_error("Load::visit() not implemented"); };
};

class DataLoad : public Load {
    data_ptr_t data_;

  public:
    DataLoad(data_ptr_t data) : Load(NodeType::DATA), data_(data) {}

    data_ptr_t data() { return data_; }

    const std::string toString() const override;
};

inline std::shared_ptr<DataLoad> data_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DataLoad>(ptr);
}

class VariLoad : public Load {
  public:
    VariLoad() : Load(NodeType::VARI) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<VariLoad> vari_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<VariLoad>(ptr);
}

class TypeLoad : public Load {
    type_ptr_t dataType_;

  public:
    TypeLoad(type_ptr_t type) : Load(NodeType::TYPE), dataType_(type) {}
    type_ptr_t dataType() const { return dataType_; }

    const std::string toString() const override;
};

inline std::shared_ptr<TypeLoad> type_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<TypeLoad>(ptr);
}

class DeclLoad : public Load {
    func_type_ptr_t funcType_;

  public:
    DeclLoad(func_type_ptr_t type) : Load(NodeType::DECL), funcType_(type) {}
    func_type_ptr_t funcType() const { return funcType_; }

    const std::string toString() const override;
};

inline std::shared_ptr<DeclLoad> decl_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DeclLoad>(ptr);
}

class FuncLoad : public Load {
    func_type_ptr_t funcType_;

  public:
    FuncLoad(func_type_ptr_t type) : Load(NodeType::FUNC), funcType_(type) {}
    func_type_ptr_t funcType() const { return funcType_; }

    const std::string toString() const override;
};

inline std::shared_ptr<FuncLoad> func_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<FuncLoad>(ptr);
}

class NRefLoad : public Load {
    std::string ident_;

  public:
    NRefLoad(const std::string &ident) : Load(NodeType::NREF), ident_(ident) {}

    const std::string ident() const { return ident_; }

    const std::string toString() const override;
};

inline std::shared_ptr<NRefLoad> nref_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<NRefLoad>(ptr);
}

class DRefLoad : public Load {
    std::string ident_;

  public:
    DRefLoad(const std::string &ident) : Load(NodeType::DREF), ident_(ident) {}

    const std::string ident() const { return ident_; }

    const std::string toString() const override;
};

inline std::shared_ptr<DRefLoad> dref_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DRefLoad>(ptr);
}

class WaitLoad : public Load {
    std::vector<std::string> idents_;

  public:
    WaitLoad() : Load(NodeType::WAIT) {}

    void wait(const std::string &ident) { idents_.push_back(ident); }
    const std::vector<std::string> &waited() const { return idents_; }

    const std::string toString() const override;
};

inline std::shared_ptr<WaitLoad> wait_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<WaitLoad>(ptr);
}

class AnnoLoad : public Load {
    std::string annotation_;

  public:
    AnnoLoad(const std::string &annotation) : Load(NodeType::ANNO), annotation_(annotation) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<AnnoLoad> anno_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<AnnoLoad>(ptr);
}

class LinkLoad : public Load {
  public:
    LinkLoad() : Load(NodeType::LINK) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<LinkLoad> link_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<LinkLoad>(ptr);
}

class WithLoad : public Load {
  public:
    WithLoad() : Load(NodeType::WITH) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<WithLoad> with_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<WithLoad>(ptr);
}

class RetnLoad : public Load {
  public:
    RetnLoad() : Load(NodeType::RETN) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<RetnLoad> retn_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<RetnLoad>(ptr);
}

class ExecLoad : public Load {
  public:
    ExecLoad() : Load(NodeType::EXEC) {}

    // const std::string toString() const override;
};

inline std::shared_ptr<ExecLoad> exec_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ExecLoad>(ptr);
}

class FromLoad : public Load {
    std::string path_;
    std::vector<std::string> idents_; // if empty, load all(*)

  public:
    FromLoad(std::string &path, std::vector<std::string> &idents)
        : Load(NodeType::FROM), path_(path), idents_(idents) {}

    const std::string path() const { return path_; }
    const std::vector<std::string> &idents() const { return idents_; }

    const std::string toString() const override;
};

inline std::shared_ptr<FromLoad> from_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<FromLoad>(ptr);
}


class WaitLoad : public Load {
  protected:
    std::vector<std::string> idents_; // List of identifiers to wait for

  public:
    WaitLoad() : Load(NodeType::WAIT) {}

    // Add an identifier to wait for
    void wait(const std::string &ident) { idents_.push_back(ident); }

    // Get the list of identifiers to wait for
    const std::vector<std::string> &waited() const { return idents_; }

    // Convert to string representation
    const std::string toString() const override;
};

inline std::shared_ptr<WaitLoad> wait_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<WaitLoad>(ptr);
}

class AnnoLoad : public Load {
  protected:
    std::string annotation_; // Annotation content

  public:
    AnnoLoad(const std::string &annotation) : Load(NodeType::ANNO), annotation_(annotation) {}

    // Get the annotation content
    const std::string &annotation() const { return annotation_; }

    // Convert to string representation
    const std::string toString() const override;
};

inline std::shared_ptr<AnnoLoad> anno_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<AnnoLoad>(ptr);
}

class LinkLoad : public Load {
  protected:
    node_ptr_t source_; // Source node
    node_ptr_t target_; // Target node

  public:
    LinkLoad(node_ptr_t source, node_ptr_t target) : Load(NodeType::LINK), source_(source), target_(target) {}

    // Get the source node
    node_ptr_t source() const { return source_; }

    // Get the target node
    node_ptr_t target() const { return target_; }

    // Convert to string representation
    const std::string toString() const override;
};

inline std::shared_ptr<LinkLoad> link_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<LinkLoad>(ptr);
}

class WithLoad : public Load {
  protected:
    node_ptr_t resource_; // Resource
    node_ptr_t body_; // Body

  public:
    WithLoad(node_ptr_t resource, node_ptr_t body) : Load(NodeType::WITH), resource_(resource), body_(body) {}

    // Get the resource
    node_ptr_t resource() const { return resource_; }

    // Get the body
    node_ptr_t body() const { return body_; }

    // Convert to string representation
    const std::string toString() const override;
};

inline std::shared_ptr<WithLoad> with_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<WithLoad>(ptr);
}

class RetnLoad : public Load {
  protected:
    node_ptr_t value_; // Return value

  public:
    RetnLoad(node_ptr_t value) : Load(NodeType::RETN), value_(value) {}

    // Get the return value
    node_ptr_t value() const { return value_; }

    // Convert to string representation
    const std::string toString() const override;
};

inline std::shared_ptr<RetnLoad> retn_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<RetnLoad>(ptr);
}

class ExecLoad : public Load {
  protected:
    node_ptr_t expr_; // Execution expression

  public:
    ExecLoad(node_ptr_t expr) : Load(NodeType::EXEC), expr_(expr) {}

    // Get the execution expression
    node_ptr_t expr() const { return expr_; }

    // Convert to string representation
    const std::string toString() const override;
};

inline std::shared_ptr<ExecLoad> exec_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ExecLoad>(ptr);
}


} // namespace AbstractSyntaxTree

namespace AST = AbstractSyntaxTree;
...