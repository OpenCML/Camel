# task-383 交接卡：8 路 scan 输出里只有 1 路被认领

分支 `lane/cc-loop-forward`（= 队列里的 `lane/block-hash`），基 `616f65b436`。树干净，无未提交改动。
接手方：`lane-oper-shape-guard`。

## ① 当前一步：一次构建能把 8 行表跑出来的探针规格

改 `src/stdlib/onnx/exporter.cpp`，三处临时 `fprintf(stderr, "[t383] …")`：

1. **逐路**（`emitLoopScanning` 内，`fromScan` 算完之后）：
   对 `c in [0, scans)` 打 `c`、`components[states + c]` 的节点引用整数、该节点的 `oper`
   （取节点照同文件 `2630-2645` 的既有写法：由引用取 `body`，读 `body->oper`；uri 用
   `core::OperatorTable::instance()` 的既有读法）。
2. **逐路**同上，打该产出节点的**操作数引用**（`normInputsOf`/`withInputsOf` 一族），
   用来判断它是不是那支 `append` 的直接消费方。
3. **`unsupported` 处**（`if (!lowering || !with.empty())`，`4262` 一带）：补打**当前正在求值的
   节点引用**，让"被拒绝的节点"与上表某一行对上号。

只打印，不改任何判断。跑完把 8 行填满再谈改法。

## ② 读数（原样，形状 A = `main(w, xs)` 返回收集循环的梯度，遍形式导出）

```
[t207] loop states=3 scans=8 components=11 fromScan=[S0,-,-]
[t207] emitLoopScanning entry scans=8 carried=2
[t207] no lowering uri=':struct/append_arr' loop(states=3 scans=8 components=11)
```

三行事实（读源码 + 上面读数，不用重跑）：
1. `src/stdlib/onnx/exporter.cpp` 里 `const size_t scans = loop.collectionCount();`
   —— 8 是**循环节点自己说的事实**，不是 `components - states` 减出来的；
2. `stateIsTheCollection` 只认得出"形状可证"的那一路（8 中 1，`fromScan=[S0,-,-]`）；
3. `components=11 = states(3) + scans(8)` **自洽** ⇒ 对不上的不是"数"，是"认领"。

已能进表的一行：scan 0 ← state 0（`IS_THE_COLLECTION (norm=45 stateParam=45 withIn=105 collected=105)`）。
scan 1..7 未认领，来源待 ① 的探针读出。

## ③ 下一步怎么接

1. 按 ① 插桩、一次构建、读 stderr，填满 8 行（每行：scan 路 → 来源符号 → 依据读数）；
2. 填满后才谈改法：若某几路的产出节点就是 `:struct/append_arr` 本身，那几路本该走 collection
   路径；若产出节点是别的算子，这几路是"本该留在 body"的设计边界候选，要写成边界 + 给一条
   能判定的读数；
3. **`src/stdlib/autodiff/pullback.cpp` 里 `collectionCount = 0` 那处（反向循环 payload）
   不是本卡病因**：它与这里 `collectionCount=8` 的循环是两条不同的循环。别顺手改。

## ④ 边界

- 不猜那 7 路的来源（没有读数就不写进表）；
- 不改 `pullback.cpp` 那处 payload；
- 探针与临时 `printf` 提交前全部还原，`numstat` 只留真正的改动；
- 不改语义/断言凑绿；不开新枝名（复用现有车道）；一次一条 run，每段带
  `[provenance] tip` + `binary built`。