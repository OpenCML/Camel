# A marker that asks for a `var` the compiler folded away before the printer saw it

日期：2026-11-20 ｜ 来自 `lane-merge-10`（工作树 `mrg2`）｜ 对象：`trans.cml / print_and_recompile`（`test/cases/trans/cml_contract.py`）｜ 状态：**默认按甲开工，待作者确认**

## ① 这条红在哪一行，以及它前半段已经是绿的

- 用例要的那一行是 `cml round trip: PASS`（`test/plans/feat/trans/cml.plan.toml:15` 的
  `expect_stdout_contains`），由 `test/cases/trans/cml_contract.py:355` 打印。
- 失败点是 `:270` 的 `assert marker in printed`，抛的是 `('write_value.cml', 'var ')`；标记表在 `:105`
  （`"write_value": ["var "]`）。
- **同一格的前半段是绿的**：`write_value.cml` 的往返在 `:234` 的 `compare(before_path, after_path)`
  上过了（`test/tools/cml_roundtrip.py` 判 `EQUAL:`），`:246`–`:267` 的运行等价也过了。红只在 marker
  这一行。这一点决定了甲与乙的代价，不是修辞。

## ② 为什么拼写不出来：marker 要的那两样，打印器收到的图里已经没有了

- 题面 `test/cases/trans/cml/write_value.cml:4`–`:8` 的体是 `var v = 1; v = 2; v`，**`v` 没有别的读者**。
- 读数（前一位车道在两条树上各读一遍）：以 `std::jgir` 读 `write_value.cml`，打印器拿到的图里既没有
  `:var/new` 也没有 `:op/assn`，记作 `var/new=False assn=False`。判据不靠这个记号：dump 里搜这两个名字，
  一条都搜不到。
- ⇒ 打印器拿到的是**折叠之后**的程序 —— 那次写的结果就是体的值，`var` 与赋值都不在图中。
  **任何拼写都写不出 `var `**，这不是打印器的缺陷，是它拿到的东西里没有这个构造。

**坐标（必写）**：造成它的那条读法（`var/new=False assn=False`）在**未改**的 `e9acae1dc` 上读数相同
⇒ 这个缺陷先前就有，**不是打印器那一笔造的**。

**但要把下一句也写清**：在 `e9acae1dc` 上检查器更早就断言了（`arm_read` 是语料第 4 位、
`write_value` 第 11 位，`:61`–`:78`），所以那边根本走不到这一行。**「同一条读法」不等于「同一条红」** ——
把这两句合成一句，就会得出「`e9acae1dc` 上有这条红」这个没人量过的结论。

## ③ `arm_read` 与 `write_value` 是两条不同的红

- `arm_read`：打印器把一个分支交出的格子版本写丢了共享 `COPY`（`MATCHER vertices: left 52, right 53`
  —— 右边缺那份共享 `COPY`）。
- 修它的那笔是 `2e09e8442`（`lane-print-fix` 上写的，`src/passes/trans/cml/cml.cpp` +106/−5，
  **已由 `6a10f0f87` 合进 develop**），那棵树上的读数是 `52 顶点 / 180 边 EQUAL`、探针 16 条全过、
  `print_then_run_agrees_with_the_origin` 8/8 未退。
- ⇒ **前者修好只是让检查器走到后者的断言，后者不是前者引起的。** 两者共有的只有一个事实：
  同一份 `cml_contract.py` 把它们串在一条 `for` 里（`:290`–`:291`），先撞哪一条由语料顺序决定。

## ④ 问作者的一件事：改标记，还是改输入？

- **甲（默认）：改标记。** `:105` 的 `var ` marker 是**对一个已被合法折叠的形状提要求**，而折叠是
  `std::cml` 的既定行为（`var/new=False assn=False` 就是它的结果，不是缺陷）。落到写法上，最小的一步是
  把 marker 换成**这次写的结果**（`counted()` 与 `counted_returned()` 的体都是 `2`）；再进一步是断言这两个
  体**打印成同一段**（`:2`–`:3` 的注释说它们本来就只差写法），但那要在 `cml_contract.py` 里多一段比较，
  比一个 `in` 重。
- **乙：改输入。** 让 `write_value.cml` 写成一个**不会被折掉**的形状（例如跨分支或跨调用可见），使打印器
  真的能写出 `var `。
- 注意同一个 marker 在 `var_param`（`:104`）那边是对的：`var` 形参必须写出来。所以 `var ` 不是天生的坏
  判据，它只是在这一格钉错了对象。

**⇒ 我选定甲（默认建议）：乙是把用例改成迁就检查器，而甲承认检查器钉的是一个实现细节。**

**⇒ 甲要付的代价（写死）**：marker 从「必须有 `var `」改成「必须有那次写的**结果**」；不这么改，就等于
把判据删掉一半 —— `var ` 存在的理由是「证明这个构造被走到过」，换过之后它只证「这次写的结果出现在该出现
的位置」，这两句话的强弱不一样，页里不把它说成同一句。

## ⑤ 「只剩这一条」的准确含义

检查器在 `:270` 断言失败就等于在语料第 11 位抛出，`:290` 的循环就此结束 —— **`write_value.cml` 之后的
五个程序（`string_conversion` / `named_type` / `map_with_closure` / `jgir_duplicate` / `hello`，
`cml_contract.py:61`–`:78`）在那棵树上一条都没跑**。

所以「只剩这一条」的意思是「**检查器跑到这里就停了**」，不是「它后面都是绿的」。要这句话，得先把这一格
放过去再量。

## ⑥ 读数坐标

本页的文件与行都在 `mrg2` 的 `6c88be5cd` 上读的。`2e09e8442` 的读数来自 `lane-print-fix`，而那笔修复
**已由 `6a10f0f87` 合进 develop** —— 所以：

- 在 `6c88be5cd` 及之前，develop 上语料能走到的第一条红是 `arm_read`；
- 从 `6a10f0f87` 起，develop 上语料能走到的第一条红**就是本页这一格**（前提是 `arm_read` 真的过了，那是
  `2e09e8442` 自己的读数，本页没有重跑）。

**⇒ 下一条动这条用例的车道，判据里要写清在哪棵树上量的**：`6c88be5cd` 与 `6a10f0f87` 上的第一条红不是
同一条，而「第一条红不同」正是 ② 与 ③ 两段各自要点出来的那件事。