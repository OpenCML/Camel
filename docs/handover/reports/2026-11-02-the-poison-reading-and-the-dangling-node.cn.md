# `--gc-check poison` 的读法，和"生成器悬垂节点"定位到的那一处

> 三件同族的事，一笔记下来：**这条旗怎么用（给 `test.js` 加旗无效）**、**本树上两条读数的原文**、
> 以及**作者 12:50 点名的那项"生成器悬垂节点"在树里只对应一处，而那一处不拒绝任何东西**。

## 1. `--gc-check` 怎么跑：选项在 target 前，一个旗只收一个值

```
./out/latest/bin/camel --gc-check poison <case> std::nvm
```

- `--gc-check` 收且只收一个值，取自 `moves`、`poison`、`verify=before|after|both`、`config`
  （`docs/cli.md:59`）。所以 `--gc-check poison --gc-check config` 不是"两个检查"，是**非法命令行**：

  ```
  Error: invalid command line: '--gc-check' 'config'
  ```

- 旗被解析、且能跑绿（`poison` 蕴含分代收集器，每个安全点移动全部年轻对象并毒化原处）：

  ```
  $ ./out/latest/bin/camel --gc-check config /tmp/poison_ok.cml std::nvm
  [camel] GC config: youngCopying=true verifyBefore=false verifyAfter=false stressAlloc=0
          stressSafepoint=0 stressMode=major logMoves=false poison=false
  $ ./out/latest/bin/camel --gc-check poison /tmp/poison_ok.cml std::nvm
  4
  poison-exit=0
  ```

## 2. **给 `test.js` 加旗无效** —— 下一个人不该再试这一遍

```
$ node scripts/test.js <plan> --tier fast --gc-check poison
Error: ENOENT: no such file or directory, stat '/home/zjwei/camel-work/resid/--gc-check'
    at loadPlans (test/tools/run-tests.mjs:2563:16)
```

runner **没有全局 poison 旗**：它把 `--gc-check` 当成一个**计划路径**。poison 是**按条目**加的参数 ——
`test/plans/feat/mm/gc_poison_sweep.plan.toml` 给自己抽样的每一条副本加 `args = ["--gc-check","poison"]`
（配 `skip_args_prefix = "--gc-"`，免得把条目里已有的 `--gc-*` 叠两次）。所以：

- "某个计划在 poison 下全绿"只能**从 sweep 那份计划读**：fast 层是每个计划抽若干条的样本，
  全扫是 `--tier milestone`（`npm run test:all` 里那些未抽样的副本是 milestone 层）。
- 想让某条目单独进 poison，是**改/加一条带 `args` 的计划条目**，不是给命令行加旗。

## 3. 本树上的两条读数（原文）

**`transpose(lin)`（作者 11:30 点名的读数，属 b2，不属残差这笔）**：程序是我补的
（`/tmp/resid_readings.cml`，`f(x, y) = x*x*y`，`camel … std::fvm`）：

```
value=  18.0
lin(1,0)=  12.0
lin(0,1)=  9.0
error[RuntimeDiag::RuntimeError]: transforms: transpose has no transpose for this function.
   --> /tmp/resid_readings.cml:15:40
stderr: TRANSPOSE function=split_of_jvp_of_f_backward skip=call node=8 arg=4
exit=1
```

`lin(1,0)=12`、`lin(0,1)=9` 是这一笔的读数，它们对了；`transpose(lin)` 要对 `split`/`jvp` 这套机制求转置，
那是 `transpose` 那股（b2）的活。另：转置后的类型是 `(float, float, float) => (float, float)` ——
**参数在前、余切在后**，所以调用形如 `transpose(lin)(3.0, 2.0, 1.0)`，不是 `transpose(lin)(1.0)`。

**`remat.split`（poison 与不加，逐字相同的红）**：

```
$ ./out/latest/bin/camel --gc-check poison test/cases/transforms/remat/remat_split.cml std::nvm
error[SemanticDiag::TypeNotIndexable]: Type '(float, float) => (float, (float, float) => float pure) pure'
       does not support index access.
   --> test/cases/transforms/remat/remat_split.cml:23:5
 23 |     let (plainForward, plainBackward) = split(lifted)
poison-exit=1
```

不加 `--gc-check poison` 时**同一行、同一个 exit 1**；计划层 `[SUMMARY] total: 6, pass: 4, fail: 2`。
红在用例的旧 `split` API（`split(f)` 现在答一个函数，不再答两个半段），**poison 起不来** ——
这条证据说明"poison 不是这条红的原因"，也说明"poison 下全绿"在本树拿不到。

## 4. 收集器"按上界扫"的兜底：**未找到**，这是三条搜索

作者说"同一笔删掉"。搜不到就不硬删，证据如下：

1. `git grep -n -i 'upper' -- src/runtime src/compile` —— 6 处，全是流格式（`std::uppercase`）、
   `builder.h` 关于"递归体未闭合时写入集只是上界"的注释，与 `src/compile/literal.cpp:487` 的十进制位循环；
   **没有收集器按上界扫的循环**。
2. `git grep -n -i -E 'fallback|safety|in case|conservative|upper|scan' -- src/core/mm` —— 唯一"扫到末尾"
   的是 `include/camel/core/mm/alloc/free_list.h` 的 `Step 2: scan the entire memory region`
   （`while (UNLIKELY(scan < end_))`，任何不一致即 `return false`，末尾还有 `if (scan != end_) return false;`）。
   它是**分配器自检**（`verify`），不是收集器的兜底。
3. `git grep -ln 'sweep' -- src include` —— `src/core/mm/{gen,block,mm}.cpp`、`gc_debug.cpp`、
   `profiler.cpp`；逐个读过，**没有"扫到上界为止"的循环**（`gen.cpp` 的扫描都按空间/对象头推进）。

## 5. "生成器悬垂节点…在哪一步"：定位到唯一一处，而**那一处不拒绝任何东西**

**搜索**：`git grep -n -i 'dangling|orphan' -- src/compile src/passes docs/design-principles.cn.md`。
全树（排除 `docs/handover/` 归档）的 `dangling` 只落在 `src/compile/gct/builder.cpp`：

```cpp
pair<data_ptr_t, bool>
Builder::extractData(const node_ptr_t &node, node_ptr_t &execNode, bool &dangling) {
    auto [refData, dang] = extractData(node, execNode);
    if (dang) {
        dangling = true;
    }
    return make_pair(refData, dang);
}
```

**判词**：这不是一条检查，**没有拒绝点** —— 没有诊断、没有 `GUARD`、注册表里没有与之相关的码。
它的含义是"这个值的构造需要只有执行节点能做的事（一次调用、一个 `yield` 的副作用）"，于是每个调用点
在拿到 `dangling` 后**把它挂到执行节点上**，而不是报错：

```cpp
if (dangling) {
    *execNode << dataNode;
    dataNode = execNode;
}
```

（`src/compile/gct/builder.cpp:2205`，同样的四行在 `:4202`、`:4860`、`:4916`、`:4983`、`:5091` 各出现一次。）

所以"检查太严"这个形状在这里**不存在**：没有一条规则因为"悬垂"而拒掉程序，也就没有可放松的条件。
生成器自己那一侧有两个真实的拒绝点，都不是"悬垂"：

- `Builder::visitGeneratorBody`（`src/compile/gct/builder.cpp:5707`）对状态机建不出的形状点名叫人送程序：
  `"the generator machine does not build this shape; send the program, this is a …"`（`:5715` 一带）；
- 一个没被生成器机器认领的 `yield` 走到语句构造器时被 `FeatureNotSupported` 拒（`:2164`）。

**卡在哪一条读不出来的东西上**：**作者说的"生成器"是哪一个** —— 是语言的 `yield` 状态机
（`visitGeneratorBody`），还是 `test/tools/progen` 那个**程序**生成器（`git grep -i dangling -- test/tools/progen`
为空，它没有悬垂这个概念）？以及"悬垂节点"的症状是什么：编译报错、还是图里有一个没人读的节点被下游拒。
**这两点定了，我能一步走到"哪一步"**；不定，任何一处都能被说成"就是它"，那就是编。

**已推进到：定位（唯一一处 `dangling` 机制 + 它的完整条件与六个调用点）。**

## 6. 本轮同族的一笔（残差机制）的读数

`--check` 306 码（307 → 306）；`git grep -c 'ResidualsAreNotVisible|isResiduals|TupleType::residuals'`
从 21 个文件降到 0（只剩 `docs/handover/` 归档，未改写）；全量 fast `8515 → 8514`，而 `pass` 8450 不动
⇒ 唯一位移是"删掉那条条目与其红"，无一条绿变红。