# 递给被调者的那个实参，得是一个「本图做出来的」值

**日期**：2026-11-19
**提出者**：车道 I（`lane-s3b-inplace`，基线 `91f708389`）
**状态**：待裁（本页写死一条默认，本轮按默认做）
**触发**：作者 2026-11-19 20:35 的分级（`f3fb9a4f9`）把 S3b 定为「调用后不再读的实参，交给被调者；计数保持为 1；计划选原地形式」。照做之后读数确实不再是 0，但两条既有不变式变红，且是**静默错值**（三个后端一致地错）。本页记下这次收窄、它为什么是经验性的，以及它上方悬着的那半个缺陷。

## 1. 做了什么

`ExecPlan::storageReuseAt` 在既有复制形式（`OperatorTable::inPlaceForm` 有、`plainForm` 无）的更新节点上记 `StorageReuse::Input`，`ExecPlan::operatorUriFor` 据此让后端跑原地形式而不是复制形式（`src/execute/exec_plan.cpp`、`include/camel/runtime/exec_plan.h`；三个后端 `fastvm/compile.cpp`、`nodevm/nodevm.cpp`、`taskflow/taskflow.cpp` 与 `macro/eval.cpp` 都改读 `operatorUriFor`）。

判据是两条，缺一不可：

- `nothingElseReadsIt`：从该值出发沿 `carried` 走，除这个更新外，读到它的节点必须把它继续带下去（JOIN/GATE/port），且它不是程序留着的（`keptByProgram`）；
- `valueMadeHere`：这个值必须是本图**做出来**的——`Copy`、`Fill`、调用的返回值，或另一个更新算子。

## 2. 读数（`camel inspect --copies`）

| 程序 | S3b 之前 | S3b 之后 |
| --- | --- | --- |
| `linear/byref/element_assigned_append.cml` | `:11:18 append copied`；main `updates 1 copied 1` | `:11:18 append in-place`；main `updates 1 in-place 1` |
| `consistency/new_values/sort_rebinding.cml` | main `updates 9 copied 9` | main `updates 9 in-place 6 copied 3`；`sorted_param by-count 1` |
| `modules/stdlib/array_sort_kernels.cml` | main `updates 10 copied 10` | main `updates 10 in-place 4 copied 6` |
| `consistency/new_values/rebinding_loop.cml` | `collect by-count 1`、`push_all 1 copied` | 不变（`push_all` 的数组来自 `var` 形参，不是本图做的） |

## 3. 为什么只有「没有第二个读者」不够

两条不变式，都是「第二个名字看见了后来的写」：

1. **`two_names_for_a_struct.cml`**：`var b = a` 之后 `b.xs[0] = 9`，`a.xs` 也变了（期望 `a [1, 2]`，实得 `a [9, 2]`）。`b.xs` 是一个 `GCNodeKind::Accs` 节点，它给出的数组就是 `a` 里那一个；`var b = a` 这个第二名字不为叶子计数。
2. **`value_used.cml`**：`rows[1]->append<2>` 写进了 `rows` 里的内层数组（期望 `[[0], [1]]`，实得 `[[0], [1, 2]]`）。`int[][]` 的元素读（`:op/idx_arr`）交出来的是内层数组本身，而 `forwardsItsValue` 对它答 false（结果类型的 code 是 Array，见 `exec_plan.cpp` 里 `handsOnOnlyOutsideArrays` 那段）。

**一句**：`nothingElseReadsIt` 证的是「本图里没有第二个**读者**」，需要的却是「没有第二个**名字**」。容器持有它的元素，这份图数不到这个持有——这正是作者说的「缺的是诚实的计数，不是认领次序」里还缺的那一半。所以 `valueMadeHere` 是补丁，不是原理。

## 4. 验收读数（本轮，八条计划）

同一个提交上测的：

| 计划 | 读数 | 基线 |
| --- | --- | --- |
| 六条（compile/linear/opt/parse/para/tools） | `total 5427, pass 5407, fail 0, skip 2, known failures 18` | 5427 / 5406 / 0（`element_assigned_append.in_place_form` 由 known_fail 转绿，+1 pass、-1 known） |
| `feat/modules` | `fail 3`，全为既有红：`runner.backend_matrix`（对负载敏感的 self-test）、`siamese_shared_encoder.devirtualize_inline.nvm`（`std::cxx::inline` 的效应次序）、`registry.reference_is_generated`（`docs/stdlib-reference.md` 漂移）。**三者在干净树上同样红**（已测） | 不劣于 |
| `feat/consistency` | `new_values.sort_rebinding.graph` 仍红（dump 打的是节点声明的 URI，`src/passes/trans/gir_metadata.cpp:152`；作者判据②把它归给观察层）；其余绿，含新增的 `new_values.sort_rebinding.copies` | 同样红 |

## 5. 问题

「容器持有它的元素，而这个持有不被计数」——这一半归谁，什么时候做？

- **(甲)** 按本轮这条经验规则收（`valueMadeHere` + `nothingElseReadsIt`），把计数留给下一批。代价：判据不是从原理推出来的，新形状出现时会再红一次——而红的是不变式，是静默错值，不是读数。
- **(乙)** 先做计数：元素读与字段读出来的值在被当作独立值使用时记一次持有，计划与运行时 `Array::writableInPlace()` 两边都看得见。代价：动运行时的引用计数与 `module/state` 的 `counted` 声明，范围远大于车道 I；而且数组块是 GC 管的、不是 `state::Ref`，计数的落点要先定（`Array` 的 block 计数，还是图槽的一次 retain）。
- **(丙)** 只保留读数：`storageReuseAt` 记下来但 `operatorUriFor` 不动。代价：读数与运行不是同一张表，而 11-17 那次被拒的正是这一点。

## 6. 默认（写死：甲）

**默认甲，本轮按甲提交。** 理由：它是今天唯一既让判词不为 0、又不让不变式变红的选择，而且方向是保守的（「都证明不了就拷贝」）；两条用例（`two_names_for_a_struct`、`value_used`）现在是它的守门人，任何把判据放宽的改动都会立刻红。

如果作者认为判据必须从原理来（乙），那么本轮的 `valueMadeHere` 应当在下一批里被计数替换掉，而不是被扩充——`docs/design-principles.cn.md` 10.5 的两层证明里，第二层（运行时计数）今天对「容器里的值」是不成立的，这才是缺陷本身。