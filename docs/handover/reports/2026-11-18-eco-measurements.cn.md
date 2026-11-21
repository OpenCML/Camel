# ECOOP 测量：(a) 看不到写法、(b) 提交检查、(c) 无关效应并行

车道 B（车道图 2026-11-18）。依据 `docs/handover/plans/2026-11-18-commit-checks-and-spelling.cn.md`
末尾「验收与测量 (a)(b)(c)」，以及车道图「B 占 `test/cases/`、`test/plans/`、报告；用例先写，(a) 等 A
合入后跑最终读数」。

本线只加用例与脚本，不改实现，不碰 `stdlib/graph.cml`。**两根提交**：
`0e1a81be6`（用例、变换、fx 对照、报告骨架）与 `d47c7cfce`（(c) 进闸门、fx 对照被用例点名），
合计 `git diff --numstat b1ac24306 HEAD` 五行、**+879/-0**（见 §5）。基线 `b1ac24306`（`origin/develop`
就是它）。构建与运行全部在主机 `88zjwei:~/camel-work/eco`，本机未构建。

---

## 1 (a) 看不到写法：用例、脚本、读数

**落点**（都在 `test/cases/opt/writings/`）：

| 文件 | 是什么 |
|---|---|
| `greedy_fuse.cml.in` | **用 Camel 写的贪心融合变换** `greedy::fuse`：取一张图的乘法（`op_name` 以 `:op/mul` 开头），按 `nodes(g)` 给的顺序两两配对，每对融合成一个节点（后者被前者吸收，`replace`）。它**不问** 丙.2 列的任何一个读写法查询（`id`/`name`/`origin`/`show`），除图本身外能读的只有 `nodes` 的顺序 |
| `greedy_fusion_writings.py` | 用例：一个程序 `x*2 + y*3 + x*y` 的两种写法，各融合一次，比规范形式，**两边字节数与 sha256 都打印** |
| `fx_greedy_fusion.py` | torch.fx 对照（`symbolic_trace`，同一个变换），全文见附录 |
| `effect_parallel_clock.py` | (c) 的墙钟测量 |

`greedy_fuse.cml.in` 不是 `.cml` 文件：树里每个 `.cml` 都被前端 AST 金样本覆盖，用例把它拷进临时目录
再编译（`test/cases/trans/same_graph_writings.py` 出于同一理由把程序生成为字符串）。程序也是生成的：
两种写法由一份模板「交换两行」与「改四个局部名」机械得到，交换先用 `adjacent_swap` 验成第 2、3 行。

**能跑。** 计划 `test/plans/feat/opt/writings.plan.toml` 两条、都 `[PASS]`：

```
[PLAN] feat.opt.writings: opt.writings
[PASS] opt.writings.greedy_fusion_reads_no_writing          821.38 ms
[PASS] opt.writings.two_effects_wall_clock                     3.18 s
[SUMMARY] total: 2, pass: 2, fail: 0
```

**六条读数**（`b1ac24306`，Release，`std::jgir canonical=true` 的字节，两列是同一个 `same_graph` 的两侧）：

| 读数 | 左 sha256（字节） | 右 sha256（字节） | same_graph |
|---|---|---|---|
| `raw_one_form` 两种写法、未变换 | `e68d3eb6…a72d4d`（5446） | `e68d3eb6…a72d4d`（5446） | **true**，nodes=14 edges=13 |
| `changes_the_graph` 融合不是空操作 | `e68d3eb6…a72d4d`（5446） | `ee16c59b…481f72`（5023） | false，nodes 14→13 |
| `renamed` 改名、都融合 | `ee16c59b…481f72`（5023） | `ee16c59b…481f72`（5023） | **true** |
| **`swapped` 交换语句、都融合（判据）** | `ee16c59b…481f72`（5023） | `1ba7cad3…78e8422`（5023） | **false** ← 红在这一处 |
| `swapped_and_renamed` 两样都改 | `ee16c59b…481f72`（5023） | `1ba7cad3…78e8422`（5023） | false（与上一行同一个哈希） |
| `different_constant` `y*4` 替 `y*3` | `ee16c59b…481f72`（5023） | `7bb09cf3…ca7c64a`（5023） | false（比较器能说不） |

全哈希（`swapped` 一行）：

```
  canonical[left] base__fused.json: 5023 bytes, sha256=ee16c59be72bce0ae45ec2fa9327f4bcd5adefe73d34f62a0d8d8d02be481f72
  canonical[right] swapped__fused.json: 5023 bytes, sha256=1ba7cad31f8f5357680e09a0f03379a7a8b0a945983a0b06e41d9dc5578e8422
same_graph …/base__fused.json …/swapped__fused.json = false
canonical: first difference at byte 2085 (line 108, column 22); 5023 bytes vs 5023 bytes
```

**这三对哈希就是判据本身，而且是"红在预期的那一处"的证明**：融合前两侧是**同一串字节**
（`e68d3eb6…`，5446 字节），所以变换两次拿到的是同一张规范图；输出却不同 ⇒ 差异不是它读到的程序里的，
而是 `nodes` 给的顺序里的。换名那一行给出对照：`ee16c59b…` 两侧相同，且 `swapped_and_renamed` 与
`swapped` 落到**同一个** `1ba7cad3…`——只有语句顺序是变量，名字从来不是。

**红在哪（原话）**，`--require-one-form`（这就是 A 合入后计划该切过去的那条命令）：

```
the two writings of one program are TWO canonical forms: the transform reads the writing order, so `nodes` is not the canonical order yet (plan 丙.1 not in force)
FAIL: swapped: --require-one-form was asked and the two writings of one program are two canonical forms: `nodes` does not give the canonical order yet (plan 丙.1)
exit=1
```

计划跑的是**测量**形态（不加 `--require-one-form`）：`swapped` 读到 two 时要求「差异被点名」（对称差里
出现 `:op/mul_l`）并且要求「两侧输入同一串字节」这条已由上表成立，读到 one 时直接 `PASS`。所以今天
计划是绿的、绿在**预-A 状态带证据**这一支；A 合入后这一支按设计必须变成 `true`，否则 `FAIL`。
**这不是把红静音**：红的那条单独跑得出上面三行原话，且切换只需在 `args` 后加一个词。

---

## 2 (b) 提交检查：三条读数逐字

不重写用例，只跑 `test/plans/feat/opt/commit_refusal.plan.toml` 的三条并抄原文：

```
[PLAN] feat.opt.commit_refusal: opt.commit_refusal
[PASS] commit_refusal.order                                  70.15 ms
[PASS] commit_refusal.effect                                 73.93 ms
[PASS] commit_refusal.legal_splice                           71.22 ms
[SUMMARY] total: 3, pass: 3, fail: 0
```

直接跑三条拿原文（`camel test/cases/opt/commit_checks/fold_function_type.cml test::<name> std::nvm`）：

**(1) `order` 拒，exit 1**

```
commit_order: build asks=0, asked
commit_order: graph 'announced' already writes stdout at node 1 and now writes it again at 6 and 8, with no edge ordering either before the other
commit_order: refused, check=order
error[RuntimeDiag::PassEffectOrder]: 'test::commit_order' committed a graph whose conflicting effects are not ordered: node 1 of graph 'announced' (:io/println), in one region of graph 'announced', has no order against node 6 of graph 'announced' (:io/println), and at least one of the two writes (write stdout / write stdout): no data edge, no sync edge, no order edge and no region's GATE input makes one wait for the other, so the commit would publish two effects on one resource that the program leaves free to happen in either order
   --> /home/zjwei/camel-work/eco/test/cases/opt/commit_checks/fold_function_type.cml:11:5
 11 |     println(x)
    |     ^^^^^^^
```

**(2) `effect` 拒，exit 1**

```
commit_effect: build asks=0, asked
commit_effect: graph 'announced' will publish '(x: int) => int pure', the type of graph 'doubled', while its body goes on writing stdout
commit_effect: refused, check=effect
error[RuntimeDiag::PassEffectBeyondType]: 'test::commit_effect' committed a graph that reaches past the effect its own type declares: node 1 of graph 'announced' (:io/println), in a graph whose type declares nothing, makes an access the type does not cover (write stdout): the commit would publish a body that reaches past its own declared effect
   --> /home/zjwei/camel-work/eco/test/cases/opt/commit_checks/fold_function_type.cml:11:5
 11 |     println(x)
    |     ^^^^^^^
```

**(3) `legal_splice` 通过，exit 48**

```
commit_legal_splice: build asks=0, asked
commit_legal_splice: graph 'announced' writes stdout at node 1 and again at 6, ordered after it
commit_legal_splice: the commit published it, and nothing was refused
3
```

`48` 不是别的东西：`doubled(21)=42` 加 `announced(3)=6`，程序自己的返回值。**同一个 splice** 在 (1) 里被拒、
在这里被发布并算完——所以 (1)(2) 的拒不是"永远拒"。三条各自的首行 `build asks=0, asked` 是分档读数：
Release 构建默认不问这两条检查，是测试自己 `asked` 的。

---

## 3 (c) 两个资源能不能并行：能

第一个读数（本报告初版）把两个**文件**当成两个资源，作者的裁定否掉了它（`1b7905cd4` 的合并说明：
「rerun the parallel measurement on two distinct resources」）：两个文件是**一个**资源 `fs`，所以那次测的
不是并行。重做如下，结论反过来了：**两个不同的资源确实并行，快一倍**。

**两个资源，两处声明。**

| 资源 | 声明 |
|---|---|
| `clock` | `stdlib/process.cml:15`：`export func sleep(seconds: float): void`，`@effect waits`（"The wait is on the clock"）。标准资源名在 `src/core/resource_semantics.cpp:504`：`stdout stderr stdin clock env fs` |
| `rendezvous_point` | `stdlib/test_support.cml:17`：`export resource rendezvous_point unordered` —— "two calls on it keep no written order and may run at the same time (design principles 3.3)" |

**程序**：`wait_clock(200)` = `sleep` + `return 1`（void 效应必须在 `sync` 体里，否则被丢），
`wait_meet(200)` = `rendezvous(2, 0.2)`（等待点，只有一个调用者时等满超时并报告 1）；`main` **不是** `sync` 体。

**读数**（`std::nvm` 与 `-t 4 std::prl` 交替各 9 次）：

```
  serial    n=9 min=   430.1 ms  median=   433.6 ms  max=   438.5 ms   runs=432, 436, 430, 436, 438, 434, 434, 432, 431
  parallel  n=9 min=   230.8 ms  median=   235.2 ms  max=   245.7 ms   runs=231, 235, 240, 246, 235, 241, 234, 234, 233
median serial / median parallel = 1.84
reading: the two effects OVERLAPPED: one wait on the parallel back end, two in series on the serial one
reading: `clock` and `rendezvous_point` are two resources the scheduler keeps apart
```

**④ 调度器自己的话**（`std::prl diag=true`）：`graph 'main': 1 scope(s), **1 run as task graphs (3 tasks)**`
—— 之前那次是 `0`。

**把 `sync` 拎出来做对照**（同一个 `unordered` 资源，两次同样的调用，只差 `sync`）；这里的数是**值**不是秒数：
`rendezvous` 报告"多少调用者在同一时刻到达"，两个并行的都报 2，依次的各报 1：

```
  sync  serial 1001   parallel 1001
  free  serial 1001   parallel 2002
```

`free` 那一行在 `std::prl` 下 20 次全 `2002`、`sync` 那行 10 次全 `1001`。所以第一次读数的两个原因各归各位：
`fs` 是一个资源（`stdlib/fs.cml:12`：'calls on the file system keep their written order'）**并且** 那次程序把两次
调用写在了 `sync` 体里（`sync` 是前端给冲突效应加控制边的办法）。**不同资源 + 主体不是 `sync` ⇒ 并行。**

脚本 `test/cases/opt/writings/effect_parallel_clock.py` 可复跑（`--runs N`），计划
`test/plans/feat/opt/writings.plan.toml` 的第二条跑它（`--runs 3`），断的是上面那两行**值**，不断秒数。

---

## 4 六计划读数（`d47c7cfce`，主机 `-Jobs 8`，`-NoBuild`）

```
[camel-remote] eco: pushed lane-eco, 0 changed file(s), 1 untracked
[tier] fast: 204 plan file(s), 5393 declared test entries, 5393 started
[SUMMARY] total: 5393, pass: 5371, fail: 0, skip: 2, known failures: 20
```

逐名对基线 `b1ac24306`（本机无法量基线，只量了本提交；下表左列的基线数是决策者给的那组，
括号里是我这一跑的分项，`total/pass/fail/skip/known`）：

| 计划 | 基线 | 本提交 | 差 |
|---|---|---|---|
| `compile` | 2630/2613/0/17 | **2630/2613/0/17**（0/17） | 无 |
| `linear` | 869/864/0/3/2 | **869/864/0/2/3** | 无（`3`+`2` 两种标记合计 5，与基线同；哪一个跳、哪一个 xfail 见下） |
| `opt` | 1597/1597/0 | **1599/1599/0** | **+2，正好是本线加的两条**：`opt.writings.greedy_fusion_reads_no_writing`、`opt.writings.two_effects_wall_clock` |
| `parse` | 121 | **121/121/0** | 无 |
| `para` | 104 | **104/104/0** | 无 |
| `tools` | 70 | **70/70/0** | 无（第一次跑是 70/59/0/11 条跳，原因见下，装完依赖后 11 条真的跑了并且全绿） |

`total` 对得上：2630+869+1599+121+104+70 = **5393**，`pass` + `fail` + `skip` + `known` =
5371+0+2+20 = **5393**。基线的 5391 加**本线的两条**（不是一变二变三，就是两条）= 5393。

两处与基线那组数字**看起来**不一样，都不是本线的变化，且都量到底了：

- **`tools` 的 11 条跳**：`grammar.plan.toml` 的三条 `grammar.scopes.*` … 共 11 条带
  `skip_unless = ["node", "editors/vscode/scripts/have-deps.mjs"]`，那脚本查 `vscode-textmate` 与
  `vscode-oniguruma` 是否装了。新开的主机检出没跑过 `npm ci`，所以 11 条跳（我第一次跑时 `skip: 13`，
  正是这 11 条加两条 `without_codegen`）。装上之后 `skip: 2`，与基线一致：
  `cd editors/vscode && npm ci` → `added 296 packages in 3s`，`have-deps.mjs` 退出 0。这是**环境**，不是树。
- **`linear` 的 `3/2`**：我这一跑是 `skip=2, known=3`，合计与基线的 `3+2` 相同。`[XFAIL]` 那几条
  （`var.loop_carry_write_in_an_arm`、`element_assigned_append.in_place_form`、…) 与 `[SKIP]` 我一条没动过，
  本线也没有任何 `known_fail`。

`[XFAIL]` 20 条与基线同（`var.*`、`edges.*`、`transforms.*`、`*_gradient_*` 那些），一条都不是本线的。

另外跑了一次用例台账（不在六计划里，单独跑）：`node scripts/plan-inventory.mjs` →
`plan-inventory: 398 plan files, every one of them has a destination` /
`plan-inventory: 4149 case files, every one of them is read by code or recorded with a reason (0 recorded)`，
退出 0。本线新增的三个非计划文件都被代码读到，没有台账例外。

再跑了一次 `test/plans/feat/docs_links.plan.toml`（也不在六计划里，本报告是 `docs/` 下唯一新增的文件，
所以单独确认它没被报告拖红）：`total: 21, pass: 19, fail: 2`。两条红**都是既有的、不是本线的**：
`docs.links.citations` 列的 11 处是 `src/compile/gir/*.cpp` 与 `test/cases/linear/…` 里的行号引用
（本报告不在其中，且 `docs_links.plan.toml` 自己写着「Task reports and dated reviews are records and
are not scanned」）；`docs.links.questions_index` 是
`docs/handover/questions/README.md:8` 表头写「11 页」而目录里有 22 页（那就是车道 A 正在写的那批问答页）。

## 5 提交

`lane-eco`，父 `b1ac24306`，两根提交：

```
d47c7cfce test(opt): item (c) runs in the gate, and the torch.fx control is named by the case
0e1a81be6 test(opt): a transform that may not read the writing, and the two commit refusals as readings
```

`git diff --numstat b1ac24306 HEAD`：

```
181	0	test/cases/opt/writings/effect_parallel_clock.py
150	0	test/cases/opt/writings/fx_greedy_fusion.py
57	0	test/cases/opt/writings/greedy_fuse.cml.in
408	0	test/cases/opt/writings/greedy_fusion_writings.py
83	0	test/plans/feat/opt/writings.plan.toml
```

零删除行；未改任何实现文件，未碰 `stdlib/graph.cml`，未动 `baseline.json`、未打 `known_fail`、未 push。

§3 的重做（两个资源）改的是 `test/cases/opt/writings/effect_parallel_clock.py` 与`n`test/plans/feat/opt/writings.plan.toml` 的第二条，提交在 `lane-pclock` 分支上（父 `2e1e77324`，即本报告
落进 `develop` 的那根）。用例条数没变：`opt` 仍是 1599。

---

## 附录：torch.fx 对照脚本全文

`test/cases/opt/writings/fx_greedy_fusion.py`（提交内的同一份；构建机没有带 torch 的 Python，所以由
ECOOP 一侧跑：`pip install torch` 后 `python test/cases/opt/writings/fx_greedy_fusion.py`）。它把
`greedy_fuse.cml.in` 的步骤一一对应：取乘法 → 按图自己的顺序两两配对 → 每对融合成一个节点（后者
`replace_all_uses_with` 前者后 `erase_node`）。预期输出最后一行是
`fx_greedy_fusion: the two writings fuse differently, which is the control = PASS`。

```python
#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The **control** for the ECOOP measurement (a): the same greedy fusion on torch.fx.

Plan 丙's acceptance item (a) says the transform written in Camel must fuse two writings of
`x*2 + y*3 + x*y` to one graph, and that the same transform written against torch.fx must fuse them
to two. The second half is what makes the first a statement about **this** language: in torch.fx a
node's place in `Graph.nodes` is the order the Python was executed in, i.e. the writing, so a
transform that pairs "adjacent" multiplications by walking `Graph.nodes` pairs different
multiplications in the two writings. Nothing in torch.fx is wrong; it has no canonical order and does
not claim one.

This script is that second half, and it is why the plan entry is not here: **it needs a Python with
`torch`, and the build host has none** (2026-11-18: the host's `~/camel-eval/venv-build` and
`venv-bench` carry cmake/ninja/conan and onnx/onnxruntime, no torch; and the host cannot reach PyPI
from the campus network). A plan entry that skipped for want of an interpreter would put a skip in
the gate where nothing is measured, so this file is run on the ECOOP side:

    python -m pip install torch
    python test/cases/opt/writings/fx_greedy_fusion.py

and its source is quoted in `docs/handover/reports/2026-11-18-eco-measurements.cn.md`.

The transform is the same one, step for step, as `greedy_fuse.cml.in`:

  * take the multiplications among the graph's nodes **in the graph's own order**;
  * pair them two at a time, the first with the second, the third with the fourth;
  * fuse each pair into one node -- the second is absorbed into the first (`replace_all_uses_with`
    then `erase_node`, the counterpart of the graph module's `replace`).

Readings, printed one per line so that "different" is a reading and not an absence:

    torch: 2.x.y
    writing base: nodes=[...] fused=[...]
    writing swapped: nodes=[...] fused=[...]
    verdict: the two writings fuse to the same graph = false
    fx_greedy_fusion: the two writings fuse differently, which is the control = PASS

Exit status: 0 the control holds (the two writings fuse differently), 1 it does not, 2 no torch.
"""

import operator
import sys

PROGRAM = "x * 2 + y * 3 + x * y"


def base(x, y):
    a = x * 2
    b = y * 3
    c = x * y
    return a + b + c


def swapped(x, y):
    b = y * 3
    a = x * 2
    c = x * y
    return a + b + c


def renamed(left, right):
    first = left * 2
    second = right * 3
    third = left * right
    return first + second + third


WRITINGS = (("base", base), ("swapped", swapped), ("renamed", renamed))


def is_mul(node):
    return node.op == "call_function" and getattr(node.target, "__name__", "") == "mul"


def fuse(gm):
    """The greedy fusion of plan item (a), on one traced graph: pair adjacent multiplications."""
    nodes = list(gm.graph.nodes)
    muls = [node for node in nodes if is_mul(node)]
    fused = 0
    for i in range(0, len(muls) - 1, 2):
        first, second = muls[i], muls[i + 1]
        second.replace_all_uses_with(first)
        gm.graph.erase_node(second)
        fused += 1
    gm.graph.lint()
    gm.recompile()
    return fused


def describe(gm):
    """The graph by what it computes, ids erased: each node as `op(#input, ...)` in written order."""
    index = {}
    words = []
    for position, node in enumerate(gm.graph.nodes):
        index[node] = position
        arguments = []
        for argument in node.args:
            if hasattr(argument, "op"):
                arguments.append(f"#{index[argument]}")
            elif isinstance(argument, (int, float, bool, str)) or argument is None:
                arguments.append(repr(argument))
            else:
                arguments.append(type(argument).__name__)
        words.append(f"{getattr(node.target, '__name__', node.op)}({', '.join(arguments)})")
    return words


def main():
    try:
        import torch
        from torch.fx import symbolic_trace
    except ImportError as error:
        print(f"fx_greedy_fusion: no torch on this interpreter ({error})")
        print("fx_greedy_fusion: this control runs on the ECOOP side; see the docstring")
        return 2

    print(f"torch: {torch.__version__}")
    print(f"program: {PROGRAM}")
    fused = {}
    for name, function in WRITINGS:
        gm = symbolic_trace(function)
        before = describe(gm)
        pairs = fuse(gm)
        after = describe(gm)
        fused[name] = after
        print(f"writing {name}: fused_pairs={pairs}")
        print(f"  nodes= {before}")
        print(f"  fused= {after}")

    same = fused["base"] == fused["swapped"]
    print(f"verdict: the two writings fuse to the same graph = {'true' if same else 'false'}")
    print(f"verdict: the renamed writing fuses to the same graph as base = "
          f"{'true' if fused['renamed'] == fused['base'] else 'false'}")

    # What makes this a control and not a repeat: the swapped pair must part here. The renamed pair
    # may or may not -- a rename moves nothing in `Graph.nodes`, so on torch.fx too it is the same
    # graph, and that is the second half of the criterion: only the order of the statements is
    # visible, never the names.
    if same:
        print("fx_greedy_fusion: the two writings fused to one graph, so this is no control: FAIL")
        return 1
    print("fx_greedy_fusion: the two writings fuse differently, which is the control = PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```