# ECOOP 第 (1) 项：案例程序上两个**会被拒**的 pass（Camel 写，走图模块的 `open`/编辑/`commit`）

投稿日加急测量。对象 `internal/reports/eval-round10/case-study-variants/train.cml`，pass 库
`.../edits-ecoop.cml`（本次在其中**加**了六个 `export func`，原有的四条一条没改）。本页只记读数：源码、
命令原文、原始输出、四样。本机不构建，构建与运行全在主机 `88zjwei`。

> **先读这一句，否则本页会看起来自相矛盾。**
> **`build asks=0` 只覆盖"遍边界检查"那一层；commit 检查的档是「这次编辑加了什么」，在每个构建里都问。**
> 前者是 `commitChecksEnabled()`（`resources.cpp:1934`，`NDEBUG` 下 `false`，要测试主动 `asked=1` 才问）；
> 后者是 `commitViolation`（`draft_session.cpp:2013`，档由 `commitAddsAccess` 定，与构建无关）。
> 所以"`build asks=0`"与"甲、乙、`loss_print` 三条都被拒在这个 Release 上"**同时为真**，不冲突。详见 §2。

## 1 四样：构建标识、时刻、来源、前提

| 项 | 值 | 从哪读的 |
|---|---|---|
| 总体 | **甲被拒（冲突检查）· 甲对照被接受且输出正确 · 乙被判拒（但拒的是冲突检查，不是效应检查）· 乙对照被接受且输出正确** | 本页 §3 |
| 时刻 | `Camel v0.2.45 (Build 261119_053324_2d7c5c3a1)`；footprint `261119_053324_2d7c5c3a1`；`builtAt 2026-11-19T05:33:29.613Z`；读数在 `2026-11-19T05:36Z`–`05:41Z`（本机 `13:36`–`13:41 +08:00`） | `camel --version`；`out/latest/build-info.json` |
| 提交 | `2d7c5c3a146d0572670def9370a07d5ebe71d38a`，`dirty false`、`dirtyCount 0`，源指纹 `0244fca0cbacd5c78253c81a43e1e0f98f1c6cf5ac99097aee774fbbdbaf2c37`（1344 文件），Release | 同上 |
| 来源（命令） | 见 §4 逐条；全部是主机上的**手工命令行** `camel`，计划一条没跑 | §4 |
| 前提 | 主机 `88zjwei`（32 核），构建时 1m load **44.48 → 28.43**，之后 30 上下；`camel-remote.ps1` 打印 `WARNING: 1m load 28.43 > 8`。**本轮所有读数都是退出码与诊断原文，不是时间**，负载不影响它们 | 构建日志、`[load]` 行 |
| 输入来自哪 | **二进制 = `2d7c5c3a1`；案例文件取自 `1e6cfcb80`**（`train.cml` 与另外五个变体）**和 `365dfcda9`**（`edits-ecoop.cml`）。这两个提交都在 `2d7c5c3a1` **之后**（`2d7c5c3a1` 是 `1e6cfcb80` 的祖先），案例目录在那个 pin 上不存在；它们不是构建输入（指纹只覆盖 `src include modules tools stdlib third_party cmake`），所以二进制仍是 `2d7c5c3a1` | `git merge-base --is-ancestor`；`build-info.json.fingerprint` |

`train.cml` **没有被改动**：`git status` 里它是 `A`（新增，取自 `1e6cfcb80`），不是 `M`。

## 2 先回答作者问的那一行：`build asks=`

`2d7c5c3a1` 的 Release 上，跑那个会自己打印分档的 C++ 测试遍（`src/passes/test/commit_checks.cpp:800`）：

```
$ ./out/latest/bin/camel test/cases/opt/commit_checks/fold_function_type.cml test::camel_commit_checks std::nvm
fold_function_type: graph 'announced' now publishes '(x: int) => int pure', the type of graph 'doubled'; its body is unchanged
CAMEL_CHECKS build asks=0
CAMEL_CHECKS accepted, asked=0
CAMEL_CHECKS released: the run continued past opt::cse with the checks off
CAMEL_CHECKS asked=1 -> the same program, the same pass
error[RuntimeDiag::PassEffectContainment]: Pass 'opt::cse' returned a program whose call site reaches past the type it declares: node 3 of graph 'main' (a call of 'announced', which is 'noisy') declares pure; the graph it runs touches an effect outside the program, write stdout
help: A call site carries the type of the function value it calls, and a pass can leave a body under a type that no longer covers it -- a printing body under 'pure', which is what an inline that copies nodes with accesses, or a fold that merges two functions, leaves behind. The message names the call and the accesses the type does not cover: the type is the promise a caller relies on.
error[RuntimeDiag::PassEffectContainment]: Pass 'opt::cse' returned a program whose call site reaches past the type it declares: node 3 of graph 'main' (a call of 'announced', which is 'noisy') declares pure; the graph it runs touches an effect outside the program, write stdout
help: A call site carries the type of the function value it calls, and a pass can leave a body under a type that no longer covers it -- a printing body under 'pure', which is what an inline that copies nodes with accesses, or a fold that merges two functions, leaves behind. The message names the call and the accesses the type does not cover: the type is the promise a caller relies on.
EXIT=1
```

**首行逐字：`CAMEL_CHECKS build asks=0`。在 `2d7c5c3a1` 上 Release 不会自己问。**

**但这条不是本项的全部，判的时候要分清两个检查层：**

| 层 | 谁问 | 在这一跑里的分档 |
|---|---|---|
| 遍边界检查（`PassEffectContainment`，横跨整个调用点） | `commitChecksEnabled()` 那个格（`src/runtime/resources.cpp:1934`，`NDEBUG` 下 `false`） | **`build asks=0`：Release 不问**，要测试主动 `asked=1` 才问 |
| **commit 检查**（乙/甲的 `PassEffectBeyondType` / `PassEffectOrder`） | `commitViolation`（`src/runtime/draft_session.cpp:2013`），**每个提交都问**，档是"这次编辑加了什么"（`commitAddsAccess`），**与构建无关** | **本页甲、乙、`loss_print` 都被问并且都被拒**，就在这个 Release 上 |

`draft_session.cpp:1979-1983` 原文：*"Asked by what the draft added, not by what the build is (the ruling of
2026-11-18, which replaces the one of 2026-11-16): a draft that added an access is asked the effect and order
checks in every build, and a draft that added none owes no answer about them in any build."*

所以 **`build asks=0` 与"甲/乙被拒"不矛盾**：前者说的是遍边界那一层，后者是 commit 那一层。**这一条的读数
（`build asks=0`）确实是"按编辑分档之前"那份材料里没有的、今天量到的新状态，但它不覆盖 commit 层。**

## 3 四条读数

### 甲（`eco::print_unordered`）——**被拒**，退出码 **1**，冲突检查

`at` = `main` 里唯一的 `f32[4, 2]` 常量（纯节点，不在链上）；`inline` 一个打印体进去，不给任何排序。

```
error[RuntimeDiag::PassEffectOrder]: 'graph::commit' committed a graph whose conflicting effects are not ordered: node 3 of graph 'main' (a call of 'run'), in one region of graph 'main', has no order against node 19 of graph 'main' (:io/println), and at least one of the two writes (write unknown / write stdout): no data edge, no sync edge, no order edge and no region's GATE input makes one wait for the other, so the commit would publish two effects on one resource that the program leaves free to happen in either order
    --> /tmp/rp-eco/eco.cml:115:12
     |
 115 |     return println('announced')
     |            ^^^^^^^
help: Analysis 4.1 asks a commit for order completeness: within one region, any two accesses to one resource of which at least one writes must have a path between them -- a data, sync or order edge, or a region's GATE input. Every pair of arms is a region of its own, so two accesses in different arms of one branch are never asked to be ordered; an access inside a loop and one outside are ordered through the loop node, which stands for its body. The message names the pair of nodes and the resource they touch.
EXIT=1
```

**首次被点名的对**是 `run` 的调用（它的足迹被记为 `write unknown`）与新加的 `:io/println`；新打印与已有三条
`println` 之间同样没有排序，只是检查器先报了这一对。

### 甲对照（`eco::print_ordered`）——**被接受**，退出码 **0**，输出正确

同一段体、同一个打印，改的是**落点**：落在块链上的一条语句（`main` 打印权重的那条）上，语句把位置让给它，
于是它按构造有序。

```
$ ./out/latest/bin/camel /tmp/rp-eco/train.cml eco::print_ordered std::nvm
loss before: 3.7500
loss after:  0.0000
announced
EXIT=0
```

未编辑的 `train.cml` 同样的前两行：

```
$ ./out/latest/bin/camel /tmp/rp-eco/train.cml std::nvm
loss before: 3.7500
loss after:  0.0000
[[1.9999925], [-0.99998784]]
EXIT=0
```

前两行逐字节相同 ⇒ 计算没被这个 pass 改变。**第三行是 `announced` 而不是权重**，因为"让它有序"的唯一现成
办法就是让体接管一条已在链上的语句的位置，被接管的就是那条权重打印。这不是掩饰：`order` 写不到刚 `inline`
出来的节点上（见 §5），所以对照只能用接管。

### 乙（`eco::step_print`）——**被拒**，退出码 **1**，但拒它的是**冲突检查**，不是效应检查

`at` = `step` 里 `nn:sgd` 那个节点（模型更新）。`step` 的类型不声明任何效应。

```
error[RuntimeDiag::PassEffectOrder]: 'graph::commit' committed a graph whose conflicting effects are not ordered: node 6 of graph 'step' (a call), in one region of graph 'step', has no order against node 10 of graph 'step' (:io/println), and at least one of the two writes (write unknown / write stdout): no data edge, no sync edge, no order edge and no region's GATE input makes one wait for the other, so the commit would publish two effects on one resource that the program leaves free to happen in either order
    --> /tmp/rp-eco/eco.cml:115:12
     |
 115 |     return println('announced')
     |            ^^^^^^^
help: Analysis 4.1 asks a commit for order completeness: within one region, any two accesses to one resource of which at least one writes must have a path between them -- a data, sync or order edge, or a region's GATE input. Every pair of arms is a region of its own, so two accesses in different arms of one branch are never asked to be ordered; an access inside a loop and one outside are ordered through the loop node, which stands for its body. The message names the pair of nodes and the resource they touch.
EXIT=1
```

**这不是"被接受"，所以不是"检查失效"。** 但也不是作者预期的 `PassEffectBeyondType`。同一编辑改落在
`step` 的 `autodiff:grad` 节点上（`eco::step_print_at_grad`）得到**一模一样**的 `PassEffectOrder` 原文。

**效应检查本身是活的，同一个构建上量到了。** 同一段体 `inline` 进 `loss`（类型也不声明任何东西，但体全由
算子组成、没有不可命名的调用）：

```
error[RuntimeDiag::PassEffectBeyondType]: 'graph::commit' committed a graph that reaches past the effect its own type declares: node 16 of graph 'loss' (:io/println), in a graph whose type declares nothing, makes an access the type does not cover (write stdout): the commit would publish a body that reaches past its own declared effect
    --> /tmp/rp-eco/eco.cml:115:12
     |
 115 |     return println('announced')
     |            ^^^^^^^
help: A commit owes the program it publishes the effect its target function declares: what the body reaches, resolved bottom-up from the operators the nodes name, must stay inside that clause. A pass can leave a body under a type that no longer covers it, and the graph module can be handed a body the type there was never widened for. The message names the node that makes the access and the resource the declaration does not cover, because the type is the promise a caller relies on.
EXIT=1
```

⇒ **`step` 上效应检查为什么不答：** `step` 的体里有一次**穿过函数值的调用**（`autodiff:grad` 的产物，
`CALL` 节点），它的足迹被分析记为 `write unknown`（两条 `PassEffectOrder` 原文里那个 `write unknown` 就是它）。
`resources.cpp:1215-1227`：`beyondCoverage` 对 `ResourceClass::Unknown` 的访问"跳过、只计数"；
`commitViolation`（`resources.cpp:1854-1868`）在编辑**同时改动了节点本身**时，只先问"编辑前那张图是否已经越界"，
已越界就**不问**编辑后的效应——以免"为一个本来就是那样的程序拒绝它"。`step` 就是这种：它原本已经有那次
不可命名的调用。**这是源码阅读，不是本轮量到的事实**；量到的事实是 §3 的两段诊断原文。

### 乙对照（`eco::step_print_ordered` 跑在 `train-step-sync.cml` 上）——**被接受**，退出码 **0**，输出正确

`train-step-sync.cml` 是 `train.cml` 的**新文件**（`train.cml` 一字未动）：`step` 的体里多一条
`println('stepping')`，于是 `step` 的类型声明了对 stdout 的写。对照 pass 把同一个 `announce` 体接在
`step` 链上的那条打印上，接管它的位置。

```
$ ./out/latest/bin/camel /tmp/rp-eco/train-step-sync.cml eco::step_print_ordered std::nvm
（200 行 announced）
loss before: 3.7500
loss after:  0.0000
[[1.9999925], [-0.99998784]]
EXIT=0
```

- 总行数 **203** = 200 行 `announced`（`step` 被调用 200 次，每次一行）+ 入口函数三行。
- 未编辑的 `train-step-sync.cml` 同样是 **203** 行 = 200 行 `stepping` + 同样的三行；两跑的三行入口输出逐字节相同，
  且与 `train.cml` 的原样输出相同 ⇒ 程序和它的计算都正确。

## 4 命令原文（主机 `88zjwei:~/camel-work/ledger`，逐条照抄）

```
cd ~/camel-work/ledger
export CAMEL_HOME=$PWD/out/latest
export LD_LIBRARY_PATH=$HOME/camel-eval/conda/envs/clang/lib:$CAMEL_HOME/libs:$CAMEL_HOME/lib
export CAMEL_PACKAGES=/tmp/rp-eco          # 里面是 eco.cml（edits-ecoop.cml 的副本）与两个程序
CAMEL=$CAMEL_HOME/bin/camel

$CAMEL --version
$CAMEL /tmp/rp-eco/train.cml std::nvm
$CAMEL /tmp/rp-eco/train.cml eco::print_unordered   std::nvm     # 甲
$CAMEL /tmp/rp-eco/train.cml eco::print_ordered     std::nvm     # 甲对照
$CAMEL /tmp/rp-eco/train.cml eco::step_print        std::nvm     # 乙
$CAMEL /tmp/rp-eco/train.cml eco::step_print_at_grad std::nvm    # 乙 的另一种落点
$CAMEL /tmp/rp-eco/train.cml eco::loss_print        std::nvm     # 效应检查是活的
$CAMEL /tmp/rp-eco/train-step-sync.cml std::nvm                       # 对照程序的原样输出
$CAMEL /tmp/rp-eco/train-step-sync.cml eco::step_print_ordered std::nvm   # 乙对照
$CAMEL test/cases/opt/commit_checks/fold_function_type.cml test::camel_commit_checks std::nvm   # build asks
```

构建（本机不编译，转发到主机；**不带 `-Plan`，只构建**，本轮不跑任何计划）：

```
pwsh -NoProfile -File D:\Projects\Camel-wt\ledger\internal\tools\camel-remote.ps1 -Path D:\Projects\Camel-wt\ledger -Jobs 8
→ [camel-remote] ledger: pushed eco/refused-passes, 0 changed file(s), 0 untracked
→ BUILD_SECONDS=6.2  BUILD_EXIT=0
→ [STEP] Stamped out/latest/build-info.json: 2d7c5c3a14 (clean), 1344 source files, fingerprint 0244fca0cbac, options 09afb0dfe7d8 (defaults)
→ [DONE] Release build completed
→ [load] 44.48 30.69 29.76 (1m/5m/15m) before run / 28.43 31.08 30.34 after run
```

`-Plan` 一律没给：本项的量是手工命令行跑 `camel`，不是计划。**没有 `-Tier all`、没有 `-Plan all`、没有跑整棵树。**

## 5 源码：本次**加**进 `edits-ecoop.cml` 的部分（原有四条未改）

只加了一行 `import`（`inline`、`inputs` 加进去），其余是新增。全文见
`case-study-variants/edits-ecoop.cml` 第 99–250 行。

```camel
// ---- two rewrites that add a printing call (the ECOOP paper's item (1)) ----
//
// `inline` is the edit that can make a node carrying an access: `replace`, `erase` and `add_constant`
// cannot, and `inline` splices a body whose code prints. Every rewrite below splices one body in and
// lets `commit` judge it; they differ in nothing but the place the body lands in, which is what makes
// the pairs a reading of the checks rather than of the print.
//
// The place is what decides the ordering, and it is the only thing a pass can decide it with today:
// `order` takes two nodes of the program the transaction was *opened* on, and the node `inline`
// answers with names the transaction (modules/graph/txn.cpp, `orderKernel` -> `draftOf`), so a pass
// cannot write an order edge to the node it just spliced. An `at` that is a statement of the block's
// chain therefore orders the body by construction, and an `at` that is a pure node only picks the
// region and leaves the body's ordering to `order`, which is exactly where the two cases part.

/// The body every rewrite splices in: it prints, and it hands the printed string on.
func announce(): string sync {
    return println('announced')
}

/// The type text a node carries, or `-` where it has none.
func type_text(n: Node): string {
    return match type_of(n) {
        case null => '-'
        case t => type_name(t)
    }
}

/// Whether the node runs the model update of the training step: `step` has one, and no other graph of
/// the program has any, so this names the graph without reading how it is written.
func is_update(n: Node): bool {
    return match op(n) {
        case null => false
        case o => op_name(o) == 'nn:sgd'
    }
}

/// Whether the node runs the gradient of the loss the training step takes: `step` has one, and no
/// other graph of the program has any, so this names the graph without reading how it is written.
func is_grad(n: Node): bool {
    return match op(n) {
        case null => false
        case o => op_name(o) == 'autodiff:grad'
    }
}

/// The training inputs of the entry function: the program's only constant of type `f32[4, 2]`, and it
/// stands in no chain.
func is_inputs(n: Node): bool {
    return constant_of(n, 'f32[4, 2]')
}

/// The entry function's print of the model's weights: the one printing call whose operand is the
/// tensor. Chosen by what it prints and not by where it stands, so the three prints of the program
/// are not told apart by the order they were written in.
func is_weight_print(n: Node): bool {
    let ins = inputs(n)
    return if !is_print(n) then false
        else if len(ins) != 1 then false
        else type_text(ins[0]) == 'f32[...]'
}

/// The printing call whose operand is a string the program already holds. `train.cml` has none of that
/// shape; `train-step-sync.cml` has the one the training step writes.
func is_literal_print(n: Node): bool {
    let ins = inputs(n)
    return if !is_print(n) then false
        else if len(ins) != 1 then false
        else is_constant(ins[0]) && type_text(ins[0]) == 'string'
}

/// Splices `announce` in at `at` and commits. Whether the print is ordered is `at`'s to say.
func spliced_at::<F>(p: F, at: Node): F! sync {
    let t = open(p)
    inline(t, at, announce)
    return commit(t)
}

/// 甲: a print added to `main` at a pure node, so the print is unordered against the three prints the
/// entry function already has. `commit` is expected to refuse it (the conflict check,
/// `RuntimeDiag::PassEffectOrder`).
export func print_unordered::<F>(p: F): F! sync {
    let at = only(p, (n: Node): bool => is_inputs(n))
    return match at {
        case null => p
        case a => spliced_at(p, a)
    }
}

/// 甲对照: the same body, the same print, spliced at a statement of the block's chain instead of at a
/// pure node -- the statement hands its place over, so the print is ordered there and `commit` is
/// expected to accept it. The statement it takes over is the entry function's print of the weights,
/// which is why the edited program prints the announcement where that line was.
export func print_ordered::<F>(p: F): F! sync {
    let at = only(p, (n: Node): bool => is_weight_print(n))
    return match at {
        case null => p
        case a => spliced_at(p, a)
    }
}

/// 乙: a print added to `step` at the node of the model update. `step`'s type declares nothing, and the
/// reading is not the one the paper predicted: `commit` refuses, but with `RuntimeDiag::PassEffectOrder`
/// -- the pair it names is the call of the gradient value and the print. `loss_print` below is the same
/// edit on a graph whose body is operations alone, and that one is refused by `PassEffectBeyondType`.
export func step_print::<F>(p: F): F! sync {
    let at = only(p, (n: Node): bool => is_update(n))
    return match at {
        case null => p
        case a => spliced_at(p, a)
    }
}

/// The same print in `step`, spliced at the node that takes the gradient instead. That node is read by
/// a call statement, so the splice changes a node in place as well as making one (`changed` is not
/// empty), and the reading is a different one: it is `commit_order` that answers, not `commit_effect`.
export func step_print_at_grad::<F>(p: F): F! sync {
    let at = only(p, (n: Node): bool => is_grad(n))
    return match at {
        case null => p
        case a => spliced_at(p, a)
    }
}

/// Whether the node runs the sum the loss takes of the squared error: `loss` has one, and no other
/// graph of the program has any.
func is_sum(n: Node): bool {
    return match op(n) {
        case null => false
        case o => op_name(o) == 'tensor:sum'
    }
}

/// The same print in `loss`, whose type declares nothing either but whose body is made of operations
/// alone: no call whose footprint the analysis cannot name. This is the reading that separates "the
/// effect check declined this graph" from "the effect check does not run on this build".
export func loss_print::<F>(p: F): F! sync {
    let at = only(p, (n: Node): bool => is_sum(n))
    return match at {
        case null => p
        case a => spliced_at(p, a)
    }
}

/// 乙对照: the same print added to `step`, in the program whose `step` prints and therefore declares
/// the write to stdout, and spliced at a statement of `step`'s own chain so it takes that place and is
/// ordered there. Expected to be accepted. Run it on `train-step-sync.cml`.
export func step_print_ordered::<F>(p: F): F! sync {
    let at = only(p, (n: Node): bool => is_literal_print(n))
    return match at {
        case null => p
        case a => spliced_at(p, a)
    }
}
```

**选点不读写法**：甲按 `f32[4, 2]` 那个常量、`loss` 按 `tensor:sum`、`step` 按 `nn:sgd` / `autodiff:grad`、
甲对照按"打印张量的那条打印"取点，没有一处取列表的第几个，也没有一处读图名（`name` 要 `spelling`，这里不要）。

## 6 两处顺手量到的东西（论文若不用，也别当成没看见）

1. **`order` 写不到刚 `inline` 出来的节点上。** `modules/graph/txn.cpp:423-433`：`orderKernel` 只把
   **第 2 个参数**过 `draftOf`（要求 `node->program() == state.base`），再要求两个句柄同程序；`inline` 回来的
   节点名字是**事务**的（`txn.cpp:580` `Handle::createInTransaction`），所以拿它当 `order` 任一端都被拒：
   `error[RuntimeDiag::InvalidArgument]: Invalid argument of 'graph.order': the node belongs to another program than the transaction.`
   而 `stdlib/graph.cml:375-375` 的文档与 `txn.cpp:475-478` 的说法是"`at` 是纯节点时，体写在已有访问之间的次序由
   `order` 说"——那句话今天**做不到**。这是接口缺口，不是本项的测量目标；甲对照因此改用"接管链上语句的位置"。
2. **`build asks=0` 与 commit 层的分档是两件事**，§2 已分开写。若论文只写"Release 默认不问"，会与今天
   甲、乙、`loss_print` 三条都被拒的读数表面冲突。

## 7 本页没有做的

- 没有跑任何计划（没有 `[SUMMARY]`，没有 pass/fail 计数）；没有 `-Tier all`、`-Plan all`、没有跑整棵树。
- 没有改 `train.cml`、`loss-*.cml`、例外表、`baseline.json`、`.plan.toml` 的期望值。
- 没有改 `stdlib/graph.cml`，也没有改任何 C++ 或 `modules/graph`。
- **`step` 上得到的是 `PassEffectOrder` 而不是 `PassEffectBeyondType`**：照实记在这里。要一个"效应检查拒
  案例程序"的读数，本页给的是 `loss`（§3 第三段），不是 `step`。