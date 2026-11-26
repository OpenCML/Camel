# 2026-11-26 头文件的分量：两周改了几次 × 改一次重编几个 TU

这一页只做第一件事：**量**。给"改包含关系本身"这件事一份可复现的依据——
哪些头文件在被频繁改动，每改一次要重编多少个 TU，这两件事乘起来最贵的是哪几个，以及它们为什么被包含。

## 一句话

按「被改次数 × 重编 TU 数」排，前 8 名**没有一个是 `src/compile` 的私有头**；最贵的三条全在
`src/kernel/include` 的公共头里，而它们贵的原因不是"被包含得深"，是**三个转手头各替几百个 TU 拿了一份**，
且这三个转手头自己**只用到类型名、或一个名字都没用到**：

```
core/type/resolver.h      478 个 TU 经由它拿到 messages/semantic.h，而它只把 SemanticDiag 用作返回类型
core/context/frame.h      246 个 TU 经由它拿到 runtime/graph.h，而它只用到 GCGraph* 这一个类型
core/operator_traits.h    423 个 TU 经由它拿到 core/operator_table.h，而它正文一个名字都没用到
```

## 读数从哪来

```
窗口        2026-11-12 00:00:00 +0800 .. 2026-11-26 23:59:59 +0800（--since / --until，时区显式写出）
被量树      73aebe80b3ad26b2a4d225dfae1c5adbf83a9bb6（2026-11-26T11:48:07+08:00，db 就是这棵树构建时写的）
            本工作树 HEAD 的 *.h 与它逐字节相同：git diff --numstat 73aebe80b3ad..HEAD -- '*.h' 的输出为空
compile db  /home/zjwei/camel-work/73aebe80b3ad26b2a4d225dfae1c5adbf83a9bb6/build/compile_commands.json
            mtime 2026-11-26 12:18:40 +0800（主机时钟 2026-11-25 23:18:40 -0500）
            2232 条 = 659 个源文件 × 3 个配置（Debug / Release / RelWithDebInfo）
编译器      clang 23.1.2（db 里那条命令行的第一个词，就是这个文件）
```

## ① 近两周每个头文件被改的次数

```bash
git -C <tree> log --since="2026-11-12 00:00:00 +0800" --until="2026-11-26 23:59:59 +0800" \
    --no-merges --name-status -M --pretty=format:'C %H' -- '*.h'
```

```
415 个提交改过 *.h（窗口内另有 245 个合并提交；合并提交不列文件，所以这里一次不多一次不少地数的是
    "真的改了这些字节的提交"）
2219 条（提交, 路径）记录 → 归并到 685 个头文件
   其中 276 个的字节被改过；409 个只是被 2026-11-22 的整树搬迁（0109b74b738）改了路径
```

**路径要先归一。** 这一搬迁把 `include/camel/x.h` 挪成 `src/kernel/include/camel/x.h`（中间还经过
`kernel/include/camel/x.h`），所以单个头文件的次数必须把它**所有历史名字一起**传给 pathspec，按提交去重：

```bash
git -C <tree> log --since=… --until=… --no-merges --pretty=format:%H -- \
    src/kernel/include/camel/runtime/graph.h kernel/include/camel/runtime/graph.h include/camel/runtime/graph.h \
  | sort -u | wc -l
# ⇒ 31
```

改得最多的 10 个头文件（`次数` 即上面那条命令；括号里是其中纯搬迁的提交数）：

| 头文件 | 次数 |
|---|---|
| `src/compile/src/gir/builder.h` | 93 (2) |
| `src/kernel/include/camel/runtime/draft.h` | 38 (2) |
| `src/kernel/include/camel/runtime/graph.h` | 31 (2) |
| `src/kernel/include/camel/core/error/diagnostics/messages/semantic.h` | 26 (1) |
| `src/kernel/include/camel/runtime/exec_plan.h` | 26 (2) |
| `src/compile/src/gir/builder_helpers.h` | 23 (2) |
| `src/kernel/include/camel/runtime/resources.h` | 23 (2) |
| `src/kernel/include/camel/core/operator_table.h` | 22 (2) |
| `src/stdlib/test/include/camel/passes/test/commit_checks.h` | 18 (2) |
| `src/kernel/include/camel/core/error/diagnostics/messages/runtime.h` | 14 (1) |

**改得最多 ≠ 最贵。** `builder.h` 两周改 93 次，排第一；但它只被 33 个 TU 包含（它是 `builder*.cpp`
的私有头，自己只包含 3 个头），乘出来 3069，排第 10。

## ② 每个头文件改它要重编多少 TU

一条 TU 的包含树，用 db 里那条命令行**自己的**编译器与开关算出来（只预处理，不出目标码）：

```bash
cd /home/zjwei/camel-work/73aebe80b3ad26b2a4d225dfae1c5adbf83a9bb6/build
/home/zjwei/camel-work/bin/clang++ <该条目全部编译开关，去掉 -c -o -MD / 保留 -I -D -std> \
    -H -w -M -MF /dev/null <该条目的源文件> 2> tree.txt
# tree.txt 每行 = `<点号><空格><路径>`；点号个数就是包含深度：
#   1 个点 = 这条 TU 的源文件自己 #include 的  ⇒ "直接"
#   ≥2 个点 = 经别的头进来的             ⇒ "传递"
# clang 不打印 TU 的源文件本身，所以最浅一层就是直接包含。
```

全 2232 条都跑一遍（jobs=8，132 秒，0 个失败），按 TU 归并后：

```
660 个头文件被至少一条 TU 打开（树里 tracked 的 *.h 是 684 个，另有 2 个 build/ 下生成的 build_config.h）
   658 = tracked，26 个 tracked 头没有任何 TU 打开（15 个 stdlib standalone、6 个 test fixture、
         2 个 compile/gir 图类型头、3 个零散头）
659 个 TU（树里 664 个 .cpp；5 个不参与构建：docs/design 的 3 个原型、1 个 test 计划夹具、1 个 test 工具）
```

**"直接"与"传递"是两个数，不是一回事，而且往往差两个数量级：**

| 头文件 | 直接 TU 数 | 传递 TU 数 | 合计 |
|---|---|---|---|
| `messages/semantic.h` | 2 | 497 | 499 |
| `runtime/graph.h` | 9 | 350 | 359 |
| `core/operator_table.h` | 14 | 421 | 435 |
| `utils/assert.h`（两周一次没改） | 2 | 568 | 570 |

合计 = 直接 + 传递。`semantic.h` 只有 2 条 TU 按名字包含它——改这两条改不动 499，**能改的是那两个
把它替别人拿过去的转手头**。这一栏是这一页最有用的一栏。

**为什么不用 `-I` 能解析到它来数。** 也数了，但那个数什么都分辨不出来：每条 TU 的 `-I` 是一张 130 个目录的
扁平大表，660 个头文件**每一个**都读作同一个数 658——
`builder.h`（真的只被 33 条打开）与 `assert.h`（570 条打开）都读作 658。`-I` 说的是"这个名字可能被找到"，
`-H` 说的是"编译这条 TU 时真的打开了它"，重编取决于后者。

三个配置各算一份，`Debug` 与 `Release` 的读数：**前 8 名完全相同**，全表只有 9 个头文件的合计不同
（都是 `src/tools/profiler` 与 `core/debug_breakpoint.h` 这类只在调试配置里被打开的头，最多差 9）；
659 个 TU 里有 15 个的包含树跨配置不同。下表取 `Release`。

## ③ 排名：被改次数 × 重编 TU 数

| # | 头文件 | 两周被改次数 | 直接 TU 数 | 传递 TU 数 | 乘积 | 包含原因分类 |
|---|---|---|---|---|---|---|
| 1 | `src/kernel/include/camel/core/error/diagnostics/messages/semantic.h` | 26 | 2 | 497 | **12974** | 只要类型名（可前向声明）；其余 380 走伞形 |
| 2 | `src/kernel/include/camel/runtime/graph.h` | 31 | 9 | 350 | **11129** | 只要类型名（可前向声明）；另一条载体必须整套 |
| 3 | `src/kernel/include/camel/core/operator_table.h` | 22 | 14 | 421 | **9570** | 只用一小块（可拆）：一条转发包含拿走 267 |
| 4 | `src/kernel/include/camel/runtime/draft.h` | 38 | 19 | 143 | **6156** | 必须整套（`id()` 内联是 ABI 需要，挪不走） |
| 5 | `src/kernel/include/camel/core/resource_semantics.h` | 10 | 2 | 548 | **5500** | 必须整套（载体按值存 `sem::ResourceSignature` 成员） |
| 6 | `src/kernel/include/camel/core/error/diagnostics/messages/runtime.h` | 14 | 2 | 380 | **5348** | 只用一小块（可拆）：伞形 `messages/index.h` 一次带进 5 族 |
| 7 | `src/kernel/include/camel/core/error/diagnostics/messages/syntax.h` | 10 | 2 | 380 | **3820** | 同上 |
| 8 | `src/kernel/include/camel/core/operator_semantics.h` | 8 | 5 | 470 | **3800** | 只要类型名（载体只用 `const sem::OperatorSemantics *`） |

乘积 = 次数 × 合计。第 1–3 名的分类核到了"某个转手头用到它的哪个名字、在第几行"；第 4–8 名的载体
是量出来的，用法也核到了行号，逐条记在 ④ 之后那一节。

## ④ 前 3 名：为什么被包含，该怎么拆

### 1. `messages/semantic.h`（499 个 TU）— 只要类型名

两条边覆盖了 499 条中的 497 条，而且**大量重叠**——把其中一条拿掉，只有"只有这一条路"的那部分会掉：

```
core/type/resolver.h:6   → 478 个 TU 经由它；其中只有 117 个是"只走这一条"（拿掉它 ⇒ 499 变 382）
messages/index.h（伞形）  → 380 个 TU 经由它；其中只有  19 个是"只走这一条"（拿掉它 ⇒ 499 变 480）
                           再往上只有一条上游：core/error/diagnostics.h
361 个 TU 两条路都在，2 条 .cpp 直接包含（core/error/diagnostics/base.cpp、messages/semantic.cpp）
```

`resolver.h` 里用到 `semantic.h` 的**全部**内容是第 201 行的一个返回类型：

```cpp
virtual std::optional<camel::core::error::SemanticDiag> rejectionDiag() const { … }
```

`DiagInfo`、`getSemanticDiagInfoMap` 一次都没出现。这个枚举有固定底层类型（`: uint32_t`），
所以不完整声明就够用。

**拆法：** `src/kernel/include/camel/core/type/resolver.h:6` 的 `#include "camel/core/error/diagnostics/messages/semantic.h"`
换成 `enum class SemanticDiag : uint32_t;` ⇒ **499 → 382**（-117）。

**再拆一层：** `messages/index.h` 把 5 个消息族一次全带进来，而它只有一条上游。让
`core/error/diagnostics.h` 按族包含（谁要哪族自己包含）⇒ `semantic.h` **499 → 2**。
这一条是"可拆小头"的真正对象；但它的收益要先在**每个真的用某族常量**的文件上把包含补回来，
所以 2 是这条边单独拿掉时的下限，不是落地后的最终数。

### 2. `runtime/graph.h`（359 个 TU）— 两条载体，一条只要类型名

```
runtime/program.h      257 个 TU   必须整套：内联调用 graph.refs() / graph.outerGraphId()
core/context/frame.h   246 个 TU   只要类型名
runtime/node_ref.h     170 个 TU   （同类，未逐个核）
execute/pass/base.h    116 个 TU   （同类，未逐个核）
```

`core/context/frame.h` 里 `graph.h` 的用处只有指针：第 44–47 行把 `GCGraph*` 交出去，第 323 行存成成员，
第 391 行比 `sizeof(GCGraph*)`，第 448–528 行的 `_acquire(GCGraph*)` 转手传下去。`WritingUnion`、
`gc_slot_idx_t`、`GCFuncRef`、`PortRole`、`GraphId` 这些 graph.h 定义的名字，在这个文件里出现 0 次。

**拆法：** `src/kernel/include/camel/core/context/frame.h:9` 的 `#include "camel/runtime/graph.h"` 换成 `class GCGraph;`
⇒ **359 → 316**（-43）。`program.h` 那条不能这么拆——它真的要读 `GCGraph` 的成员，只能靠把
`refs()`/`outerGraphId()` 这类小函数挪进 .cpp 才动得动，那是第二步的事。

### 3. `core/operator_table.h`（435 个 TU）— 一条转发包含拿走 267

```
core/operator_traits.h  423 个 TU  ← 这一条边造成 267 条 TU 的全部损失
graph/graph_view.h      107 个 TU
compile/gct/tree.h       45 个 TU
```

`core/operator_traits.h` 第 14 行包含 `operator_table.h`，理由写在它自己的第 7–9 行：
**"让调用者不必自己包含"**。而它正文（`ValueTypeRefiner`、`ValueTypeRefinerRegistry`，共 45 行）
**一个 `operator_table.h` 定义的名字都没用到**。

**拆法：** 删掉 `src/kernel/include/camel/core/operator_traits.h` 第 14 行那条转发包含，谁用 `oper::` 的 id 常量谁自己包含
⇒ **435 → 168**（-267），同时把 `operator_semantics.h` 从 475 拉到 265（它顺着这条链进来）。
收拢前要确认的是那 423 个使用方里谁真的用了 operator 表的名字——用了的补在自己的文件里，
那个数才是落地后的数；168 是这条边单独拿掉时的读数。

### 第 4 至 8 名：原因，与为什么现在不动它们

```
draft.h             载体 runtime/graph_txn.h（142 TU）有成员 std::unique_ptr<GraphDraft> graph_，
                    且 id() 内联调用 GraphDraft::id()；该文件第 46–48 行写明这个内联是
                    "出到 .cpp 就不进共享库、模块加载要断符号"——所以不能靠前向声明拆。
resource_semantics  载体 core/type/composite/func.h（550 TU）第 154 行按值存
                    sem::ResourceSignature resources_，要完整类型；只有改存储方式才动得动。
messages/runtime.h  与 syntax.h 同形：380 条走 messages/index.h 这条伞形边，2 条直接包含。
operator_semantics  载体 operator_table.h 只用 const sem::OperatorSemantics *（第 344/417/423/430 行），
                    类型名级；要拆得先把 operator_table.h 里那些指针的使用挪走。
```

## 结论

拆前 3 名——`resolver.h` 里把 `semantic.h` 换成一个枚举的前向声明、`frame.h` 里把 `graph.h` 换成
`class GCGraph;`、删掉 `operator_traits.h` 第 14 行那条转发包含——**一次语义性头文件改动的重编 TU 数**从
**499 / 359 / 435 降到 382 / 316 / 168**（三名合计 **1293 → 866**，-33%）；
最贵的一条 `semantic.h` 若连 `messages/index.h` 那条伞形包含一起拆，是 **499 → 2**。

## 这份读数会在哪里偏

```
· 次数口径：`git log -- '*.h'` 在合并提交上不列文件，所以数的是"改过这些字节的提交"。
  单条 `git log -- <path>` 会把合并也算上（builder.h 会读成 128 而不是 93）——两个口径不是一回事，
  排名用前者，因为后者把一次改动记两次（分支上那笔 + 把它合进来的那笔）。
· 树与 db 不是同一棵：*.h 逐字节 0 差异；*.cpp 有 11 个在 db 生成后被改（都是本树日常改动，
  没有新增/删除 TU）。这 11 个 TU 的直接包含若变了，个别数字会差一两个。
· 26 个 tracked 头没有任何 TU 打开 ⇒ 它们在②里没有数，不是数出来 0；表里也只有进了前 8 的。
· 659 个 TU 是"每个源文件一次"，db 里每个源文件有 3 条（三个配置）。要按"一次构建编几个目标文件"数，
  乘以该配置的条数即可，排序不变。
```

## 复现

```bash
# 一条 TU 的包含树（证明"传递"不是猜的）
cd /home/zjwei/camel-work/73aebe80b3ad26b2a4d225dfae1c5adbf83a9bb6/build && python3 - <<'PY'
import json, shlex, subprocess
db = json.load(open('compile_commands.json'))
e = [x for x in db if x['file'].endswith('src/kernel/utils/assert.cpp')][0]
a = shlex.split(e['command']); rest, i = [], 1
while i < len(a):                      # 去掉 -c / -o / -MD，保留 -I -D -std
    if a[i] in ('-o', '-MF', '-MT', '-MQ', '-MJ'): i += 2; continue
    if a[i] in ('-c', '-MD', '-MMD', '-E', '-S'):   i += 1; continue
    rest.append(a[i]); i += 1
r = subprocess.run([a[0]] + rest + ['-H', '-w', '-M', '-MF', '/dev/null'],
                   cwd=e['directory'], stderr=subprocess.PIPE, stdout=subprocess.DEVNULL)
for line in r.stderr.decode().splitlines()[:8]:
    print(line)
PY
```

全表 2232 条照上面同一段跑（`-H` 的树按点号个数分层，1 = 直接、≥2 = 传递），
再按上面 ① 的 git 命令求次数，两者相乘就是③那张表。