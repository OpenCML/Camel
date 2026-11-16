# 在调用的实参位置造出来的闭包，该读到写之前的值还是写之后的值？

> 一页一问。**默认建议写死在下面，本轮按默认开工，不另等。**

## 这一页要的答复

`src/compile/gir/captures.cpp#Builder::decideCaptureByValue` 里第三道保守门是这一条：

```cpp
if (programMayWriteThroughCall(name)) { return false; }   // 留格子
```

注释里写着它为哪条用例而留：`test/cases/linear/byref/var_param_reference.cml#write_then_read` ——
闭包在**那条调用的实参位置**被造出来，调用本身通过 `var` 形参写这个名字，所以闭包的**最后一次使用就是这次调用**，
3.5 要看的那段区间是空的，`CapturedVarHandedToVarParameter` 不报。**读数现在是 7**，注释写明「按值会是 0」。

直接构造 SSA 之后，`var` 形参改成「值进 + 值出」，写回是**调用处的一次重绑**（计划页 S4b）。
于是这次写不再经过格子：闭包造出来的时候，名字还是写之前的值。

**问题：`write_then_read` 的读数应该是几？**

## 默认建议（按此开工，不另等）

**改成「造闭包那一刻的值」（现在这个形状下是 0），并同步改期望。**

理由只有一条，但它是 3.5 的原话：**闭包持有的是名字在闭包被造出来那一刻的值，不是一个地方。**
造出来之后、闭包最后一次使用之前，这个名字被谁写、怎么写，都不该让闭包看见。
7 这个读数不是因为规则要求 7，而是因为今天格子还在：闭包捕的是格子，所以它看见了后一次写。
**保住 7 就必须保住那道门，而那道门是这次要拆的东西之一。**

**连带后果（写清，避免以后被当成回归）**：`test/cases/linear/byref/var_param_reference.cml` 由 `ok_` 正例改成
「读到写之前的值」的期望；`feat/linear/byref` 的读数按这条重记一次。

## 若作者另有裁定

- **若要保 7**：那么 `decideCaptureByValue` 的 `programMayWriteThroughCall` 这道门留着，语义是「闭包的实参位置那次调用
  里的写，看得见」。**代价是数**：B 的只读程序捕获从 43 变成 42，于是终值是 **79 不是 78**，10 模块级那栏之外多一个
  「实参位置闭包」的名额。这条要写进计划页 S6 的判据里，本计划的总账从 78 改 79。
- **若说「这个形状应当报错」**：`CapturedVarHandedToVarParameter` 的豁免（`captures.cpp` 里那段「闭包的最后一次使用
  就是这次调用」）撤掉，`write_then_read` 由正例改负例，`feat/linear/byref` 同样按负数重记。

## 这一页不改变的事

计划页的 S1–S3、S4a、S4b、S7 都不依赖这条答复；只有 S6 的判据与终值（78 或 79）依赖它。
把这条裁掉之前，S6 可以照默认先做，读数一并按默认记。
## 决策者答复（2026-11-16）

**同意默认：0。** 11-16 对"闭包交给 `var` 形参"一页已经这样答过（`reviews/2026-11-16-does-a-closure-handed-to-a-var-parameter-capture-by-reference.cn.md`）：闭包拿到的是造出来那一刻的值，`var` 形参的写回算在调用之后。7 是格子留下的读数。计划页 S6 的终值按 78 记，`write_then_read` 改期望。
