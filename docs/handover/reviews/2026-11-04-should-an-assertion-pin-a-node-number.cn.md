# 断言侧钉 `node=`/`param=` 的期望串，要不要一律改成按名字或关系断言？

2026-11-04 22:40，`lane-repin` @ `f15eb9d7f`，主检出 `D:\Projects\Camel` 干净。
读数：`git grep -n 'node=' -- test/plans/` 27 行；`param=` 14 行；`git ls-files 'test/plans/*golden*'` 274 个文件里 0 处。

## 现象

本轮把 14 条 case×backend 的 `known_fail_msg` 从追踪行片段改钉到 `[Error]` 句（`known_fail` 只
该说「撞的是这堵墙」）。同一类缺陷在**断言侧**还在：`expect_output_contains` / `expect_stderr_contains`
里有 8 处把**图节点的当前编号**写成了事实。

它不是理论问题：本轮自证时给
`test/cases/compile/transforms/transpose_loop_escape_reads_outer.cml` 的 `read_outer` 加了一个节点
（`let z = k + 0.0`，值不变），实跑同一程序的编号从 `node=12`/`from=11`/`from=10`/
`with=[11,13,]` 平移到 `node=14`/`from=13`/`from=12`/`with=[13,15,]`，
`test/plans/feat/compile/transforms.plan.toml:956` 那三条 `expect_output_contains` 全不匹配，
而这条用例的 known_fail 仍然命中——**钩住编号的是断言，不是墙**。

## 今天有几处（27 行按用途分）

| 类别 | 处数 | 例 |
| --- | --- | --- |
| 绿用例的期望串里**字面编号** | 8 | `transforms.plan.toml:956`（`node=12 from=11 with=[11,13,]`）、`:908`、`:921`、`:1099`、`:1114`；`test/plans/feat/showcase_s2.plan.toml:228`（`node=7 position=0`）；`test/plans/feat/transforms/split.plan.toml:160`（`node=6`）；`test/plans/feat/compile/rules_angle_args.plan.toml:88`（`node=6 param=0 param-type=float[...]`） |
| 只写字段名、不写编号（已是关系） | 5 | `transforms.plan.toml:1193/1213/1227/1241/1258` 的 `"skip=call-no-transpose node="` |
| 与节点编号无关（字段名叫 `node` 的事实行 2 + 内联脚本里的 JS 3） | 5 | `test/plans/feat/modules/graph_facts.plan.toml:287`（`FACTS node_other node=valid`）、`test/plans/feat/opt/gate_value.plan.toml:81`（`const node=id=>…`） |
| 散文（注释 2 + `desc` 7） | 9 | `transforms.plan.toml:1092` 等 |

三类（按「红/绿/金标」）：
1. **红着并被钉住的 XFAIL pin**：`known_fail_msg` 里带 `node=` 的现在是 **0 条**（本轮之前 1 条：
   `rules_angle_args.mean_axis_transpose` 的 `node=5 position=0`，两后端）；
2. **绿用例的期望串**：**8 处**（上表第一行，落在 8 条用例上，其中 5 条在
   `test/plans/feat/compile/transforms.plan.toml`）；`param=` 另有 14 处同类；
3. **golden 文件**：274 个，`node=` **0 处**——金标这一侧今天没有这个问题。

## 三条路

1. **全改**：8 处字面编号一律改成按名字或关系断言（例如钉 `carried=2 … answer=per-target`、
   `ESCAPE target=0 … typeOfTarget=float` 与 `kindOfTarget` 的关系，而不是 `node=12`/`from=11`）；
   `param=` 的 14 处同样处理。**代价**：一次做完之后，一批今天靠「编号恰好成立」才没有暴露的
   断言会被解掉——其中可能真有几处一直在断言错的东西，要在同一次提交里逐条解释。
2. **只改会随律声明平移的那些**：先量出哪些用例的编号会随一次无关声明而移动，只改这些。
   **代价**：要先把「会平移」量清楚（本轮只量了 `transpose_loop_escape` 一族），范围窄但收益也窄，
   剩下那些只是「今天还没人碰过」。
3. **不改，改为允许编号差异的归一化**：比较前把 `node=`/`from=`/`with=`/`position=` 抹成占位符。
   **代价**：改动最小，但「编号错得离谱」也不再被发现——`node=12` 与 `node=99` 一样通过，
   等于把这几个字段从断言里删掉。

## 默认

**按第 1 条做**（全改）。理由：编号是实现细节，断言钉它就是把「今天恰好编号为 6」当成事实；
而 `lane-knownau` 今晚已经量出「pin 匹配残留」的代价——留在断言里的旧编号不会报错，只会在
下一次有人挪动节点时把一条正确的改动变成红。第 3 条把断言变松，比钉错更糟。

## 没定的事

- 8 处里 `test/plans/feat/compile/transforms.plan.toml:956` 的三条与 `:908`/`:921` 是一族
  （`transpose_loop_escape` 的 `carried`/`ESCAPE` 行），改成关系断言时是否合并成一条？
- `expect_stderr_contains` 里 `param=`/`param-type=` 是否比 `node=` 更该留（它钉的是算子的第几个
  参数与它的类型，语义而非编号）——按第 1 条做时会一并量到。