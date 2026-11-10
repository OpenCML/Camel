# 派生链位还没接上的三处

`refactor(runtime): derive a statement's chain place from the graph's structure`（`6b105528a`）换了链位的来源：一条语句在块里的位置不再由构建器打的标记给出，而是由图的形状读出来（`docs/design/effects/edges.cn.md` 2、3.1、4.6、5.3、5.7）。`test/cases/opt/generic/static_branches.cml` 的 `tick 1` 因此修好，`test/plans/feat/opt` 1185/1185 全绿。快速层在 `6ba46ba81` 之后还剩三处是这次改动带来的，都测到了根因，都还没有修：它们要动的东西互不相同，任何一个都不是"顺手改一行"。

读法：每处三句——**症状**（能复现的原话）、**量到的**（探针读数，含文件与行）、**下一步**（先动哪儿，以及为什么试过的两条路不通）。

---

## 一、`exemplars/05_effect_order`：内联交接之后调用者的下一条语句没有链位

**症状**

```
$ camel exemplars/05_effect_order/program.cml std::nvm
start / visit 3 / visit 1 / visit 2 / total 60
$ camel exemplars/05_effect_order/program.cml std::macro std::opt std::nvm
start / total 60
```

`visit 3 / visit 1 / visit 2` 是回调里的 `println`，加 `std::opt` 就整段丢掉。最小复现 `D:\tmp\e05-min2.cml`（两个字面元素 `[3, 1]`）：`std::opt` 下 `start / visit 3 / total 40`，只丢第二次。**递归是必要条件**：把它们写成不递归的等价形式，两条路都对；所以问题不在 `std::opt` 的分支化简本身，而在被内联的递归体交接链位的那一步。

**量到的**

`src/runtime/draft_inline.cpp` 的交接（`callHasSyncPlace && bodyHasChain` 那段，约 2439 行）在最小复现上读到：

```
handover start call=9 body head=16 tail=17 value=20 exit(tail)=17 users=1
append id=12 pred=17 entry=17 exitOfPred=17 idSyncIn=0 aliveId=true alivePred=true
append refused: place entry=17 exit=17 exitUsers=1
    [u=20 kind=13 region=3 sameBlock=true chainStmt=true exitOfU=20]
```

- `12` 是调用者的下一条语句（`kind=10`，`Func`），`17` 是被内联体的最后一条语句（`println`），`20` 是拼接为"调用值"写下的 `GATE`（`kind=13`）。
- `appendChainStatement(12, 17)` 被 `!syncUsersOf(place.exit).empty()` 挡下：`17` 已经有同步后继 `20`——拼接把体末语句直接连到了值门。
- 挡下之前循环已经执行过 `draft.setSyncInputs(user, {})`（这是 `appendChainStatement` 的前置条件），于是 `12` **既没有旧链位也没有新链位**。按 `edges.cn.md` 2，`12` 从此不从结果沿数据边或同步边可达，它带的效果不执行——这就是丢失的打印。

**下一步**

先定 `20` 的位置，而不是绕过守卫。拼接后的链应该是 `… → 17 → 12`，而值门 `20` 不能是 `17` 的同步后继（同步链不分叉，4.6）；门只承载调用值，所以它要么跟在块的最后一条语句之后（`12` 及其后继之后），要么像同段 2446 行那个特例一样被并进 `result.valueExit`。那个特例不匹配本例：它的前提是门唯一的同步输入等于调用节点（`funcNodeId`），而这里是 `17`（体末语句）。

两条试过并被回退的路，都记在这里以免重走：

1. 顺着体所占的槽位（`>= slotsBefore`）走到拼接代码的最后一个停点（即 `20`），把 `12` 接在 `20` 之后：`insert` 成功，但**两个打印全丢**（连 `visit 3` 也没了），说明把值门放到链中间会让体内的语句不再被结果读到。
2. `afterBody = result.valueExit`（同上，等价的直接写法）：同样全丢。

另外，`setSyncInputs(user, {})` 与随后的失败之间没有回滚，这一点无论最终怎么定位 `20` 都值得单独收紧：守卫拒绝时应当把旧链位放回去，而不是留下一个既无旧位也无新位的节点。

---

## 二、`new_values.peek_loop.graph`：读的用户不再是链上节点，`:var/peek` 判不出来

**症状**

```
[FAIL] new_values.peek_loop.graph   stdout does not contain ':var/peek'
```

`test/cases/consistency/new_values/peek_loop.cml` 的存在意义就是钉住"`len(out)` 的读留不住数组，于是每轮 `append` 原地做、循环是线性的"。现在图里是 `:var/get` + `:struct/append_arr_in_place` 之外还多一次拷贝，判据不成立。

**量到的**

`Builder::markNonRetainingReads`（`src/compile/gir/builder_diagnostics.cpp`，1188 行起的条件 3）要求读的**每个直接用户**都在 `chainedNodes_` 里。`__g_49__` 里这个读的用户是 `:struct/len_arr`——纯运算，按 5.3 已经不是链上语句，于是判据失败，读保持 `:var/get`。临时探针（已回退）读出 `read=15 -> keep`、`read=18 -> keep`，正是这一条。

**下一步**

条件 3 想说的是"读的值在变量被再次写之前就用完了"，而"用户是链上语句"只是它在前一个模型里的写法。在现在的模型里它是一条**顺序**问题：值必须被链上某条语句消费，且那条语句在变量下一次写之前。构建器手里只有 `chainedNodes_` 这个集合，没有顺链的顺序，所以要么在 `markNonRetainingReads` 里把顺序读出来（遍那边有 `isComputedAfter` 一类现成的走法），要么干脆把"纯的、结果不含引用的计算"钉到链上——后者是设计问题，不在这里定。

试过并被回退的写法：把条件 3 从"直接用户"放宽成"值能到达的所有节点，链上语句即钉住，纯且无引用的运算继续往下走"。`peek_loop` 因此变成 `:var/peek`，但 `new_values.peek.*` 六个后端一起红：`n0 = len(acc)` 的 `len` 被推迟到最后那个 `println` 才跑，读到的是几次 `append` 之后的数组，期望 `1` 打出 `2`。这条路的教训就是上面那句：**不是"能到达链"，而是"在下次写之前到达链"**。

---

## 三、`export_gated_gru_step`：间接调用的被调者变成了程序的参数端口

**症状**

```
export_gated_gru_step.cml:53:26: [Error]: An error value reached the exit of 'main' without being handled:
  ExportUnsupported: in 'run.pb': an indirect call remains after simplification;
  its callee must be known at export time.
```

**量到的**

`Evaluator::evalIndirectCall`（`modules/onnx/exporter.cpp` 3161 行）在失败点被探针（已回退）读到：

```
indirect call ref=2 with=1 kind=0 form=0 closureKind=0
  with[0] ref=1 nodeKind=1
```

- `with` 不空，所以不是"被调者边丢了"；
- `with[0]` 的节点是 `Port`（`kind=1`），求值结果却是常量张量（`kind=0 form=0`，`Tensor`）；
- 该图只有一个端口（`ref=1` 就是它），调用节点是 `ref=2`。

即：间接调用的被调者操作数指向了程序的**参数端口**，而那个端口绑的是 `(m, x)` 这个元组，不是 `step` 这个函数值。上报位置说的是"after simplification"，所以先怀疑内联/拼接把 WITH 边重新指错了对象——和第一处是同一块代码的邻居。

**下一步**

把 `run.pb` 的图在导出时打出来（今天没有任何 pass 能打印这个程序：`std::gir` 到不了，需要一行 `-vv` 或者给程序构建器一个 `std::jgir` 口），看调用节点的 `WITH[0]` 到底是谁。应当是 `step` 的函数常量或其闭包端口。在拿到那张图之前，任何改动都只能靠猜。

---

## 附带：环境噪声与本轮已修的部分

- `stream.generators_shapes_consumed.{nvm,fvm,jit,prl}`、`match.complete.prl`、`capacity.growth_nvm_gc_stress`、`registry.*`（5 个）、`gate.conformance_matrix`、`transpose.call_of_a_body_closure.function_value_argument_is_not_read_as_the_callee.nvm` 在满载并发跑快速层时红过，单独跑或换一轮负载就绿——是并发噪声，不是这次改动带来的。
- 本轮已经随 `46785728e`、`6ba46ba81` 修好的：`entering.graph_*`、`partial_application.graph`、`generic_values.graph`、`comprehension.graph_*`、`tutorial.examples.13_graph_rgir`（六张图加一对 `SYNC`+`CTRL`，即 4.6 的"调用是语句"）、`rename.{env,process}_graph_identity` 与 `poison` 里那份拷贝（摘要重录）、`framed.loops.regions.{lowered,compiled.inline}`（`ticks` 的 helper 理由从 `the round contains OPER` 换成 `the round makes the state tuple anew every round`，两条都是真的，行为不变）。