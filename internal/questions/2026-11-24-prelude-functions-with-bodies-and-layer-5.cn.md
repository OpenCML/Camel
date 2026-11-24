# prelude 内建层对**标准库模块**是否也生效？

- 日期：2026-11-24（同日改写：原问句「带函数体的 prelude 名字要不要进第 5 层」已被 `prelude-ambient` 答过，见下）
- 状态：待实测；不答复则按默认建议执行
- 来源：`lane/red-b-fixes` 修 `C-1123-64` 时提出；负责人 `D-1124-13` 已把原问句否掉

## 已定的部分（不再问）

`prelude` 是内建层，**不导入就看得见**。`prelude-ambient` 已于 2026-11-24 07:31 合进 `develop`。
所以「在 `random.cml` / `str.cml` 里写 `import { to_array } from prelude`」与这条原则**相反**，
那两行 `import` 不应当存在。原问句（「该不该进第 5 层」）因此作废 —— 不是被回答，是被**取代**。

## 还剩下的问题

`prelude-ambient` 合进 `develop` 之后，**编译标准库模块本身**时，prelude 的内建绑定是否也生效？

两种可能，修法相反：

**① 生效。** 那么 `C-1123-64` 的那两行 `import` 是冗余的（车道基点早于 `prelude-ambient`），
去掉即可，缺陷本身不存在。

**② 不生效。** 例如编译 stdlib 模块时 `exports_` 为空，`preludeEntity` 直接返回空 ——
那是 `prelude-ambient` 对标准库模块的**漏洞**，应当修那个漏洞，**不在调用处补 `import`**。

## 默认

不答复则按 **② 优先**：先把「标准库模块拿不到内建层」当成待证伪的假设去实测；
实测为 ① 才按 ① 处理。
——理由：① 只需要删两行，② 修的是原则的实现。**先按更难的那一支查，
因为按 ① 处理会在漏洞仍存在时留下两行看起来「已经修好」的 import。**

## 判据

只认诊断原文。在 `origin/develop` 上去掉那两行 `import`：
- 解析得到 ⇒ ①；
- 仍报 `Unresolved reference: 'to_array'` ⇒ ②。

## 相关

- `C-1123-64`：594 已作废，实测 12 条（`fbdd685a09`）。
- `perf.cml:37` `No matching function ... (Range)`：**同一族的另一个症状** —— 那里算子组存在、Range 重载不存在。
