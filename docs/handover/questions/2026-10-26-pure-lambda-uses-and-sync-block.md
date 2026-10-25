# 三处「函数资源子句」不一致 · 2026-10-26

> 说明（协调者）：本页由教程车道在主机 `88zjwei`（`out/latest/bin/camel`，build-info `ba56e9f21437`）逐条探针所得，复现文件在主机 `~/h598-probe/`，不在仓库里。我按原文落档，未改一字。
> 三件的性质不同：(a)(b) 是**缺陷**（规范写了要做而今天没做），(c) 是**措辞相抵、要你裁**。每条都给了选项、代价与建议，没有"等裁定"的写法。
> 另有一件小事顺便请你一句话授权：`docs/design/io-concurrency-streams/plan.cn.md:297` 引的那个程序今天仍带 `module` 行，而 `module` 声明已按前一次裁定删除（`scripts/strip-module-declarations.mjs` 现在报告无可删）。`docs/design/**` 我们无权改，**要动它需要你一句"可以"**。

共同点：**资源子句（`pure` / `uses`）今天只是"类型里写着的字面要求"，不是"对函数体推断出的资源集的检查"。**

## (a) 匿名函数满足不了 `pure` 形参 —— 缺陷

**是什么。** 形参类型写 `pure` 时，实参的类型也必须写着 `pure`；匿名函数没有任何写法带资源子句，于是它永远满足不了这个要求，哪怕它一个资源都不碰。

**逐字证据。**

`a_named.cml`（具名 + 显式 `pure`）通过，输出 `5`。

`a_lambda.cml`（同一个 lambda，不碰任何资源）被拒：
```cml
func apply(f: (x: int) => int pure, x: int): int { return f(x) }
func main(): int sync { println(apply((x: int) => x + 1, 4)); return 0 }
```
```text
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> ((x: int) => int, int).
   --> /home/zjwei/h598-probe/a_lambda.cml:8:18
    |
  8 |     println(apply((x: int) => x + 1, 4))
    |                  ^^^^^^^^^^^^^^^^^^^^^^
help: ... Available overloads:
          apply: (f: (x: int) => int pure, x: int) => int
```

`a_generic.cml`（具名、签名没写子句、体不碰资源）同样被拒，错误逐字相同，位置 `12:18`。

想给 lambda 加子句也没有语法：
```text
apply(((x: int) => x + 1) pure, 4)  → error[SyntaxDiag::MissingComma]: Expected ',' or ')' before 'pure'.  (8:39)
apply(pure (x: int) => x + 1, 4)    → error[SyntaxDiag::MissingComma]: Expected ',' or ')' before '('.    (8:24)
```

**规范怎么说。** 设计原则 3.3：
> **`pure`** 承诺函数不使用任何资源，由编译器检查；可以写在函数声明与函数类型上（如要求回调是纯函数：`f: (x: T) => U pure`）。
> **函数类型的参数默认对资源泛型**：`f: (x: int) => int` 不写资源，表示 `f` 用什么资源都可以……按实例化确定。**资源少的函数可以用在要求资源多的地方，反之不行。**

两条都指向：lambda 的类型"对资源泛型"，空资源集是它的合法实例化，所以在要求 `pure` 的位置它应当可用。今天不是。

**今天的读数。** 能喂饱 `pure` 形参的只有**签名里写着 `pure` 的具名声明**；具名但没写子句的、匿名的都不行。`pure` 今天是字面标注，不是推断结果。

**选项与代价。**
1. **让泛型在调用处按空集实例化**（照 3.3 第二条）：lambda 与没写子句的具名函数都能满足 `pure`；前提是有东西证明"它真的不碰资源"，即 (b) 要落地的推断集比对。代价：函数值类型要带推断集，所有调用处匹配从"字面子句"改成"推断 ⊆ 声明"，会**双向**改变今天的接受/拒绝（标准库要过一遍）。这是 3.3 原文的读法，最彻底也最贵。
2. **给 lambda 一门写法**（`(x: int): int pure => …` 或后缀 `pure`）：改文法 + 同一条"承诺要被检查"的机制。代价：语法形态要作者定（设计原则没写）；只解决匿名，`a_generic.cml` 那种仍被拒。
3. **不动，写进文档**：把"`pure` 形参只接受显式 `pure` 的具名声明"写成规则。代价 0 代码；3.3 第二条（子类型方向）继续不做，回调这个最需要它的位置用不了 `pure`（教程正是绕开：用一个具名 `twice`）。

**建议**：1（与 (b) 同批）；只想先止血则 2。

## (b) 带函数体的 `pure` / `uses` 不被检查 —— 缺陷

**是什么。** 带函数体的声明写了 `pure` 或 `uses`，编译器不拿函数体推断出的资源集比对：虚假的 `pure`（体内读写资源、甚至拿 `var` 形参）照常编译、照常执行效应、一个字也不报；显式 `uses` 被体超出也一样。

**逐字证据**（全部 exit 0）：

`b_pure_body.cml`：`pure` + 会打印的 `sync` 体 → 打印 `x` 与 `1`，**无任何诊断**：
```cml
func say(): int pure sync { println('x'); return 1 }
func main(): int sync { println(say()); return 0 }
```

`b_pure_var.cml`：`pure` + `var` 形参（`var` 形参本身就是一个资源）→ 打印 `2`，无诊断。效应设计稿说这条必须报 `UsesPromiseViolated`：
```cml
func bump(var c: int): int pure sync { c = c + 1; return c }
```

`b_pure_body2.cml`：虚假 `pure` 的函数被喂进 `pure` 形参——**接受**（只有一条"那个 `println` 没人用"的警告）：
```cml
func apply(f: (x: int) => int pure, x: int): int { return f(x) }
func say(x: int): int pure { println('x'); return x + 1 }
func main(): int sync { println(apply(say, 4)); return 0 }
```
```text
warning[SemanticDiag::EffectNotExecuted]: ... 8:5
5
```
即：`pure` 是**可伪证**的——(a) 里"匿名/具名没写子句的纯函数进不去"，而"写了 `pure` 的函数体里干什么都行"。

`b_uses_live.cml`：`func g(): int uses fs sync { println('x'); return 1 }`（体用 `stdout`，声明 `fs`）→ 打印 `x` 与 `1`，无错。

**规范怎么说。** 设计原则 3.3：
> 显式写 `uses` 是承诺，函数体超出即为编译错误
> **`pure`** 承诺函数不使用任何资源，**由编译器检查**

效应设计稿 `docs/design/effects/plan.cn.md` 第 5 步：
> `uses` 是承诺：推断 ⊆ 声明，超出报 `UsesPromiseViolated`；`pure` 要求空集。

同一份稿子的状态表把 3.3 这一行标成"**缺**（语言层）……函数类型没有 `pure`，没有资源子类型"；`UsesPromiseViolated` 在仓库里只有这份稿子里的 4 处命中——`diagnostics/registry.tsv` 没有它，`include/camel/core/error/diagnostics/messages/semantic.h` 也没有它。所以这不是漏一条诊断，是**整套检查还没做**。

**今天的读数。** 带体声明上的 `pure`/`uses` 只是注解：不比对推断集，也不影响匹配（匹配看字面类型）。同稿第 51 行写着推断"只在需要时算，不存进类型、不在编译期检查、无悬停、无变化警告"，与今天一致。

**选项与代价。**
1. **落地第 5 步**（推断集存进函数类型，声明是上界，超出报 `UsesPromiseViolated`，帮助指向第一个超出的使用点）。代价：类型带 footprint，调用处匹配改成"推断 ⊆ 声明"——正是 (a) 想借的规则，两件一起解决；风险是今天能编过的程序会有一部分被拒（标准库子句要逐条核对），需要一次迁移。
2. **只校验"体超出显式声明"**（先不动 (a) 的匹配）：挡住今天最危险的一类（虚假 `pure` + 副作用体）；代价：没写子句的函数仍不能满足 `pure`，且"推断集在哪层算"仍要定。
3. **不动，只把帮助里"编译器检查它能查的那些"写清楚**：代价 0；但 3.3 与教程会继续被读成"承诺会被检查"，虚假 `pure` 继续可伪造，也可能被优化器当纯调用处理——这是它不只是"少一条报错"的原因。

**建议**：2 先落地，1 作为同批后续（把 (a) 一并解决）。

**与教程的关系。** 教程第 10 章原先把"会打印"当成被拒原因（"把一个会打印的函数传进去……签名不匹配"），按今天读数不成立——没写子句、一个资源都不碰的具名函数同样被拒；已在提交 `24febeecd` 改成"签名里没有 `pure` 的函数"。教程没教"承诺会被编译器核对"，第 5 步落地后不需要撤回任何一句。

## (c) 设计原则 3.4 与语言参考 4.3.1 对"单独的 `sync { … }`"措辞相反 —— 要作者判

**是什么。** 同一个程序：非同步函数体里写一条单独的 `sync { println('inside') }`，值没人用。

设计原则 3.4：
> 写在块前的 `sync`（包括函数体的后缀写法 `func f(): T sync { ... }` 和**单独的 `sync { ... }`**）表示这是一个同步块：**它直接写着的语句里的效应一定发生，并且按书写顺序发生**

语言参考 4.3.1（`docs/language/04_execution_model.md`）：
> 单独的块语句 `sync { ... }`。注意它本身也是一个表达式：**写在非同步的函数体里而值没有被使用时，整个块不执行。**

**逐字证据**（`c_standalone.cml`，exit 0，**没有任何 stdout**）：
```cml
func main(): int {
    sync { println('inside') }
    println('after')
    return 0
}
```
```text
warning[SemanticDiag::EffectNotExecuted]: The call of 'println' has side effects but is never executed: nothing depends on it and it is not in a sync block.
   --> /home/zjwei/h598-probe/c_standalone.cml:4:12
    |
  4 |     sync { println('inside') }
    |            ^^^^^^^
...
warning[SemanticDiag::EffectNotExecuted]: ... 5:5   （println('after')：main 的函数体不是同步块）
```
把块当值绑定也不行：`let v = sync { println('inside'); 7 }` → `error[SyntaxDiag::UnexpectedToken]: Unexpected '{'; expected '('.`（4:18）。参考 4.3.1 的清单只列了函数体、分支臂/lambda 体、单独的块语句三个位置，`let v =` 不在其中，所以这一条不是矛盾。

**今天的读数。** 实现站在**语言参考**一边：单独的 `sync { … }` 作为语句、值没人用就整个不执行（块里的 `println` 也被判"没人依赖"）。要裁定的是 3.4 那句话。顺带一条小缺陷：那条警告的帮助文字说"it is not in a sync block"——它**就在** `sync { … }` 里；帮助文字只覆盖"不在同步块"，对"在同步块里但整个块没人用"没有话说。

**选项与代价。**
1. **按实现（=语言参考）改 3.4 的措辞**：把"一定发生"限定到**在同步链上**的同步块（函数体、被用到的块、链上的臂/尾表达式）："同步块在链上时，它直接写着的语句里的效应一定发生并按书写顺序发生；作为语句而值没人用的块不在链上，整个块不执行，且不向内继承。"代价：改一句话；风险 0（与实现、与 4.3.1、与 `10_sync_block.cml` 里"臂上没人要的效应不执行"都一致）。
2. **按 3.4 改实现**：让单独的 `sync { … }` 语句无条件执行块里直接写着的效应（当成同步链的锚）。代价：给"没人依赖的效应不执行"开例外，图上死代码消除、`EffectNotExecuted` 判据、参考 4.3.1 那条与帮助文字都要跟着改；风险：教程第 10 章"没人用的效应不执行（除非放进同步块）"整段示例要重新核对（尤其 `10_sync_block.cml` 里被警告的那个臂）。

**建议**：1。

**与教程的关系。** 教程第 10 章的同步块全是函数体，或写在链上的 `if second then sync { … }`（`10_sync_block.cml`），不依赖 3.4 对"单独的块语句"的说法；若采纳 2，教程需要重新核对。