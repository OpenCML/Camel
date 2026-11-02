# 「生成器悬垂节点」指的是哪一件事：`yield` 状态机降级出的无人读节点，还是 `test/tools/progen` 生成的程序里的悬垂？

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

（`src/compile/gct/builder.cpp:2205`，同样四行还出现在 `:4202`、`:4860`、`:4916`、`:4983`、`:5091`。）

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

**边界**：这是**一条用例、GIR 一层**；字节码/VM 层与其它 yield 用例未读，且没有一个探针进过编译器
（这条读数是拿转储算的，两次 dump 相同即自证无副作用）。

## 5. 为什么要问，而不是挑一处说"就是它"

**不定则任一处都能被说成"就是它"，那就是编。** 现在能给的是三处带条件的位置与一条读数；
要给"在哪一步"，还差作者一句话：**(i) 还是 (ii)**，以及**症状**是"编译报错"还是"图里一个没人读的节点被下游拒"。

**已推进到：读数**（一个 yield 用例的 GIR 层：无读者节点只有 EXIT，两次 dump 相同）。