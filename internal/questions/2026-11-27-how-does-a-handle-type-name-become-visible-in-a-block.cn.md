# 块体里写 `Node` 这样的句柄类型名，要谁把它带进作用域

**状态**：等负责人回答（默认见末节，按默认不改语义）

## 问句

设计页说“`graph` 模块导出的类型都是句柄类型；**签名里提到句柄类型的函数就是图算子，只能在块里调用**”。那么写这种签名的地方（块体自己、块里调用的用户函数）**怎么才能写出 `Node` 这个裸类型名**：

- (A) 块边界隐式把它带进作用域（与块头绑定根句柄同一件事的两半）；还是
- (B) 谁用谁具名导入（`import { Node } from graph`），块边界一概不管。

## 今天的读数

**① 探针用例本来就没有 `import graph`。**
```
test/cases/probe/editview/syntax_only.cml:1: import { is_function } from types
test/cases/probe/editview/view_only.cml:1:   import { is_function } from types
```

**② 按 (A) 补了隐式**模块**导入之后，读数一条没变**（`c09931ce37`，两文件 21 行，全在解析器侧）：
```
[SUMMARY] total: 15, pass: 11, fail: 4      RUN_EXIT=1
[FAIL] editview.sec2_fold / sec2_log / sec2_count / sec12_step      ← 与改动前逐名相同
```

**③ 第一道墙比块根句柄更上游：类型名本身没有被解析。**
```
[stderr] (sec2_fold) test/cases/probe/editview/sec2_fold.cml:5:57:
  [Error]: Unresolved type reference: 'Node'.
  Ensure the type is defined and visible in the current scope.
  (name=SemanticDiag::UnresolvedTypeReference, …)
```
用例里 `nodes(d)` 是有的（`sec2_count.cml:4`、`sec12_step.cml:5`、`sec2_fold.cml:5`），但它们**连 `d` 都还没被查到**，就先死在块体签名里的 `Node` 上。也就是说：`import graph` 只把**模块**带进来，`Node` 这个**名字**仍然不可见。

**④ 语言文档这边只说了“导入按名字”：**
```
docs/design-principles.cn.md:768  `import a from m` 是从 `m` 引入名字 `a`，等价于 `import { a } from m`；
                                 给模块起别名写 `import m as x`。导入可以改名。两个导入的名字冲突是编译期错误。
```
它没有说“导入一个模块会让它的名字裸可见”。**按这条读，(B) 才是现在的语言规则**；而设计页那句“签名提到句柄类型”预设了写签名的人能拿到那个名字 —— 两页之间缺的正是这一问。

## 为什么这一问必须由负责人定

- 按 (A) 做，块边界就多了一层“把一批类型名放进作用域”的语义，**而这层今天在任何设计页里都没有**，等于车道自己发明写法（卡上明禁）。
- 按 (B) 做，**示例与探针要自己写具名导入**，`builder_expr.cpp` 的隐式导入就是无用代码，应当撤掉。
- 两种都能让那四条探针红变成可判的红/绿，但落点完全不同：一个是解析器的可见性规则，一个是用例的写法。

## 默认（不等回答也照这个开工）

**按 (B)**：不改语义，块边界不带任何名字进作用域；探针/示例写 `import { Node } from graph`。
**理由**：`design-principles.cn.md:768` 明确写了“导入按名字”，(A) 会新增一条无出处的可见性规则；而 (B) 只需要改用例 —— 失败的方向是“用例写少了”，不是“编译器少做了一件事”。

**可被这两条读数推翻**：① 设计页里能找到“块内可以直接写句柄类型名”的原句（给 `file:line` 与原句）；② 或负责人口头给出 (A)。在这之前，`c09931ce37` 那笔隐式导入**保持行为中性、不进合并队列**，也不再加码。

## 影响面

- `test/cases/probe/editview/*` 四条探针（`sec2_fold`、`sec2_log`、`sec2_count`、`sec12_step`）能不能被判，取决于这一问。
- 块根句柄那条（`let d = <块头函数>` 的初值）**在墙后面**：类型名不可见时，连“`nodes(d)` 拿到什么”都量不到。