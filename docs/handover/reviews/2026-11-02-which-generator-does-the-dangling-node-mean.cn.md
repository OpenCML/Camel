# 「生成器悬垂节点」指的是哪一件事：`yield` 状态机降级出的无人读节点，还是 `test/tools/progen` 生成的程序里的悬垂？

> **该页引用的「本版本尚未构建 / 派内部错误并把程序发来」一句已于 2026-11-02 由作者裁定作废**（生成器 `loop` 的计算初始值是合法程序）。以下保留当日原文与当日读数。

**状态**：待裁；我已按默认 (i) 走了一步，读数见第 4 节。

## 1. 问题

作者 12:50 那句里有一项"生成器悬垂节点…在哪一步"。**"生成器"在树里有两个意思**，指哪一个，
决定"哪一步"落在哪个文件上：

- **(i) 语言的 `yield` 生成器** —— 编译成状态机（`src/compile/gct/builder.cpp` 的 `visitGeneratorBody`）。
- **(ii) `test/tools/progen`** —— 差分测试用的**程序**生成器（写 `.cml` 源文件，不参与发布）。

## 2. 树里 `dangling` 的全部命中（排除 `docs/handover/` 归档）

`git grep -n -i 'dangling|orphan' -- src/compile src/passes docs/design-principles.cn.md` 只落在一处机制：

```cpp
pair<data_ptr_t, bool>
Builder::extractData(const node_ptr_t &node, node_ptr_t &execNode, bool &dangling) {
    auto [refData, dang] = extractData(node, execNode);
    if (dang) { dangling = true; }
    return make_pair(refData, dang);
}
```

它的意思是"这个值的构造需要只有执行节点能做的事"，于是每个调用点**把它挂到执行节点上**，而不是报错：

```cpp
if (dangling) { *execNode << dataNode; dataNode = execNode; }
```

（`src/compile/gct/builder.cpp#Builder::visitExitStmt`，同样四行还出现在 `:4202`、`:4860`、`:4916`、`:4983`、`:5091`。）

生成器那一侧有两个**真**拒绝点，都不是"悬垂"：

- `visitGeneratorBody`（`:5707`）对状态机建不出的形状点名叫人送程序：
  `"the generator machine does not build this shape; send the program, this is a …"`（`:5715` 一带）；
- 一个没被生成器机器认领的 `yield` 走到语句构造器时被 `FeatureNotSupported` 拒（`:2164`）。

`test/tools/progen` 里 `dangling` 命中 **0**。

**结论一句**：**全树没有一条规则因"悬垂"拒程序，所以我现在无法把任何一种解释对上一条红** ——
没有拒绝点，就没有"检查太严"，也没有可放松的条件。

## 3. 我方默认（按它往下做）

**(i) 语言的 `yield` 状态机。** 理由：作者原话与"哪一步"并列，说的是**实现进度**；
`progen` 是测试工具，不在发布路径上，作者平时点它叫"生成器版本"而不叫"生成器节点"。

## 4. 按 (i) 先走的一步（读数，不是推测）

用例：`test/cases/compile/stream/generators.cml`（树里已有的 yield 用例，`std::gir` 转储，exit 0）。

读数（**两次运行，dump 逐字节相同**：`md5sum` 两行都是 `d53c5bbadfb31786336bcbeb32eacf41`）：

```
nodes=595  no-reader=25
{"EXIT":25}
sample: O05[EXIT] O06[EXIT] O07[EXIT] O04[EXIT] O03[EXIT] O02[EXIT] O0B[EXIT] O0C[EXIT] O0A[EXIT] O09[EXIT]
```

**判读**：GIR 里 `A -> B` 是"生产者 → 消费者"（例：`N12 [label="0", type="DATA"] -> N13`），
所以"无人读"= **出度为 0**。降级后的图里**出度为 0 的节点只有 25 个 EXIT** —— exit 本来没人读。
**没有一个 DATA/OPER/FUNC 节点是无读者的**，即在这一条用例的 GIR 层上，**"降级出无人读节点"不成立**。

分母与被否掉的读法一并记下：出度为 0 是 25 个；我第一遍按**入度**算得的 202 个是错的
（那是"没有生产者"，也就是根/端口/常量；`N12` 的第二次出现正是它自己的出边，不是属性里的引用）。

**边界（第二版：实际扫到的范围）**：`test/cases/compile/stream/{generators,yield}_*.cml` 共 **24 条**，
逐条 `std::gir`、逐条跑两次（**24 条全部 `same=y`**）：

- **19 条绿**：`nodes` 从 114 到 595，**`no-reader` 的 kind 计数一律只有 `EXIT`**
  （8、7、8、8、13、6、7、6、7、6、8、7、14、12、7、5、7、11，加 `generators.cml` 的 25；最大一条
  `generators_shapes_consumed` 571 个节点 / 14 个 EXIT）；
- **5 条红**（`generators_cell_retype`、`generators_read_before_declaration`、`yield_head_nested_no_stream`、
  `yield_head_no_stream`、`yield_keyword`）：exit 1、转储为空（`nodes=0`）；它们本来就是拒绝用例，
  **没有图可读，因此不构成"无读者"的样本**（这 5 条的红与"悬垂"无关，另案）。

**字节码/VM 层：这一层不存在"读者"这个概念，所以这条结论到 GIR 为止。** `std::bc`
（= `std::fastvm::bytecode`，`src/execute/pass.cpp#passAliases`）转储的是**槽位与指令**：

```
[index] opcode (opsize) [self] | [fastops] | <with> | extra
  [0] CALL (2) [ 1] | [ 0,  1] | () <-1> |
```

值活在槽里，**没有"哪个节点读哪个节点"这张关系**；"写了没人读"在那一层是**槽的活跃性**问题，
不是图的悬垂问题。要用那一层做判据，得先定义一个"读"，不能把 GIR 的出度直接搬过去。

**我方对下一层的默认提议（作者可驳回）**：若要字节码层的判据，建议用**槽的活跃性**
（`slot live` 的读者集合 —— 指令的读槽集合减写槽集合），而不是"节点出度"：
两者判的不是同一件事，前者能回答"写了没人读"，后者在这一层没有定义。

**没有一个探针进过编译器**（读数是拿转储算的，两次 dump 相同即自证无副作用）。

## 5. 为什么要问，而不是挑一处说"就是它"

**不定则任一处都能被说成"就是它"，那就是编。** 现在能给的是三处带条件的位置与一条读数；
要给"在哪一步"，还差作者一句话：**(i) 还是 (ii)**，以及**症状**是"编译报错"还是"图里一个没人读的节点被下游拒"。

**已推进到：扫完 GIR 层**（24 条 generator 用例，19 条绿的全部只有 EXIT 无读者；5 条拒绝用例无图可读），
**字节码层无此概念，结论到 GIR 为止**。下一层若要读，需要先定义那一层的"读"。
## 6. 作者 13:50 指明出处（本页结论不变）

作者 13:50 说明他说的那一件是 **`stream.*` 11 条在 Windows 上崩在 `src/compile/gir/builder_support.h#nodeKindOf` 的
`nodeGraphOf()`（"Draft node is not owned by a compile graph builder"）**，Linux 上碰巧读得到已释放的内存所以不报；
**与 GCT 的 `extractData` 的 `dangling` 无关，与 `test/tools/progen` 也无关**。本页第 4 节那份 GIR 层扫描
（24 条用例、19 条绿、0 个非 EXIT 无读者节点）**是另一件事，结论照旧**。

## 7. `reportUnbuiltShape` 的现状与它卡在哪（预备改成一个说明语言规则的码）

- **现状**：`src/compile/gct/builder.cpp` 的 `visitGeneratorBody` 里有一个 lambda `reportUnbuiltShape`
  （`:5717`），报的是 **`InternalDiag::UnknownInternalError`** + 文案
  `"the generator machine does not build this shape; send the program, this is a compiler defect"`。
  另有 `:2164` 那条 `yield` 落语句构造器的路径，报的也是 `InternalDiag::UnknownInternalError`；
  **`FeatureNotSupported` 在 `diagnostics/registry.tsv` 零命中**（作者的"那个码已删"与实测一致）。
- **它有九个调用点**（`:6001`、`:6105`、`:6126`、`:6216`、`:6228`、`:6303`、`:6376`、`:6412`，加 lambda 本身）。
- **卡在哪：没有一个现成用例能触发它。** 五条 present-day 拒绝用例实测报的是别的码：
  `generators_cell_retype` → `SemanticDiag::NoMatchingFunction`、
  `generators_read_before_declaration` → `SemanticDiag::UnresolvedReference`、
  `yield_head_nested_no_stream` / `yield_head_no_stream` → `SemanticDiag::YieldRequiresStreamHead`、
  `yield_keyword` → `SyntaxDiag::KeywordAsIdentifier`。
  所以新码要落地，**必须先写出一条能走到那九个分支之一**的生成器程序（这就是 `--coverage-check` 要求的"有产出者的用例"）。
  下一步读物：那九个分支各自接哪种语句形状（逐个读条件），再挑最好构造的一种写成用例。

**已推进到：出处对齐 + 现状 + 卡点（缺一个会红的用例）。**
