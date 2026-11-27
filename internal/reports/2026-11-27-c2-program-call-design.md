# C2：程序里把一遍当函数调 —— 只读设计（三问三答 + 定稿形状）

只读。未改代码、未跑测试。树 `D:\Projects\Camel-wt\cli-rework`，HEAD `22205a05a1`，分支 `lane/task-229`（**不是本人的枝，故未提交**）。

## 问 1：`f->std::cxx::cse` 今天在哪一步被拒

**不在 `src/compile/src/gir/builder_expr.cpp`**：`git grep -n 'pass'` 只有四处注释（`304`/`731`/`1211`/`1716`），没有“`->` 后跟遍名”的判别分支。

遍名在编译侧唯一的“跳过”发生在**导出表**：

```
src/compile/src/gir/builder_modules.cpp:1351-1358
    for (size_t i = 0; i < gct->exportCount(); ++i) {
        const Reference ref(gct->exportName(i));
        const std::string name = ref.toString();
        if (unit_->passDeclarations.contains(name)) {
            // A pass (design principles 9.3): its declaration is read by the pipeline, which
            // resolves the kernel by module and name. It is not an entity the module exports.
            continue;
        }
```

⇒ 遍名不进模块导出表 ⇒ 导入方/限定引用拿不到实体 ⇒ 落到普通的**未解析名字**路径：

```
src/compile/src/gir/builder_calls.cpp:1262-1270
    // One declaration is not a set: a call of it reads its signature, and the signature path is …
    // declaration). A name with none is reported at the call like any other unresolved name. The
    if (set.declarations.size() < 2) {
        return std::nullopt;
    }
```

结论：缺的是**引用/导出可见性**，不是调用形状。

## 问 2：“算子值”在 GIR 侧怎么造

有路，但只对**库算子索引**成立：

```
src/compile/src/gir/builder.h:860-862
    // The functions that stand for library operators named as values (operatorValueGraphOf),
    // by operator URI and signature.
    std::unordered_map<std::string, compile_graph_ptr_t> operatorValueGraphs_;

src/compile/src/gir/builder_operators.cpp:1090-1104
    compile_graph_ptr_t
    Builder::operatorValueGraphOf(const oper_idx_ptr_t &op, const GCT::node_ptr_t &origin) {
        FunctionType *opType = op->funcType();
        if (opType == nullptr || !opType->hasExitType()) return nullptr;
        for (size_t i = 0; i < opType->withTypesCount(); ++i)
            if (opType->withIsVarAt(i)) return nullptr;
        for (size_t i = 0; i < opType->normTypesCount(); ++i)
            if (opType->normIsVarAt(i)) return nullptr;
```

调用点：`builder_calls.cpp:186`、`builder_operators.cpp:1402`、`builder_generics.cpp:2266`。
相关但不同（按 `OperId` 找组/写节点，不是“取值”）：`internalOperatorGroup` / `operatorGroupOf` / `internalOperatorDref`（`builder_variables.cpp:71/80/100`）、`DraftGraphBuilder::addOperNode`（`draft_graph_builder.cpp:218`）。

结论：入参是**算子索引**（`oper_idx_ptr_t`/`OperId`），不是名字；遍名走不到这里，因为没有索引。

## 问 3：内核侧“收/交”的是什么

`std::cxx::cse` 注册的是**遍对象**，不是“收函数值的内核”：

```
src/stdlib/std/cxx/opt/generic/generic.cpp:2195
    const PassKernel commonSubexpressionKernel("std::cxx::cse", makePass<CommonSubexpressionPass>());
src/stdlib/std/cxx/opt/generic/generic.cpp:1613
    size_t cseDraft(camel::runtime::GraphTransaction &transaction)

src/kernel/include/camel/execute/pass/registration.h:52-60
    class PassKernel {
      public:
        PassKernel(const char *path, PassMaker make, bool readsTags = false);
        PassKernel(const char *path, PassMaker make, std::vector<PassParam> params);
    };
    // PassMaker = (const context_ptr_t&, const PassArgs&) -> std::unique_ptr<GraphIRPass>

src/kernel/include/camel/execute/pass/base.h:17-30
    class GraphIRPass {
      public:
        virtual const camel::runtime::Program *apply(const camel::runtime::Program *program, std::ostream &os) = 0;
    };
```

`base.h:32` 把遍的**语义形状**写成 `(f: F) => F!`，那是**声明侧形状**，不是内核签名里的“函数值”类型。

结论：内核收/交的是 **`runtime::Program *`**；没有“函数值”这一档。

## 定稿形状（C2）

1. 缺的是**引用/导出可见性**（遍名不在模块导出表），不是调用形状。
2. 只对“库算子索引”有“拿到算子值”的路：`operatorValueGraphOf(oper_idx_ptr_t, origin)` → 函数图。
3. 内核侧收/交的是 `runtime::Program*`，没有“函数值”这一档。

⇒ **最小闭环：遍名在引用处解析成算子索引 → 复用 `operatorValueGraphOf`。**

## 不做什么

**不改 `PassKernel` / `GraphIRPass` 的签名**——那会把“程序级遍”和“值级函数”混成一件事。“收函数值 / 交回函数值”属于算子层（`operatorValueGraphOf` 的产物就是值），不属于遍内核。

## 与在跑车道的重叠

`task-230`（解析面按 `#op` 找实现）与本格同区域（builder / pass 登记）。**建议划界**：
- **`task-230` 承担“解析面按 `#op` 找到实现”**（它在解析器/实现查表那条线上，改的是“按什么找实现”）；
- **C2 承担“遍名的导出可见性”**（`builder_modules.cpp:1351-1358` 那条 `continue` 与随后的引用解析），因为它直接决定“程序里能不能写这个名字”。

两者若都要动 `builder_modules.cpp`/`builder.cpp` 的登记段，**先合 `task-230` 再落 C2**，否则两边会在同几行上撞。若决定把“导出可见性”也并入 `task-230`，C2 可缩成“复用 `operatorValueGraphOf`”一笔即可。