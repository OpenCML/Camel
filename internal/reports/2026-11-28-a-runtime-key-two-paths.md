# 同一个读，两条路：`t.[k]` 拒绝运行期键，`:op/access` / `:op/access_key` 接受

**日期**：2026-11-28 03:4x　**来源**：`task-21` 第 13 条②（`D-1126-113` 每日审查第 13 条后半句"位置不是常量的图在提交时被拒"）

## 一、审查那句话成立的那一半（已用用例钉住）

```
test/cases/probe/e13/a_runtime_key.cml
func field(t: { a: int, b: int }, k: string) => int {
    return t.[k]            // 3:15
}
⇒ exit 1
e13/a_runtime_key.cml:3:15: [Error]: The key 'k' of a 't.[...]' access is not known when the
function is compiled.
The key is a string or integer literal, the 'n' of an enclosing 'static for', or an expression
whose value is known at compile time, such as a 'let' bound once to such a value.
A run-time value (a 'var', a parameter) cannot choose a field, because only the selected field
is built into the graph.
```
- 发出处：`src/compile/src/gir/builder_static.cpp:1690`，诊断名 `SemanticDiag::StaticAccessNotConstant`（`registry.tsv:213`，0x000179）。
- "提交时" = **函数被编译时**；位置在访问本身（`3:15`）。
- 真用例已补断言（名字 + 行 + 列 + 两段正文）：
  `test/cases/compile/static_expansion/static_access_not_constant.cml` +
  `test/plans/feat/compile/static_expansion.plan.toml`
  读数：`88 total / 88 pass / 0 fail`，支 `lane/item-13b @ e7928eaa41`。

## 二、不成立的那一半（同一读走操作符就绕过去了）

图级的 `:op/access` / `:op/access_key` 用**运行期键/位置**：

```
B  access_key::<{x: int, y: int}, int><k>(v)   k 是运行期参数  ⇒ exit 0，无诊断
C  access::<(int,int), int><i>(t)              i 是运行期参数  ⇒ exit 0，无诊断
D  同一函数两次调用给不同键 ⇒ stdout 2 / 1     （答案跟着参数走）
E  同一函数两次调用给不同位 ⇒ stdout 20 / 10   （答案跟着参数走）
```
- 键作为 **static data node** 挂 `With` 边进图：`src/compile/src/gir/builder_control.cpp:425-460`（`memberReadNodeOf`）⇒ **运行期操作数**。
- 与文档矛盾：`docs/stdlib-reference.md:731-733` —— "位置是调用的编译期参数，被读的成员在开跑之前就定了"。
- 与拒绝侧矛盾：语言自己的 `t.[k]` 拒绝这个形状，编译器把**同一个读**降到的操作符却接受；且 `access`/`access_key` 的 `<key>`/`<place>` 声明为编译期参数，**没有返回期检查**。

## 三、判定（需要决策者定口径）

两条互斥的读法，选一条：

- **甲：拒绝侧为准** —— 运行期键对字段选择无意义（只有被选中的字段进图）。若如此，`:op/access` / `:op/access_key` 的 `<key>`/`<place>` 在拿到非编译期值时应当拒绝（同一句诊断，或它们自己的 `@fails`），且 D/E 的行为是缺陷。
- **乙：接受侧为准** —— 图上"按运行期键取成员"是一个真实的操作（运行期在已建好的候选里选），那么 `t.[k]` 的拒绝就太严，诊断正文那句"只有被选中的字段才被建进图里"就不再普遍成立；`stdlib-reference.md:731-733` 要改。

**协调者的默认（未被推翻就按此记）**：**甲**。理由：诊断正文与文档都写着"只有被选中的字段才被建进图里"，这是语言自己在说的形状；乙要改的是原则级说法，不是一处实现。因此 D/E 记为缺陷，**缺陷页在这里**，修法归决策者（是否本轮修、由谁修，听派工）。

## 四、没有冻成契约的东西

D/E 的行为**没有**写成用例 —— 钉住等于把"运行期位置可用"冻成契约。探针已删（`test/cases/probe/e13`、`test/plans/probe/e13.plan.toml` 均不存在），需要时按本文第二节的命令重建。