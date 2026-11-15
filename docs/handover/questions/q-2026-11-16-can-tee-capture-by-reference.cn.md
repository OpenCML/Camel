# 闭包按值捕获落地了，`tee` 还在按引用：这条规则今天报不报？

## 事实

原则 3.5（`docs/design-principles.cn.md:197`）把闭包的捕获定成**值**，并且同一段接着写了它的代价：

> 闭包**不能**给外层的名字赋值（编译错误，诊断提示三条出路：返回一个新值、写成循环状态或 `fold`、
> 用 `Ref`）。闭包提到的 `var`，从闭包创建到它最后一次使用之间**不得**被赋值。

本轮的改动（(d2)，按值捕获）已经落地：一个写在原地的闭包（lambda，或写在另一个函数体里的
`func`）读到**本体的** cell 时，拿的是值，不再是那个 cell。`test/cases/compile/var/closure_read.cml`
与它的参照写法 `closure_read_let.cml` 现在编译成逐字节相同的 canonical 图，钉在
`test/plans/feat/compile/var.plan.toml` 的 `var.closure_read_is_the_let_writing`。

规则的另一半——"闭包不能给外层的名字赋值"——**没有**实现，而且今天就有一处真实的代码撞在它上面：
`stdlib/stream.cml:181` 的 `tee`。它有四个局部 `var`（`src:187`、`buffer:188`、`taken:189`、
`peak:190`），由它内部嵌套的 `func`（`pull:194`、`read:211`、`side0`、`side1`）在
`:198, :199, :214, :219` 写入，然后返回 `(unfold(0, side0), unfold(0, side1))`。

按设计 197 的字面意思，`tee` 的这四处都是**编译错误**。但它今天编得过，而且是对的：两路 `unfold`
共用一条流、共享游标，靠的就是那四个 cell。把按值捕获无条件套上去，`tee` 的五条计划条目
（`stream.tee_consumers`、`tee_result_is_tuple`、`tee_of_tee`、`tee_report`、
`tee_report_element_size_unknown`）会以
`camel: internal error: Runtime graph 'side0' breaks its edge invariants (4 violation(s))`
当场炸掉：闭包不再能写那个 cell，那四处写就悬空了。

所以设计文本和 `tee` 的用法是**直接冲突**的，本轮的代码没有在这里选边。

## 为什么这需要一个答复

两条路都站得住：

- **P1，现在就报**（照设计 197 的字面执行）：`tee` 改成作者已经定下的 `Ref`——四个 `var` 变
  `Ref`，四个嵌套 `func` 用 `get`/`set` 读写，返回值不变（`(T.., T..)`）。同一轮还要重写
  `docs/handover/reports/2026-11-15-the-var-migration-surface.cn.md` §3 列的那 17 组用例里
  "期望输出会变"的部分，其中 **`test/cases/linear/byref/*` 整组**（`var_capture_reference.cml`
  的每一条断言今天都是"按引用"：`if` 分支写 5、lambda 写完 `12 12`、`foreach` 写 6、嵌套 `func`
  写 8、被捕获的 `var` 形参写 21）。这不是一个 lane 能收口的东西，而且它会把 `feat/linear` 打红。
- **P2，先不报**（本轮的做法）：按值捕获只在设计 197 **允许它静默通过**的地方生效——闭包不写这个
  名字、这个名字也没在被捕获之后被写。规则会开枪的形状（闭包写外层名字、捕获之后还有写、名字被交给
  一个 `var` 形参或 `var` `with` 形参）一律**保留 cell，也不报诊断**，行为与改动前逐字节一致。

## 默认建议（按此开工，不另等）

**走 P2：本轮不报，按值捕获只落在设计 197 允许它静默通过的地方。** 理由：这个 lane 的题面是
"(d2) 把捕获改成按值，并处理依赖按引用的两处"，不是"实现 197 的诊断"；把诊断打开就必须同时迁移
`tee` 并重写 17 组用例（其中 `feat/linear/byref` 整组），那是下一轮的范围，硬塞进来只会把
`feat/linear` 打红、把一条已经绿了的标尺换成一条红的。代价要说清楚：**设计 197 的"不能在闭包创建之后
赋值"这一半今天没有执行**，`tee` 继续按引用捕获，而"闭包写外层名字"在语言里仍然合法。

下一轮的最小顺序（供作者裁定时参考，不影响本轮）：① 加 `err` 诊断 + 三条出路的帮助文本；② `tee`
迁到 `Ref`；③ 重写受影响的用例；④ 把 `decideCaptureByValue` 里的三条保守门（`programWrites`、
`handedOver_`、`lastWriteOffsetOf`）换成真正的规则。