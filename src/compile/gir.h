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
 * Created: Aug. 13, 2024
 * Updated: Dec. 19, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <any>
#include <list>
#include <unordered_set>
#include <variant>

#include "core/data/composite/func.h"
#include "core/data/data.h"
#include "core/operator.h"
#include "utils/exstore.h"
#include "utils/log.h"
#include "utils/type.h"

namespace GraphIR {

enum class NodeType {
    DATA, // Compile-time constant
    PORT, // Port data (runtime)
    CAST, // Type casting during runtime
    COPY, // A runtime copy of data
    FILL, // Structured data constructed during runtime
    ACCS, // Element accessed during runtime
    BRCH, // Conditional branch
    JOIN, // Conditional branch join
    CALL, // Invoke a function
    BIND, // Attach with params to a function
    FUNC, // (Sub)-Graph, function
    OPER, // Atomic operation
    EXIT, // Final output node

    // Nodes without private data
    SYNC, // Synchronization point
    NREF, // Reference of node

    // Temporary nodes during graph construction
    DREF, // Dereference
};

enum class LinkType {
    Norm, // Node edge
    With, // With edge
    Ctrl, // Control edge
};

std::string to_string(NodeType type);
std::string to_string(LinkType type);

class Graph;
class Node;

using graph_ptr_t     = std::shared_ptr<Graph>;
using graph_wptr_t    = std::weak_ptr<Graph>;
using graph_vec_t     = std::vector<graph_ptr_t>;
using graph_vec_ptr_t = std::shared_ptr<graph_vec_t>;
using node_ptr_t      = std::shared_ptr<Node>;
using node_wptr_t     = std::weak_ptr<Node>;
using node_lst_t      = std::list<node_ptr_t>;
using node_vec_t      = std::vector<node_ptr_t>;
using node_set_t      = std::unordered_set<node_ptr_t>;

// 0 represents empty, positive numbers indicate runtime data segment indices,
// and negative numbers indicate the opposite of static data segment indices.
using data_idx_t = int16_t;
using arr_size_t = uint16_t;

struct WeakPtrHash {
    template <typename T> std::size_t operator()(const std::weak_ptr<T> &wp) const {
        if (auto sp = wp.lock()) {
            return std::hash<T *>()(sp.get());
        }
        return 0;
    }
};

struct WeakPtrEqual {
    template <typename T>
    bool operator()(const std::weak_ptr<T> &lhs, const std::weak_ptr<T> &rhs) const {
        return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
    }
};

class Graph : public std::enable_shared_from_this<Graph> {
  public:
    Graph(const Graph &other)            = delete; // Delete copy constructor
    Graph &operator=(const Graph &other) = delete; // Delete copy assignment
    Graph(Graph &&other)                 = delete; // Delete move constructor
    Graph &operator=(Graph &&other)      = delete; // Delete move assignment

    explicit Graph(
        const func_type_ptr_t &funcType, const graph_ptr_t &graph = nullptr,
        const std::string &name = "")
        : name_(name), outer_(graph), funcType_(funcType),
          staticDataType_(std::make_shared<TupleType>()),
          runtimeDataType_(std::make_shared<TupleType>()),
          closureType_(std::make_shared<TupleType>()) {
        EXEC_WHEN_DEBUG(
            l.in("GIR").debug("Created Graph: {}", name_.empty() ? "<anonymous>" : name_));
    }
    ~Graph() {
        EXEC_WHEN_DEBUG(
            l.in("GIR").debug("Destroyed Graph: {}", name_.empty() ? "<anonymous>" : name_));
    };

    static graph_ptr_t create(
        const func_type_ptr_t &funcType, const graph_ptr_t &graph = nullptr,
        const std::string &name = "");

    static graph_ptr_t null() { return nullptr; }

    // Compare pointer addresses
    bool operator==(const Graph &other) const { return this == &other; }
    bool operator!=(const Graph &other) const { return !(this == &other); }

    bool isRoot() const { return !outer_.lock(); }
    const std::string &name() const { return name_; }
    std::string mangledName() const { return name_ + std::format("<{}>", funcType()->mangle()); }
    std::string location() const;
    bool looped() const { return looped_; }
    bool empty() const { return nodes_.empty(); }
    graph_ptr_t outer() const;
    size_t inDegree() const { return dependencies_.size(); }
    size_t outDegree() const { return dependents_.size(); }

    std::string toString() const;

    const func_type_ptr_t &funcType() const { return funcType_; }
    const std::shared_ptr<TupleType> &staticDataType() const { return staticDataType_; }
    const std::shared_ptr<TupleType> &runtimeDataType() const { return runtimeDataType_; }
    const std::shared_ptr<TupleType> &closureType() const { return closureType_; }

    const data_vec_t &staticDataArr() const { return staticDataArr_; }
    data_idx_t addStaticData(const data_ptr_t &data);
    data_idx_t addRuntimeData();
    void setStaticData(data_idx_t index, const data_ptr_t &data);
    data_ptr_t getStaticData(data_idx_t index) const;
    size_t staticDataSize() const { return staticDataArr_.size(); }
    size_t runtimeDataSize() const { return runtimeDataSize_; }

    std::optional<std::unordered_set<graph_ptr_t>> getSubGraphsByName(const std::string &name);
    void addSubGraph(const graph_ptr_t &graph);
    void delSubGraph(const graph_ptr_t &graph);
    std::unordered_map<std::string, std::unordered_set<graph_ptr_t>> &subGraphs() {
        return subGraphs_;
    }

    std::unordered_set<graph_wptr_t, WeakPtrHash, WeakPtrEqual> &dependents() {
        return dependents_;
    }
    std::unordered_set<graph_ptr_t> &dependencies() { return dependencies_; }
    void addDependency(const graph_ptr_t &graph);
    void delDependency(const graph_ptr_t &graph);

    // Called by Node::create
    void addNode(const node_ptr_t &node);
    void delNode(const node_ptr_t &node);

    // Called by PortNode::create
    void addPort(const node_ptr_t &node, bool isWith = false);
    void addClosure(const node_ptr_t &node);
    bool parameterized() const { return parameterized_; }

    // Convert all closure capture nodes to parameter nodes
    void parametrizeClosure();

    const node_ptr_t &exitNode() const;
    const node_ptr_t &outputNode() const;
    bool hasOutput() const { return exitNode_ != nullptr; }
    void setOutput(const node_ptr_t &node);

    const node_vec_t &nodes() { return nodes_; }
    node_vec_t ports() {
        node_vec_t ports;
        ports.insert(ports.end(), normPorts_.begin(), normPorts_.end());
        ports.insert(ports.end(), withPorts_.begin(), withPorts_.end());
        return ports;
    }
    bool hasPorts() const { return !normPorts_.empty() || !withPorts_.empty(); }
    bool hasClosure() const { return !closure_.empty(); }
    const node_vec_t &normPorts() { return normPorts_; }
    const node_vec_t &withPorts() { return withPorts_; }
    const node_vec_t &closure() { return closure_; }
    size_t argsCount() const { return normPorts_.size() + withPorts_.size() + closure_.size(); }

    graph_ptr_t clone() const;

    node_ptr_t inlineNode(const node_ptr_t &node, bool forceSync = false);

    bool dirty() const { return dirty_; }
    void rearrange();

    template <typename T, std::size_t Index> T *getExtra() const { return extras_.get<T, Index>(); }
    template <typename T, std::size_t Index> void setExtra(T *ptr) const {
        extras_.set<T, Index>(ptr);
    }

  private:
    std::string name_;
    graph_wptr_t outer_;

    std::unordered_map<std::string, std::unordered_set<graph_ptr_t>> subGraphs_;
    std::unordered_set<graph_ptr_t> dependencies_;
    std::unordered_set<graph_wptr_t, WeakPtrHash, WeakPtrEqual> dependents_;

    func_type_ptr_t funcType_;
    std::shared_ptr<TupleType> staticDataType_, runtimeDataType_, closureType_;
    // Static data segment, index 0 is reserved as empty
    data_vec_t staticDataArr_ = {nullptr};

    // Dynamic data size, starting from 1, 0 is reserved as empty
    size_t runtimeDataSize_ = 1;

    node_vec_t normPorts_, withPorts_, closure_;
    node_vec_t nodes_;
    node_ptr_t exitNode_;

    // Marked as dirty when nodes are added or removed
    // Needs to be resolved via rearrange
    bool dirty_ = false;

    bool looped_        = false;
    bool parameterized_ = false;

    // 用于快速查询 Graph 附属信息
    mutable ExtraStorage<4> extras_;
};

class Node : public std::enable_shared_from_this<Node> {
  public:
    Node(Graph &graph, NodeType nodeType, const type_ptr_t &dataType, data_idx_t index)
        : graph_(graph), nodeType_(nodeType), dataType_(dataType), dataIndex_(index) {}
    virtual ~Node() = default;

    NodeType type() const { return nodeType_; }
    type_ptr_t dataType() const {
        ASSERT(dataType_ != nullptr, "Node has no data type.");
        return dataType_;
    }
    void setDataType(const type_ptr_t &type) { dataType_ = type; }
    virtual std::string toString() const {
        return std::format("Node({}, {})", to_string(nodeType_), std::to_string(dataIndex_));
    }
    virtual operator std::string() const { return toString(); }
    virtual node_ptr_t clone(Graph &graph) const = 0;

    bool operator==(const Node &other) const {
        // Compare pointer addresses
        return this == &other;
    }
    bool operator!=(const Node &other) const { return !(this == &other); }

    Graph &graph() const { return graph_; }
    data_idx_t index() const;
    void setIndex(data_idx_t index) { dataIndex_ = index; }
    bool macro() const { return macro_; }
    bool constant() const { return const_; }
    void setMacro(bool m) { macro_ = m; }
    void setConstant(bool c) { const_ = c; }

    node_vec_t dataInputs() const {
        node_vec_t inputs;
        inputs.insert(inputs.end(), normInputs_.begin(), normInputs_.end());
        inputs.insert(inputs.end(), withInputs_.begin(), withInputs_.end());
        return inputs;
    }
    node_vec_t &normInputs() { return normInputs_; }
    node_vec_t &withInputs() { return withInputs_; }
    node_vec_t &ctrlInputs() { return ctrlInputs_; }
    node_vec_t inputs() {
        node_vec_t inputs;
        inputs.reserve(normInputs_.size() + withInputs_.size() + ctrlInputs_.size());
        inputs.insert(inputs.end(), normInputs_.begin(), normInputs_.end());
        inputs.insert(inputs.end(), withInputs_.begin(), withInputs_.end());
        inputs.insert(inputs.end(), ctrlInputs_.begin(), ctrlInputs_.end());
        return inputs;
    }

    node_vec_t &normOutputs() { return normOutputs_; }
    node_vec_t &withOutputs() { return withOutputs_; }
    node_vec_t &ctrlOutputs() { return ctrlOutputs_; }
    node_vec_t dataOutputs() {
        node_vec_t outputs;
        outputs.reserve(normOutputs_.size() + withOutputs_.size());
        outputs.insert(outputs.end(), normOutputs_.begin(), normOutputs_.end());
        outputs.insert(outputs.end(), withOutputs_.begin(), withOutputs_.end());
        return outputs;
    }
    node_vec_t outputs() {
        node_vec_t outputs;
        outputs.reserve(normOutputs_.size() + withOutputs_.size() + ctrlOutputs_.size());
        outputs.insert(outputs.end(), normOutputs_.begin(), normOutputs_.end());
        outputs.insert(outputs.end(), withOutputs_.begin(), withOutputs_.end());
        outputs.insert(outputs.end(), ctrlOutputs_.begin(), ctrlOutputs_.end());
        return outputs;
    }

    bool hasDeepLinkedTo(const node_ptr_t &node, size_t maxJumps = 99) const;
    bool hasLinkedTo(const node_ptr_t &node) const;

    size_t inDegree() const { return normInputs_.size() + withInputs_.size() + ctrlInputs_.size(); }
    size_t outDegree() const {
        return withOutputs_.size() + normOutputs_.size() + ctrlOutputs_.size();
    }
    size_t dataInDegree() const { return normInputs_.size() + withInputs_.size(); }
    size_t dataOutDegree() const { return normOutputs_.size() + withOutputs_.size(); }

    bool isSource() const { return inDegree() == 0; }
    bool isReturn() const { return outDegree() == 0; }

    bool detach();

    static void link(LinkType type, const node_ptr_t &from, const node_ptr_t &to);
    static bool unlink(const node_ptr_t &from, const node_ptr_t &to);
    static bool replace(const node_ptr_t &oldNode, const node_ptr_t &newNode);

  protected:
    bool macro_ = false;
    bool const_ = false;

    Graph &graph_;

    NodeType nodeType_;
    type_ptr_t dataType_;
    data_idx_t dataIndex_;

    node_vec_t normInputs_;
    node_vec_t withInputs_;
    node_vec_t ctrlInputs_;

    node_vec_t normOutputs_;
    node_vec_t withOutputs_;
    node_vec_t ctrlOutputs_;
};

class DataNode : public Node {
  public:
    DataNode(Graph &graph, const type_ptr_t &type, data_idx_t index)
        : Node(graph, NodeType::DATA, type, index) {}
    ~DataNode() = default;

    static node_ptr_t create(Graph &graph, const data_ptr_t &data) {
        data_idx_t index = graph.addStaticData(data);
        auto node        = std::make_shared<DataNode>(graph, data->type(), index);
        graph.addNode(node);
        return node;
    }

    data_ptr_t data() const { return graph_.getStaticData(dataIndex_); }

    virtual std::string toString() const override {
        return std::format(
            "DATA({}, {}): {}",
            dataIndex_,
            data()->toString(),
            dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return DataNode::create(graph, data());
    }
};

class PortNode : public Node {
    std::string name_;
    bool isVar_;

  public:
    PortNode(
        Graph &graph, const type_ptr_t &type, data_idx_t index, const std::string &name, bool isVar)
        : Node(graph, NodeType::PORT, type, index), name_(name), isVar_(isVar) {}
    ~PortNode() = default;

    static node_ptr_t
    create(Graph &graph, const type_ptr_t &type, const std::string &name, bool isVar) {
        data_idx_t index = graph.addRuntimeData();
        auto node        = std::make_shared<PortNode>(graph, type, index, name, isVar);
        // This is not automatically called and needs to be added manually because the order of
        // ports is very important. graph.addPort(node);
        return node;
    }

    const std::string &name() const { return name_; }
    bool isVar() const { return isVar_; }

    virtual std::string toString() const override {
        return std::format(
            "PORT({}, {}{}): {}",
            dataIndex_,
            isVar_ ? "var " : "",
            name_,
            dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return PortNode::create(graph, dataType_, name_, isVar_);
    }
};

class CastNode : public Node {
  public:
    CastNode(Graph &graph, const type_ptr_t &type, data_idx_t index)
        : Node(graph, NodeType::CAST, type, index) {}
    ~CastNode() = default;

    static node_ptr_t create(Graph &graph, const type_ptr_t &type) {
        data_idx_t index = graph.addRuntimeData();
        auto node        = std::make_shared<CastNode>(graph, type, index);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("CAST({}): {}", dataIndex_, dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return CastNode::create(graph, dataType_);
    }
};

class CopyNode : public Node {
  public:
    CopyNode(Graph &graph, const type_ptr_t &type, data_idx_t index)
        : Node(graph, NodeType::COPY, type, index) {}
    ~CopyNode() = default;

    static node_ptr_t create(Graph &graph, const type_ptr_t &type) {
        data_idx_t index = graph.addRuntimeData();
        auto node        = std::make_shared<CopyNode>(graph, type, index);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("COPY({}): {}", dataIndex_, dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return CopyNode::create(graph, dataType_);
    }
};

class FillNode : public Node {
  public:
    FillNode(Graph &graph, const type_ptr_t &type, data_idx_t index)
        : Node(graph, NodeType::FILL, type, index) {}
    ~FillNode() = default;

    static node_ptr_t create(Graph &graph, const type_ptr_t &type) {
        data_idx_t index = graph.addRuntimeData();
        auto node        = std::make_shared<FillNode>(graph, type, index);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("FILL({}): {}", dataIndex_, dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return FillNode::create(graph, dataType_);
    }
};

class AccsNode : public Node {
  public:
    AccsNode(
        Graph &graph, const type_ptr_t &type, data_idx_t index,
        const std::variant<std::string, size_t> &accsIdx)
        : Node(graph, NodeType::ACCS, type, index), accsIndex_(accsIdx) {}
    ~AccsNode() = default;

    static node_ptr_t
    create(Graph &graph, const type_ptr_t &type, const std::variant<std::string, size_t> &accsIdx) {
        data_idx_t index = graph.addRuntimeData();
        auto node        = std::make_shared<AccsNode>(graph, type, index, accsIdx);
        graph.addNode(node);
        return node;
    }

    bool isNum() const { return std::holds_alternative<size_t>(accsIndex_); }
    template <typename T> T index() const { return std::get<T>(accsIndex_); }
    std::string index2String() const {
        if (std::holds_alternative<size_t>(accsIndex_)) {
            return std::to_string(std::get<size_t>(accsIndex_));
        } else {
            return std::get<std::string>(accsIndex_);
        }
    }

    virtual std::string toString() const override {
        return std::format("ACCS({}, ${}): {}", dataIndex_, index2String(), dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return AccsNode::create(graph, dataType_, accsIndex_);
    }

  private:
    std::variant<std::string, size_t> accsIndex_;
};

class BrchNode : public Node {
  public:
    BrchNode(Graph &graph, const type_ptr_t &type, data_idx_t index)
        : Node(graph, NodeType::BRCH, type, index) {}
    ~BrchNode() = default;

    static node_ptr_t create(Graph &graph, const type_ptr_t &type) {
        data_idx_t index = graph.addRuntimeData();
        auto node        = std::make_shared<BrchNode>(graph, type, index);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("BRCH({}): {}", dataIndex_, dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return BrchNode::create(graph, dataType_);
    }
};

class JoinNode : public Node {
  public:
    JoinNode(Graph &graph, const type_ptr_t &type, data_idx_t index)
        : Node(graph, NodeType::JOIN, type, index) {}
    ~JoinNode() = default;

    static node_ptr_t create(Graph &graph, const type_ptr_t &type) {
        data_idx_t index = graph.addRuntimeData();
        auto node        = std::make_shared<JoinNode>(graph, type, index);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("JOIN({}): {}", dataIndex_, dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return JoinNode::create(graph, dataType_);
    }
};

class CallNode : public Node {
  public:
    CallNode(Graph &graph, const type_ptr_t &type, data_idx_t index)
        : Node(graph, NodeType::CALL, type, index) {}
    ~CallNode() = default;

    static node_ptr_t create(Graph &graph, const type_ptr_t &type) {
        data_idx_t index = graph.addRuntimeData();
        auto node        = std::make_shared<CallNode>(graph, type, index);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("CALL({}): {}", dataIndex_, dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return CallNode::create(graph, dataType_);
    }
};

class BindNode : public Node {
  public:
    BindNode(Graph &graph, const type_ptr_t &type, data_idx_t index)
        : Node(graph, NodeType::BIND, type, index) {}
    ~BindNode() = default;

    static node_ptr_t create(Graph &graph, const type_ptr_t &type) {
        data_idx_t index = graph.addRuntimeData();
        auto node        = std::make_shared<BindNode>(graph, type, index);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("BIND({}): {}", dataIndex_, dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return BindNode::create(graph, dataType_);
    }
};

class FuncNode : public Node {
    func_ptr_t func_;

  public:
    FuncNode(Graph &graph, data_idx_t index, func_ptr_t func)
        : Node(graph, NodeType::FUNC, func->funcType()->exitType(), index), func_(func) {}
    ~FuncNode() = default;

    static node_ptr_t create(Graph &graph, func_ptr_t func) {
        data_idx_t index = graph.addRuntimeData();
        auto node        = std::make_shared<FuncNode>(graph, index, func);
        graph.addNode(node);
        return node;
    }

    func_ptr_t func() const { return func_; }
    func_type_ptr_t funcType() const {
        ASSERT(func_, "Function is not set for FunctionNode.");
        return std::dynamic_pointer_cast<FunctionType>(func_->type());
    }

    virtual std::string toString() const override {
        return std::format(
            "FUNC({}, {}: {}): {}",
            dataIndex_,
            func_->name().empty() ? func_->graph().name() : func_->name(),
            funcType()->toString(),
            dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override { return FuncNode::create(graph, func_); }
};

class OperNode : public Node {
    oper_idx_ptr_t operator_;

  public:
    OperNode(Graph &graph, data_idx_t index, oper_idx_ptr_t op)
        : Node(graph, NodeType::OPER, op->funcType()->exitType(), index), operator_(op) {}
    ~OperNode() = default;

    static node_ptr_t create(Graph &graph, oper_idx_ptr_t op) {
        data_idx_t index = graph.addRuntimeData();
        auto node        = std::make_shared<OperNode>(graph, index, op);
        graph.addNode(node);
        return node;
    }

    oper_idx_ptr_t oper() const { return operator_; }
    func_type_ptr_t funcType() const {
        ASSERT(operator_, "Operator is not set for OperatorNode.");
        return tt::as_shared<FunctionType>(operator_->funcType());
    }

    virtual std::string toString() const override {
        return std::format(
            "OPER({}, <{}>, {}: {}): {}",
            dataIndex_,
            operator_->name(),
            operator_->uri(),
            operator_->funcType()->toString(),
            dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return OperNode::create(graph, operator_);
    }
};

class ExitNode : public Node {
  public:
    ExitNode(Graph &graph, const type_ptr_t &type, data_idx_t index)
        : Node(graph, NodeType::EXIT, type, index) {}
    ~ExitNode() = default;

    static node_ptr_t create(Graph &graph, const type_ptr_t &type, data_idx_t index = 0) {
        auto node = std::make_shared<ExitNode>(graph, type, index);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("EXIT({}): {}", dataIndex_, dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return ExitNode::create(graph, dataType_);
    }
};

class DrefNode : public Node {
  public:
    using dref_target_t = std::variant<graph_vec_ptr_t, oper_group_ptr_t>;

    DrefNode(Graph &graph, const dref_target_t &target)
        : Node(graph, NodeType::DREF, Type::Void(), 0), target_(target) {}
    ~DrefNode() = default;

    static node_ptr_t create(Graph &graph, const dref_target_t &target) {
        auto node = std::make_shared<DrefNode>(graph, target);
        // Virtual node, not added to the graph, only used temporarily during construction
        // graph.addNode(node);
        return node;
    }

    const dref_target_t &target() const { return target_; }

    virtual std::string toString() const override {
        return std::format("DREF({}): {}", graph_.name(), dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        ASSERT(false, "DrefNode cannot be cloned.");
        return nullptr;
    }

  private:
    dref_target_t target_;
};

class SyncNode : public Node {
  public:
    SyncNode(Graph &graph) : Node(graph, NodeType::SYNC, Type::Void(), 0) {}
    ~SyncNode() = default;

    static node_ptr_t create(Graph &graph) {
        auto node = std::make_shared<SyncNode>(graph);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("SYNC({}): {}", dataIndex_, dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override { return SyncNode::create(graph); }
};

class NRefNode : public Node {
  public:
    NRefNode(Graph &graph) : Node(graph, NodeType::NREF, Type::Void(), 0) {}
    ~NRefNode() = default;

    static node_ptr_t create(Graph &graph) {
        auto node = std::make_shared<NRefNode>(graph);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("NREF({}): {}", dataIndex_, dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override { return NRefNode::create(graph); }
};

} // namespace GraphIR
