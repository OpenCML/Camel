# 一个 block 的名字读的是什么

`blockFormHash` 给一具 body 取的名字（`block:{:016x}`）是 `BlockPrograms::add` 的唯一键，而
`add` 遇到同一个名字只留第一个程序：两个不同的 body 撞在一个名字上，第二个静默地跑第一个的
程序。所以"这个名字到底读进了 body 的哪些部分"是一条正确性条款，不是摘要。

这一页记四件事的四组读数：一次调用之后的两具 body、区域的形状端口、两份重复的哈希代码、以及
一个调用 block 的程序被导出成 C++ 时的行为。每处都给出改前与改后的原文。

这些数值是**在那次改动落地的树上**量的：改前取 `b5bfb906c`（加两条 instrument 提交），改后取
同一棵树加上这条车道的改动之后、重底之前的那次运行。数值本身随规范形式变动——之后换的底
（`236d795eaf`）带着另一批规范形式的改写，所有 block 名字整体换了一批十六进制；这里报的是**关系**
（两具 body 两名还是同一名、`portable=` 是否逐字不变），而关系不随底浮动。

| 读到的 | 改前 | 改后 |
| --- | --- | --- |
| 只差"调用之后用哪个参数"的两具 body | 同名 `block:ffd88c0239b6412c` | `block:7cdd0ca534a93d7f` / `block:dbaa52c7fa4eaf1d` |
| 只差"循环条件读哪个状态"的两具 body | 同名 `block:6d047bdeb540ebf6` | `block:d9a601cbdb793159` / `block:11254c8599132514` |
| 只差分支某一臂的两具 body | 已经两名（`block:947b3206386be1b9` / `block:f0745acf619af92f`） | 不变（区域走遍后重算为 `block:64713b41873350de` / `block:a4f51ff65c8170a3`） |
| 一个调用 block 的程序交给 `std::cpp` | 导出成功（exit 0），写出一条运行期解析不到的桥接调用 | 定位到该调用的 `TranslationRefused`（exit 1） |

## 1. 名字读的是什么

`src/kernel/runtime/block_form.cpp` 里的一次走遍，从图体的 `exitNodeRef` 起按
`normInputsOf` / `withInputsOf` / `ctrlInputsOf` 递归，先混入图的函数类型 mangle、端口数、返回
种类，每个节点混入它的 kind、flags、类型 mangle、权限、属性，运算符节点混入 URI 与
`operatorTraitBits`，`Fill` 节点混入每个槽的参数位置（位置而不是名字：同一具 body 的两个参数
可以带同样的负载），`Call`/`Func` 混入被调图的 form，然后才是子节点。

区域曾经完全不进这个名字：一具 body 的语句在区域里（`graph.h` 的 `regionCount()` /
`region(id)` / `regionPorts(id)`），而走遍只走节点的输入端口，不读区域。现在每个节点在自己
的端口之后混入它拥有的区域：`hashRegion` 混入 `"region"`、区域 kind、`index`、
`params->mangle()`、形状端口的个数与每个端口节点的 form、以及区域交出的那个值
（`region->result`）。区域的语句仍然不是被逐条列出来的——它们通过那个值被走到：区域的 exit
GATE 读它交出去的值，并保留它必须不丢的写；臂和循环体因此按"所属节点的端口顺序"进入，与
设计原则 9.5 的"臂与循环体按所属节点的端口顺序进入"一致。

## 2. 读数一：一次调用之后

两具 body 只差一件事：调用之后读哪个参数。

```cml
def after_call_left(a: int, b: int, f: int -> int) -> int:
    r = f(a)          # 左边
    return r + a      # 读 a
def after_call_right(a: int, b: int, f: int -> int) -> int:
    r = f(a)
    return r + b      # 读 b
```

改前（base `b5bfb906c` 加上那两具 body 的读数）：

```
BLOCK_TRAITS after_call_left uri=block:ffd88c0239b6412c ... portable=92f67587aa9c0c49
BLOCK_TRAITS after_call_right uri=block:ffd88c0239b6412c ... portable=c43d3b35ef4454a7
```

同一个名字，两个 portable 图哈希。原因在走遍的一处状态：它维护一张"端口 → 签名里第几位"的表
来区分参数节点，而走进被调的图时清空并填成被调图的端口，回来时不复原；于是调用之后读的
`a` 与 `b` 都是按被调图的签名读的，两具 body 混出同一串字节。修法是把外层那张表在走进被调图
前收起来、走完放回。改后：

```
BLOCK_TRAITS after_call_left uri=block:7cdd0ca534a93d7f ... portable=92f67587aa9c0c49
BLOCK_TRAITS after_call_right uri=block:dbaa52c7fa4eaf1d ... portable=c43d3b35ef4454a7
```

注意两行的 `portable=` 与改前逐字相同。那不是巧合：这一处修的是 block 名字的走遍，portable 图
哈希是另一个函数，两边都没有动。**portable 相等**是"这次改动没有动到别处任何字节"的硬证据，
它比"跑绿了"更强，因为一个绿的计划也可能只是没有覆盖到。

## 3. 读数二：区域里的差异曾经进不了名字吗

`hot` / `chill` 这两具 body 只差分支某一臂：

```cml
def hot(a: int, n: int) -> int:
    s = 0
    loop a < n do
        s = s + a
        a = a + 1
    if s > 0 then s else 0 - s
def chill(a: int, n: int) -> int:
    s = 0
    loop a < n do
        s = s + a
        a = a + 1
    if s > 0 then s + 1 else 0 - s
```

这两具 body 改前就已经是两个名字：

```
BLOCK_TRAITS hot uri=block:947b3206386be1b9
BLOCK_TRAITS chill uri=block:f0745acf619af92f
BLOCK_TRAITS hot_again uri=block:947b3206386be1b9
```

原因是臂的语句并非不可达：JOIN 通过自己的 with 端口读臂交给它的值，而那是一条普通的值边，
所以"走节点的输入端口"本来就把臂走到了。区域不是走遍的入口，但这不等于区域里的东西不在名字
里。

真正复现的缺口在另一处：区域的**形状端口**没有位置身份。区域的形状端口（循环的状态值）是
区域自己的端口，不在图体的 `normPorts()` / `withPorts()` 里，所以旧表里没有它们，`hashNode`
把它们当普通节点处理——而它们的负载在循环的左右两半里可以一样。两具只差"循环条件读哪个状态"
的 body 因此同名：

```
BLOCK_TRAITS loop_state_left  uri=block:6d047bdeb540ebf6 ... portable=4172bcca81b0e35e
BLOCK_TRAITS loop_state_right uri=block:6d047bdeb540ebf6 ... portable=18547d7eada1fc7b
```

改后：`positionsOf` 先放图体的 norm/with 端口，再对每个活区域按 `regionPorts(id)` 的次序放它的
形状端口（位置在每个区域内从 0 数起），于是名字里每个参数节点都是"签名里的第几位"；同时区域
本身以 kind、index、参数 mangle、形状端口序列和交出的值进入。读数：

```
BLOCK_TRAITS loop_state_left  uri=block:d9a601cbdb793159 ... portable=4172bcca81b0e35e
BLOCK_TRAITS loop_state_right uri=block:11254c8599132514 ... portable=18547d7eada1fc7b
```

portable 两行仍然逐字相同，名字分开了。

## 4. 两份打包、两份 FNV、一个没人读的字段

特征位（`pure`、`elementwise`、`stateOnly`、`mayFault`、`callsGraphs`、`readsGraphs`、
`mayNotTerminate`）在两条哈希路径上各被打包一次，而两份已经漂了：portable 图哈希
（`src/kernel/runtime/fact_persist.cpp`）打包了 7 位，block 名字（`block_form.cpp`）只打包了
5 位——`callsGraphs` 与 `readsGraphs` 被静默丢掉。于是同一个运算符对两条都"按特征"命名的哈希
不是同一个值。现在两边都调用 `camel::core::operatorTraitBits`
（`src/kernel/include/camel/core/operator_table.h`），一份打包；缺 traits 时是 `0xFF`，七位全
真时是 `0x7F`。案例里由 `BLOCK_BITS stated=7f unknown=ff` 这一行把布局读成文本。

FNV-1a 的混入同样是两份同样的结构（`block_form.cpp` 与 `fact_persist.cpp` 各一个匿名
`Fnv`），现在是 `src/kernel/runtime/fnv.h` 里的一个 `Fnv1a`，注释写明它与两个读者。图里还剩
两处 ad-hoc 混入（`src/kernel/runtime/graph.cpp`、`src/kernel/runtime/reachable.cpp`），它们混
的是图 id 与修订号、不是特征位，这页的改动不碰。

`operator_record.h` 的 `Record` 上有一个 `bool mayNotTerminate = false;`，写它的人没有（
`CppRegistration` 没有相应 setter），读它的人也没有（作用归结从
`OperatorTraits::mayNotTerminate` 读，那张表从运算符语义推导该位）。一个没人读的字段是第三个
"说法"，读起来却像一个："终止性在这里说"。它删掉了，`src/kernel/facts/facts.cpp` 的注释改成
说明这个词来自语义、以及为什么不是记录上的字段。

计数（`src/` 下）：`mayNotTerminate` 31 行 → 29 行；`bool mayNotTerminate` 的定义 4 处 → 3 处
（`operator_semantics.h`、`operator_table.h`、`effects.h`，都是真被读的那几个）；FNV 结构定义
2 处 → 1 处；特征位打包表达式 2 处 → 1 处。

## 5. 生成的程序里的 block 运算符

`std::cpp` 把表里没有规则的运算符写成运行期模块的桥接调用
（`src/stdlib/std/cpp/function_emitter.cpp`），桥接侧
`src/stdlib/std/cpp/standalone_bridge.cpp` 的 `ArgsOfCall` 从不设置
`::ArgsView::setCallOperator`，所以 block 的内核看到的 `norm.oper()` 是 `kNoOper`，只能答 -1。

把编号设上并不能修好它。block 的运算符编号指的不是"一条实现"，而是**那个编译过程上下文里
的 block store 条目**：`src/kernel/runtime/block_body.cpp` 的内核拿
`ctx.blockPrograms().byOper(norm.oper())` 找程序。生成程序用自己的上下文（从安装目录建），
其中没有任何 block——block 是编译期 pass 的产物。即使编号设上，调用也在更早的一步就死了：
`operatorFor(uri)`（`standalone_bridge.cpp`）在生成程序的上下文里找不到 `block` 模块的
执行器，先抛 `PassInternalError`。改编号只是把一个"没有实现"换成一个更晚出现的"没有实现"。

所以选的是明确拒绝：导出时问上下文的 block store"这个 URI 是不是你的某个 block"
（`blockPrograms().byUri`，store 自己的答案，不是翻译器去匹配名字），是就在节点处
`TranslationRefused`，理由说清生成程序缺什么、以及该把 body 写成函数。改前改后的读数：

```
改前  [FAIL] ...the_cpp_export_refuses_a_block_operator
      [INFO] expected exit 1, got exit 0
      [INFO] output does not contain 'Translation pass 'std::cpp' refused the graph'
改后  [PASS] runtime.block_body.the_cpp_export_refuses_a_block_operator
```

## 6. 名字里仍然没有的东西

- **区域里语句的顺序**：区域的交出口是一个 GATE，它的端口列表就是源码里那些写的顺序
  （`src/kernel/passes/canonical.cpp` 的注释把它称作一个"spelling-order tie group"，只有在
  平票时才由 `arrange()` 重新定序）。走遍跟着那张列表走，所以两具只有"区域内两个写的先后"
  不同的 body 仍然拿两个名字。这不是"读少了"，是"读多了一种拼法"——要消掉它得让规范次序先
  定序，再按定序走。这页没有动它。
- **无人读的纯计算**：区域里没人读的纯计算不在可见结构里（设计原则 9.5），也就不在名字里。
  两具只差一条死语句的 body 是同一个 block，这是有意的。
- **内联区域被调方的身份**：`GCRegionKind::Inline` 的区域按 kind/index/参数/形状端口/交出的值
  进入，不混被内联那具图的 form。block 的 body 是现编的，其中不会有 Inline 区域，所以这页
  没有为它写读数。